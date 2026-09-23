#!/usr/bin/env python3
"""Prepare and optionally upload a deduplicated map collection through the Workshop API.

Uses existing verified mod snapshots; never uploads saves, replays or arbitrary folders.
Preparation is offline. --upload explicitly publishes the prepared maps and dependencies.
"""
from concurrent.futures import ThreadPoolExecutor
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import time
import urllib.error
import urllib.parse
import urllib.request


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ini(text):
    c = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(';',))
    c.read_string(text)
    return c


def fields(raw):
    return dict(line.split('=', 1) for line in raw.decode().splitlines() if '=' in line and not line.startswith('file='))


def post(endpoint, action, values):
    request = urllib.request.Request(endpoint.rstrip('/')+'/v1/content/'+action,
                                     urllib.parse.urlencode(values).encode(), method='POST')
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                data = response.read(524289)
            if len(data) > 524288:
                raise ValueError('Oversized response')
            result = dict(line.split('=', 1) for line in data.decode().splitlines() if '=' in line)
            if result.get('status') != 'ok':
                raise ValueError('Server rejected '+action+': '+result.get('code', 'unknown'))
            return result
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504) or attempt == 4:
                raise
            time.sleep(min(30, 2 ** (attempt+1)))
        except (TimeoutError, urllib.error.URLError):
            if attempt == 4:
                raise
            time.sleep(min(30, 2 ** (attempt+1)))


def publish(endpoint, owner, manifest, directory):
    digest = sha(manifest)
    start = post(endpoint, 'begin', {'hash': digest, 'manifest': manifest.hex(),
                                    'owner': owner, 'source': 'manual', 'promoted': '1'})
    if 'version' in start:
        return int(start['version'])
    def upload_file(line):
        h, size, encoded = line[5:].split(',')
        relative = Path(bytes.fromhex(encoded).decode())
        path = directory/relative
        if relative.is_absolute() or '..' in relative.parts or path.is_symlink():
            raise ValueError('Unsafe snapshot path')
        data = path.read_bytes()
        if len(data) != int(size) or sha(data) != h:
            raise ValueError('Snapshot verification failed: '+str(relative))
        for offset in range(0, len(data), 65536):
            post(endpoint, 'chunk', {'upload': start['upload'], 'file': h, 'offset': offset,
                                     'data': data[offset:offset+65536].hex()})
    file_lines=[line for line in manifest.decode().splitlines() if line.startswith('file=')]
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(upload_file,file_lines))
    complete = post(endpoint, 'commit', {'upload': start['upload']})
    if complete.get('hash') != digest:
        raise ValueError('Commit hash mismatch')
    return int(complete['version'])


CITY_BUILDINGS = {'industrial zone', 'police', 'zone commercial', 'residential zone', 'zone industrial', 'nuclear plant', 'police station', 'stadium', 'commercial zone', 'airport', 'powerline', 'road', 'power line', 'nuclear', 'zone residential'}
TORNIE_BUILDINGS = {'advanced wind trap 3x2', 'chemipost', 'advanced wind trap 3x3', 'advanced wind trap mk2', 'scoutpost', 'advanced windtrap 3x2', 'avant-poste', 'flame post', 'green post', 'chaosfactory', 'sentinel post', 'advanced windtrap 3x3', 'tech center', 'avant poste', 'love factory', 'scout post', 'advanced wind trap', 'worfinery', 'advanced windtrap mk2', 'flamepost', 'advanced windtrap', 'advanced wind trap mk3', 'techcenter', 'lovefactory', 'advanced wind trap 2x3', 'chaos factory', 'chemi post', 'advanced windtrap mk3', 'advanced windtrap 2x3'}


def infer_mod(c):
    category = 'vanilla'
    for section in c.sections():
        if section.lower() != 'structures':
            continue
        for key, value in c[section].items():
            if not re.fullmatch(r'(id|gen)[0-9]+',key,re.I):
                continue
            parts=value.split(',')
            building=parts[1].strip().lower() if len(parts)>1 else ''
            if building in CITY_BUILDINGS:
                return 'dunecity'
            if building in TORNIE_BUILDINGS:
                category='tornie'
    return category


