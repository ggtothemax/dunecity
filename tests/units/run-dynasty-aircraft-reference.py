#!/usr/bin/env python3
"""Run original Dynasty turret/unit scripts with ASan/UBSan.

Reuse original-source objects from compare-dynasty-routes.py and extract BUILD.EMC
from the local game bundle. The fixture directly places initialized turrets to
avoid an unrelated original Structure_IsUpgradable out-of-bounds read.
"""
import argparse
import csv
import hashlib
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dynasty-dir', type=Path, required=True)
p.add_argument('--reference-build-dir', type=Path, required=True)
p.add_argument('--build-dir', type=Path, default=ROOT / 'build')
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--reload', action='store_true', help='Measure turret and launcher firing intervals')
a = p.parse_args()
dd, ref, build, out = (x.resolve() for x in (a.dynasty_dir, a.reference_build_dir, a.build_dir, a.output_dir))
out.mkdir(parents=True, exist_ok=True)
commit = subprocess.check_output(['git', '-C', str(dd), 'rev-parse', 'HEAD'], text=True).strip()
assert commit == '4469449c75f51388ad2725297a95f09a6c601905'
assert not subprocess.check_output(['git', '-C', str(dd), 'status', '--porcelain', '--', 'src', 'include'], text=True).strip()
archive = build / 'bin/dunecity.app/Contents/Resources/DUNE.PAK'
data = archive.read_bytes()
pos, entries = 0, []
while True:
    offset = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if not offset:
        break
    end = data.index(b'\0', pos)
    name = data[pos:end].decode('ascii')
    pos = end + 1
    entries.append((name, offset))
scripts = {}
for i, (name, offset) in enumerate(entries):
    if name in ('BUILD.EMC', 'UNIT.EMC'):
        end = entries[i + 1][1] if i + 1 < len(entries) else len(data)
        scripts[name] = data[offset:end]
        (out / name).write_bytes(scripts[name])
assert len(scripts) == 2
assert scripts['UNIT.EMC'] == (ref / 'UNIT.EMC').read_bytes()
(out / 'provenance.txt').write_text('Dynasty ' + commit + '\n' + '\n'.join(
    name + ' sha256=' + hashlib.sha256(content).hexdigest() for name, content in scripts.items()) + '\n')
flags = ['-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-D__DARWIN_LDBL_COMPAT(x)=',
         '-I' + str(dd / 'include'), '-I' + str(ref / 'include'), '-iquote', str(dd / 'src')]
objects = sorted(str(x) for x in (ref / 'objects').glob('*.o') if x.name != 'dynasty-route-harness.c.o')
assert objects
binary = out / 'dynasty-aircraft-reference'
with (out / 'build.log').open('w') as log:
    source = 'dynasty-reload-reference.c' if a.reload else 'dynasty-aircraft-reference.c'
    subprocess.run(['cc', *flags, str(ROOT / 'tests/units' / source), *objects,
                    '-Wl,-dead_strip', '-lm', '-o', str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True)
with (out / 'encounters.csv').open('w') as results, (out / 'run.log').open('w') as log:
    subprocess.run([str(binary), str(out / 'UNIT.EMC'), str(out / 'BUILD.EMC'), str(out / 'shots.csv')],
                   stdout=results, stderr=log, check=True, timeout=120,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
rows = list(csv.DictReader((out / 'encounters.csv').open()))
if a.reload:
    assert len(rows) == 30 and all(int(r['shots']) >= 4 for r in rows)
else:
    assert len(rows) == 91
    assert all(r['outcome'] in ('escaped', 'killed') for r in rows if r['scenario'] == 'flyby')
print('Original Dynasty aircraft reference passed ASan/UBSan:', out / 'encounters.csv')