def prepare(source, output, snapshots, owner, previous_output=None):
    previous = {}
    if previous_output:
        previous = {row["source"]: row for row in json.loads((previous_output/"inventory.json").read_text())["maps"]}
    seen, maps, skipped = set(), [], []
    for path in sorted(source.rglob('*.ini')):
        if path.name.endswith('.workshop.ini') or path.is_symlink():
            continue
        raw = path.read_bytes()
        if len(raw) > 1024*1024 or b'\0' in raw:
            skipped.append({'file': str(path), 'reason': 'binary or oversized, not a supported map INI'})
            continue
        if sha(raw) in seen:
            skipped.append({'file': str(path), 'reason': 'identical map already included'})
            continue
        try:
            try:
                text = raw.decode('utf-8-sig')
            except UnicodeDecodeError:
                text = raw.decode('cp1252')
            c = ini(text)
            sections = {s.lower(): s for s in c.sections()}
            if 'map' not in sections:
                raise ValueError('No MAP section')
            m = c[sections['map']]
            if not ('seed' in m or (0 < int(m.get('sizex','0')) <= 2048 and 0 < int(m.get('sizey','0')) <= 2048)):
                raise ValueError('Invalid dimensions')
            basic = c[sections['basic']] if 'basic' in sections else {}
            mod = infer_mod(c)
            # Reclassification does not rewrite an already published map's exact
            # gameplay dependency (e.g. a map containing Tornie units only).
            old = previous.get(str(path), {})
            dependency_mod = old.get('dependency_mod', old.get('mod', mod))
            if dependency_mod not in snapshots:
                raise ValueError('No pinned snapshot for mod '+dependency_mod)
            # Preserve ordinary map numbers (e.g. Alkozeltser 4); strip only explicit version suffixes.
            name = re.sub(r'\s*(?:[-_]\s*)?v\d+(?:\.\d+)*\s*$', '', basic.get('name',path.stem), flags=re.I).strip()
            item = sha(('legacy-map/'+owner+'/'+path.name+'/'+basic.get('author','')).encode())[:32]
            additions = {'mod': mod, 'mapversion': '1', 'name': name}
            lines, section, inserted = [], '', False
            for line in text.splitlines():
                match = re.match(r'\s*\[([^]]+)\]',line)
                if match:
                    section = match.group(1).lower()
                    lines.append(line)
                    if section == 'basic':
                        lines.extend(['Mod='+mod, 'MapVersion=1', 'Name='+name]);inserted=True
                    continue
                key = line.split('=',1)[0].strip().lower()
                if section == 'basic' and '=' in line and key in additions:
                    continue
                lines.append(line)
            if not inserted:
                lines = ['[BASIC]', 'Version=2', 'Mod='+mod, 'MapVersion=1', 'Name='+name]+lines
            data = ('\n'.join(lines)+'\n').encode()
            dependency = snapshots[dependency_mod].name
            manifest = ('DUNEWORKSHOP1\nkind=map\nid='+item+'\nname='+name.encode().hex()
                        +'\nbase=\nmod='+dependency+'\nfile='+sha(data)+','+str(len(data))+',6d61702e696e69\n').encode()
            digest = sha(manifest)
            target=output/'maps'/digest;target.mkdir(parents=True,exist_ok=True)
            (target/'map.ini').write_bytes(data);(target/'manifest').write_bytes(manifest)
            maps.append({'name': name, 'mod': mod, 'dependency_mod': dependency_mod, 'hash': digest, 'source': str(path), 'source_sha256': sha(raw)})
            seen.add(sha(raw))
        except (ValueError, configparser.Error) as error:
            skipped.append({'file': str(path), 'reason': str(error)})
    report={'maps':maps, 'skipped':skipped}
    (output/'inventory.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--previous-output',type=Path,help='Preserve exact dependencies from an earlier seed inventory')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--owner-file',type=Path,required=True)
    p.add_argument('--mod',action='append',default=[],help='name=/path/to/verified/workshop/revisions/hash')
    p.add_argument('--endpoint',default='https://dunelegacy.com/p2p')
    p.add_argument('--upload',action='store_true')
    a=p.parse_args()
    if not a.endpoint.startswith('https://'):
        p.error('Uploads require HTTPS')
    owner=a.owner_file.read_text().strip()
    if not re.fullmatch('[0-9a-f]{64}',owner):
        p.error('Invalid publishing owner file')
    snapshots={}
    for spec in a.mod:
        name,path=spec.split('=',1);folder=Path(path);raw=(folder/'manifest').read_bytes()
        f=fields(raw)
        if sha(raw)!=folder.name or f.get('kind')!='mod' or bytes.fromhex(f['base']).decode().lower()!=name.lower():
            p.error('Invalid mod snapshot '+name)
        snapshots[name.lower()]=folder
    a.output.mkdir(parents=True,exist_ok=True)
    report=prepare(a.source,a.output,snapshots,owner,a.previous_output)
    print('Prepared',len(report['maps']),'maps;',len(report['skipped']),'duplicates or invalid inputs',flush=True)
    if not a.upload:return
    receipt=[]
    try:
        for mod in sorted({r['dependency_mod'] for r in report['maps']}):
            folder=snapshots[mod]
            print('Sharing required mod',mod,flush=True)
            publish(a.endpoint,owner,(folder/'manifest').read_bytes(),folder/'files')
        for row in report['maps']:
            folder=a.output/'maps'/row['hash']
            version=publish(a.endpoint,owner,(folder/'manifest').read_bytes(),folder)
            receipt.append({**row,'version':version})
            print('Uploaded',row['name'],'version',version,flush=True)
    finally:
        (a.output/'uploaded.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__=='__main__':main()
