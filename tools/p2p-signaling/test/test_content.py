#!/usr/bin/env python3
"""Workshop API integration tests against real concurrent PHP HTTP workers."""
import hashlib
import json
import os
import re
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from test_signaling import SignalingTestCase, claims


def sha(data):
    return hashlib.sha256(data).hexdigest()


def manifest(files, kind='map', item='a' * 32, name='Shared dunes', base='', mod=''):
    return ('DUNEWORKSHOP1\nkind=' + kind + '\nid=' + item + '\nname=' + name.encode().hex()
            + '\nbase=' + base.encode().hex() + '\nmod=' + mod + '\n'
            + ''.join('file=%s,%d,%s\n' % (sha(data), len(data), path.encode().hex())
                      for path, data in sorted(files.items()))).encode()


def map_ini(size=(64, 64), players=2, mod=None):
    """A saved map in the format the client writes: a format version, dimensions, player sections."""
    basic = '[BASIC]\nVersion=2\nName=Shared dunes\n' + ('Mod=%s\n' % mod if mod is not None else '')
    body = '[MAP]\nSizeX=%d\nSizeY=%d\n' % size
    body += ''.join('\n[Player%d]\nBrain=Human\n' % (n + 1) for n in range(players))
    return (basic + body).encode()


class ContentTests(SignalingTestCase):
    def post(self, action, **form):
        return self.service.request('POST', '/v1/content/' + action, form)

    def begin(self, raw, owner='b' * 64, **extra):
        return self.post('begin', manifest=raw.hex(), hash=sha(raw), owner=owner, **extra)

    def ok(self, response):
        self.assertEqual(200, response.status, response.body)
        self.assertEqual('ok', response.fields['status'])
        return response

    def share(self, files, **kw):
        raw = manifest(files, **kw)
        begin = self.ok(self.begin(raw))
        if 'upload' not in begin.fields:
            return raw, begin
        token = begin.fields['upload']
        for data in files.values():
            for offset in range(0, len(data), 65536):
                self.ok(self.post('chunk', upload=token, file=sha(data), offset=offset,
                                  data=data[offset:offset + 65536].hex()))
        return raw, self.ok(self.post('commit', upload=token))

    def test_roundtrip_chunks_revisions_dedup_and_same_names(self):
        data = b'[BASIC]\nVersion=2\n' + b'x' * 150000
        raw, first = self.share({'map.ini': data})
        self.assertEqual('1', first.fields['version'])
        duplicate = self.ok(self.begin(raw, owner='c' * 64))
        self.assertEqual('1', duplicate.fields['version'])
        _, second = self.share({'map.ini': data + b'changed'})
        self.assertEqual('2', second.fields['version'])
        _, independent = self.share({'map.ini': data}, item='d' * 32)
        self.assertEqual('1', independent.fields['version'])
        result = self.ok(self.post('manifest', hash=sha(raw)))
        self.assertEqual(raw, bytes.fromhex(result.fields['manifest']))
        downloaded = b''
        for offset in range(0, len(data), 65536):
            result = self.ok(self.post('blob', hash=sha(raw), file=sha(data), offset=offset, count=65536))
            downloaded += bytes.fromhex(result.fields['data'])
        self.assertEqual(data, downloaded)
        rows = self.ok(self.post('list', kind='map')).multi['item']
        self.assertEqual(3, len(rows))
        self.assertEqual(['1', '2', '1'], [row.split(',')[2] for row in rows])

    def test_wrong_hash_partial_and_idempotent_chunks(self):
        raw = manifest({'map.ini': b'abcdef'})
        self.assertEqual(400, self.post('begin', manifest=raw.hex(), hash='0' * 64, owner='b' * 64).status)
        token = self.ok(self.begin(raw)).fields['upload']
        filehash = sha(b'abcdef')
        self.assertEqual('incomplete_upload', self.post('commit', upload=token).fields['code'])
        self.ok(self.post('chunk', upload=token, file=filehash, offset=0, data=b'abc'.hex()))
        self.ok(self.post('chunk', upload=token, file=filehash, offset=0, data=b'abc'.hex()))
        self.assertEqual(409, self.post('chunk', upload=token, file=filehash, offset=0, data=b'bad'.hex()).status)
        self.assertEqual(409, self.post('chunk', upload=token, file=filehash, offset=4, data=b'e'.hex()).status)
        self.ok(self.post('chunk', upload=token, file=filehash, offset=3, data=b'def'.hex()))
        a = self.ok(self.post('commit', upload=token))
        b = self.ok(self.post('commit', upload=token))
        self.assertEqual(a.body, b.body)
        self.ok(self.post('chunk', upload=token, file=filehash, offset=0, data=b'abc'.hex()))
        badraw = manifest({'map.ini': b'123456'}, item='c' * 32)
        token = self.ok(self.begin(badraw)).fields['upload']
        self.ok(self.post('chunk', upload=token, file=sha(b'123456'), offset=0, data=b'654321'.hex()))
        self.assertEqual('checksum_mismatch', self.post('commit', upload=token).fields['code'])
        self.assertEqual(404, self.post('manifest', hash=sha(badraw)).status)

    def test_duplicate_large_assets_and_resumed_upload_acknowledge_full_offset(self):
        # Real Dune2R idle/movement atlases contain identical >64KiB files.
        data = b'atlas' * 30000
        raw = manifest({'mod.ini': b'[Mod]', 'idle.png': data, 'movement.png': data}, kind='mod')
        token = self.ok(self.begin(raw)).fields['upload']
        for offset in range(0, len(data), 65536):
            self.ok(self.post('chunk', upload=token, file=sha(data), offset=offset,
                              data=data[offset:offset + 65536].hex()))
        # A new share attempt reuses staging, as does a second path for the same blob.
        self.assertEqual(token, self.ok(self.begin(raw)).fields['upload'])
        resumed = self.ok(self.post('chunk', upload=token, file=sha(data), offset=0,
                                   data=data[:65536].hex()))
        self.assertEqual(str(len(data)), resumed.fields['next'])
        self.ok(self.post('chunk', upload=token, file=sha(b'[Mod]'), offset=0, data=b'[Mod]'.hex()))
        self.ok(self.post('commit', upload=token))

    def test_owner_and_competing_initial_commits(self):
        files = {'map.ini': b'first'}
        raw = manifest(files)
        token1 = self.ok(self.begin(raw)).fields['upload']
        raw2 = manifest({'map.ini': b'other'})
        token2 = self.ok(self.begin(raw2, owner='c' * 64)).fields['upload']
        self.ok(self.post('chunk', upload=token1, file=sha(b'first'), offset=0, data=b'first'.hex()))
        self.ok(self.post('chunk', upload=token2, file=sha(b'other'), offset=0, data=b'other'.hex()))
        self.ok(self.post('commit', upload=token1))
        self.assertEqual('not_owner', self.post('commit', upload=token2).fields['code'])
        self.assertEqual('not_owner', self.begin(raw2, owner='c' * 64).fields['code'])
        fork = manifest({'map.ini': b'other'}, item='e' * 32)
        self.ok(self.begin(fork, owner='c' * 64))

    def test_concurrent_commits_allocate_unique_versions(self):
        tokens = []
        for data in [b'one', b'two', b'three', b'four']:
            token = self.ok(self.begin(manifest({'map.ini': data}))).fields['upload']
            self.ok(self.post('chunk', upload=token, file=sha(data), offset=0, data=data.hex()))
            tokens.append(token)
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda token: self.post('commit', upload=token), tokens))
        self.assertEqual(['1', '2', '3', '4'], sorted(self.ok(r).fields['version'] for r in results))

    def test_dependency_and_blob_membership(self):
        modraw = manifest({'mod.ini': b'[Mod]\nName=Test'}, kind='mod', item='c' * 32)
        mapraw = manifest({'map.ini': b'map'}, mod=sha(modraw))
        self.assertEqual('missing_dependency', self.begin(mapraw).fields['code'])
        self.share({'mod.ini': b'[Mod]\nName=Test'}, kind='mod', item='c' * 32)
        self.share({'map.ini': b'map'}, mod=sha(modraw))
        self.assertEqual(404, self.post('blob', hash=sha(mapraw), file=sha(b'[Mod]\nName=Test'), offset=0).status)
        self.assertEqual(1, len(self.ok(self.post('list', kind='mod')).multi['item']))

    def test_manifest_traversal_case_collisions_and_bounds(self):
        for files in [{'../bad': b'x'}, {'/bad': b'x'}, {'a\\bad': b'x'}, {'CON.txt': b'x'},
                      {'a.': b'x'}, {'a': b'x', 'A': b'x'}, {'a': b'x', 'a/b': b'x'}, {'A/b': b'x', 'a/c': b'x'}]:
            with self.subTest(files=files):
                self.assertEqual(400, self.begin(manifest(files, kind='mod')).status)
        self.assertEqual(400, self.begin(manifest({'wrong.ini': b'x'})).status)
        self.assertEqual(400, self.begin(manifest({'map.ini': b'x'}).replace(b'kind=map\n', b'kind=map\r\n')).status)
        self.assertEqual(400, self.begin(manifest({'map.ini': b'x'}).replace(b',1,', b',01,')).status)
        raw = manifest({'a': b'x', 'b': b'x'}, kind='mod')
        lines = raw.splitlines(keepends=True)
        self.assertEqual(400, self.begin(b''.join(lines[:-2] + lines[-2:][::-1])).status)

    def test_empty_files_and_quota_expiry(self):
        self.share({'mod.ini': b'[Mod]', 'empty': b''}, kind='mod')
        self.service.write_config(content_quota_bytes=1048576)
        try:
            raw = manifest({'mod.ini': b'[Mod]', 'large': b'x' * 700000}, kind='mod', item='d' * 32)
            self.ok(self.begin(raw))
            raw2 = manifest({'mod.ini': b'[Mod]', 'large': b'x' * 700000}, kind='mod', item='e' * 32)
            self.assertEqual('quota_exceeded', self.begin(raw2).fields['code'])
            path = Path(self.service.state) / 'content/index.json'
            state = json.loads(path.read_text())
            for upload in state['uploads'].values(): upload['expires'] = 0
            path.write_text(json.dumps(state))
            self.ok(self.begin(raw2))
        finally:
            self.service.write_config()

    def test_name_encoding_and_required_mod_metadata(self):
        raw = manifest({'map.ini': b'map'})
        for name in [b'   ', b'\xc0\xaf', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'\x80', b'\xe2\x82']:
            replaced = raw.replace(b'name=' + b'Shared dunes'.hex().encode(), b'name=' + name.hex().encode())
            self.assertEqual(400, self.begin(replaced).status)
        self.ok(self.begin(manifest({'map.ini': b'map'}, name='Dunes é 🌍')))
        missing = manifest({'atlas.png': b'atlas'}, kind='mod')
        self.assertEqual(400, self.begin(missing).status)

    def test_content_symlinks_fail_closed_and_no_uncommitted_reads(self):
        raw = manifest({'map.ini': b'test'})
        token = self.ok(self.begin(raw)).fields['upload']
        self.ok(self.post('chunk', upload=token, file=sha(b'test'), offset=0, data=b'test'.hex()))
        self.assertEqual(404, self.post('blob', hash=sha(raw), file=sha(b'test'), offset=0).status)
        staged = Path(self.service.state) / 'content/uploads' / token / sha(b'test')
        target = Path(self.service.tmp) / 'not-content'
        target.write_bytes(b'test')
        staged.unlink()
        staged.symlink_to(target)
        self.assertEqual(503, self.post('commit', upload=token).status)
        self.assertEqual(b'test', target.read_bytes())
        self.assertEqual(404, self.post('manifest', hash=sha(raw)).status)

    def test_large_manifest_fits_client_response_budget(self):
        files = {'%04d' % n: b'' for n in range(2800)}
        files['mod.ini'] = b'[Mod]'
        raw, _ = self.share(files, kind='mod')
        self.assertGreater(len(raw), 128 * 1024)
        response = self.ok(self.post('manifest', hash=sha(raw)))
        self.assertLessEqual(len(response.body), 512 * 1024)
        self.assertEqual(raw, bytes.fromhex(response.fields['manifest']))

    def test_pagination_preserves_every_revision(self):
        for n in range(53):
            # Keep this test focused on pagination, not per-minute abuse limits.
            if n == 25 or n == 50:
                (Path(self.service.state) / 'rate.json').unlink()
            self.share({'map.ini': str(n).encode()})
        first = self.ok(self.post('list', kind='map'))
        self.assertEqual(50, len(first.multi['item']))
        self.assertEqual('50', first.fields['next'])
        second = self.ok(self.post('list', kind='map', cursor=first.fields['next']))
        self.assertEqual(3, len(second.multi['item']))
        self.assertEqual('0', second.fields['next'])
        self.assertEqual('53', second.multi['item'][-1].split(',')[2])

    # ------------------------------------------------------------------------------------
    # Browsable map catalogue
    # ------------------------------------------------------------------------------------

    def catalogue(self, **form):
        response = self.ok(self.post('list', catalogue='maps', **form))
        return [row.split(',') for row in response.multi.get('item', [])], response.fields['next']

    def test_catalogue_reports_map_metadata_and_keeps_legacy_rows(self):
        self.share({'map.ini': map_ini(size=(128, 128), players=2)})
        rows, next_cursor = self.catalogue()
        self.assertEqual('0', next_cursor)
        self.assertEqual(1, len(rows))
        self.assertEqual(11, len(rows[0]))
        kind, item, version, digest, name, base, mod, modname, width, height, players = rows[0]
        self.assertEqual(['map', 'a' * 32, '1'], [kind, item, version])
        self.assertEqual('Shared dunes', bytes.fromhex(name).decode())
        self.assertEqual(['', '', ''], [base, mod, modname])
        self.assertEqual(['128', '128', '2'], [width, height, players])
        legacy = self.ok(self.post('list', kind='map')).multi['item']
        self.assertEqual(1, len(legacy))
        self.assertEqual(7, len(legacy[0].split(',')))
        self.assertEqual(digest, legacy[0].split(',')[3])
        # A map format version is not a content version: revision numbering stays server-assigned.
        self.assertEqual('1', version)

    def test_catalogue_shows_only_the_latest_revision_of_each_item(self):
        self.share({'map.ini': map_ini(size=(64, 64), players=2)})
        self.share({'map.ini': map_ini(size=(32, 32), players=3)})
        self.share({'map.ini': map_ini(size=(128, 128), players=4)}, item='b' * 32)
        self.assertEqual(3, len(self.ok(self.post('list', kind='map')).multi['item']))
        rows, _ = self.catalogue()
        self.assertEqual([['a' * 32, '2', '32', '32', '3'], ['b' * 32, '1', '128', '128', '4']],
                         [[r[1], r[2], r[8], r[9], r[10]] for r in rows])

    def test_catalogue_derives_legacy_seed_and_broken_map_dimensions(self):
        seed = b'[BASIC]\nMapScale=1\n\n[MAP]\nSeed=1234\n\n[Atreides]\nQuota=0\n[Ordos]\nQuota=0\n'
        self.share({'map.ini': seed})
        self.share({'map.ini': b'[BASIC]\nMapScale=0\n[MAP]\nSeed=7\n[Player1]\n'}, item='b' * 32)
        self.share({'map.ini': b'[MAP]\nSeed=7\n[Player1]\n[Player2]\n'}, item='c' * 32)
        # An unusable map stays a visible row rather than hiding the rest of the catalogue.
        self.share({'map.ini': b'\xff\xfe not an ini at all'}, item='d' * 32)
        self.share({'map.ini': map_ini(size=(99999, 8), players=1)}, item='e' * 32)
        rows, _ = self.catalogue()
        self.assertEqual([['32', '32', '2'], ['62', '62', '1'], ['64', '64', '2'],
                          ['0', '0', '0'], ['0', '0', '1']], [r[8:] for r in rows])

    def test_catalogue_filters_by_mod_size_and_players(self):
        self.share({'map.ini': map_ini(size=(64, 64), players=2)})
        self.share({'map.ini': map_ini(size=(64, 64), players=4)}, item='b' * 32)
        self.share({'map.ini': map_ini(size=(32, 32), players=2)}, item='c' * 32)
        modraw, _ = self.share({'mod.ini': b'[Mod]\nName=Test'}, kind='mod', item='f' * 32, base='dunecity')
        self.share({'map.ini': map_ini(size=(64, 64), players=2)}, item='d' * 32, mod=sha(modraw))
        # An absent or empty mod filter browses every mod, base game included.
        self.assertEqual(4, len(self.catalogue()[0]))
        self.assertEqual(4, len(self.catalogue(mod='')[0]))
        self.assertEqual(['a' * 32, 'b' * 32, 'd' * 32], [r[1] for r in self.catalogue(size='64x64')[0]])
        self.assertEqual(['a' * 32, 'c' * 32, 'd' * 32], [r[1] for r in self.catalogue(players='2')[0]])
        self.assertEqual(['a' * 32], [r[1] for r in self.catalogue(size='64x64', players='2', mod='*')[0]])
        self.assertEqual(['a' * 32, 'b' * 32, 'c' * 32], [r[1] for r in self.catalogue(mod='*')[0]])
        self.assertEqual(['d' * 32], [r[1] for r in self.catalogue(mod='dunecity')[0]])
        self.assertEqual(['d' * 32], [r[1] for r in self.catalogue(mod='DuneCity')[0]])
        self.assertEqual([], self.catalogue(mod='dunecity', players='4')[0])
        self.assertEqual([], self.catalogue(mod='?')[0])
        for bad in [dict(size='64'), dict(size='64x'), dict(size='0x64'), dict(size='64 x 64'),
                    dict(size='4096x4096'), dict(players='0'), dict(players='13'), dict(players='two'),
                    dict(mod='../etc'), dict(mod='a/b'), dict(mod='x' * 65), dict(kind='mod')]:
            with self.subTest(**bad):
                response = self.post('list', catalogue='maps', **bad)
                self.assertEqual(400, response.status, response.body)
                self.assertEqual('bad_content', response.fields['code'])
        self.assertEqual(400, self.post('list', catalogue='everything').status)

    def test_catalogue_mod_identity_comes_from_the_pinned_dependency(self):
        modraw, _ = self.share({'mod.ini': b'[Mod]'}, kind='mod', item='f' * 32, base='dunecity')
        self.share({'map.ini': map_ini(mod='DuneCity')}, mod=sha(modraw))
        self.share({'map.ini': map_ini(mod='somethingelse')}, item='b' * 32, mod=sha(modraw))
        # Without a pinned dependency a declared mod name proves nothing and stays unknown.
        self.share({'map.ini': map_ini(mod='dunecity')}, item='c' * 32)
        rows, _ = self.catalogue()
        self.assertEqual([('a' * 32, 'DuneCity'), ('b' * 32, 'dunecity'), ('c' * 32, '?')],
                         [(r[1], bytes.fromhex(r[7]).decode()) for r in rows])
        self.assertEqual(['a' * 32, 'b' * 32], [r[1] for r in self.catalogue(mod='dunecity')[0]])
        self.assertEqual(['c' * 32], [r[1] for r in self.catalogue(mod='?')[0]])
        self.assertEqual([], self.catalogue(mod='*')[0])
        self.assertEqual(3, len(self.catalogue(mod='')[0]))

    def test_catalogue_collapses_identical_map_files_for_the_same_mod(self):
        data = map_ini(size=(64, 64), players=2)
        self.share({'map.ini': data})
        self.share({'map.ini': data}, item='b' * 32, name='Same dunes, other item')
        modraw, _ = self.share({'mod.ini': b'[Mod]'}, kind='mod', item='f' * 32, base='dunecity')
        self.share({'map.ini': data}, item='c' * 32, mod=sha(modraw))
        self.share({'map.ini': map_ini(size=(64, 64), players=3)}, item='d' * 32)
        # Both copies remain separately addressable revisions; the catalogue lists one of each.
        self.assertEqual(4, len(self.ok(self.post('list', kind='map')).multi['item']))
        rows, _ = self.catalogue()
        self.assertEqual([('a' * 32, ''), ('c' * 32, 'dunecity'), ('d' * 32, '')],
                         [(r[1], bytes.fromhex(r[7]).decode()) for r in rows])

    def test_catalogue_rejects_dimensions_beyond_the_client_limit(self):
        self.share({'map.ini': map_ini(size=(2048, 2048), players=2)})
        self.share({'map.ini': map_ini(size=(2049, 64), players=2)}, item='b' * 32)
        rows, _ = self.catalogue()
        self.assertEqual([('2048', '2048'), ('0', '0')], [(r[8], r[9]) for r in rows])
        self.assertEqual(['a' * 32], [r[1] for r in self.catalogue(size='2048x2048')[0]])

    def test_catalogue_includes_host_uploads_and_stores_each_map_revision(self):
        data = map_ini(size=(64, 64), players=2)
        raw, _ = self.share({'map.ini': data}, item='b' * 32)
        self.assertEqual('1', self.ok(self.begin(manifest({'map.ini': map_ini(size=(32, 32), players=3)}),
                                                 source='host')).fields.get('version', '1'))
        token = self.ok(self.begin(manifest({'map.ini': map_ini(size=(32, 32), players=3)}),
                                   source='host', promoted='0')).fields['upload']
        hosted = map_ini(size=(32, 32), players=3)
        self.ok(self.post('chunk', upload=token, file=sha(hosted), offset=0, data=hosted.hex()))
        self.ok(self.post('commit', upload=token))
        rows, _ = self.catalogue()
        self.assertEqual([('b' * 32, '64', '64', '2'), ('a' * 32, '32', '32', '3')],
                         [(r[1], r[8], r[9], r[10]) for r in rows])
        stored = Path(self.service.state) / 'content/maps' / (sha(raw) + '.ini')
        self.assertEqual(data, stored.read_bytes())
        self.assertFalse(stored.is_symlink())
        metadata=json.loads(stored.with_suffix('.json').read_text())
        self.assertEqual(1,metadata['version'])
        self.assertEqual(sha(data),metadata['file_sha256'])
        self.assertEqual(2,metadata['max_players'])
        self.assertEqual(0o600, stored.stat().st_mode & 0o777)
        state = json.loads((Path(self.service.state) / 'content/index.json').read_text())
        self.assertEqual(2, len(state['maps']))
        self.assertEqual({'width': 64, 'height': 64, 'players': 2, 'mod': '', 'known': True, 'file': sha(data)},
                         state['maps'][sha(raw)])

    def test_catalogue_bootstraps_revisions_stored_before_the_map_index(self):
        raw, _ = self.share({'map.ini': map_ini(size=(128, 128), players=4)})
        self.share({'map.ini': map_ini(size=(64, 64), players=2)}, item='b' * 32)
        # Simulate revisions committed by an older server: no maps folder, no cached metadata.
        path = Path(self.service.state) / 'content/index.json'
        state = json.loads(path.read_text())
        del state['maps']
        path.write_text(json.dumps(state))
        for stored in (Path(self.service.state) / 'content/maps').iterdir():
            stored.unlink()
        rows, _ = self.catalogue()
        self.assertEqual([('128', '128', '4'), ('64', '64', '2')], [(r[8], r[9], r[10]) for r in rows])
        self.assertEqual(['a' * 32], [r[1] for r in self.catalogue(size='128x128')[0]])
        restored = Path(self.service.state) / 'content/maps' / (sha(raw) + '.ini')
        self.assertEqual(map_ini(size=(128, 128), players=4), restored.read_bytes())
        state = json.loads(path.read_text())
        self.assertEqual(2, len(state['maps']))

    def test_catalogue_pagination_walks_every_item_once(self):
        for n in range(52):
            if n == 25 or n == 50:
                (Path(self.service.state) / 'rate.json').unlink()
            self.share({'map.ini': map_ini(size=(64, 64), players=2) + ('; variant %d\n' % n).encode()}, item='%032x' % n)
        first, next_cursor = self.catalogue(players='2')
        self.assertEqual(50, len(first))
        self.assertEqual('50', next_cursor)
        second, done = self.catalogue(players='2', cursor=next_cursor)
        self.assertEqual(2, len(second))
        self.assertEqual('0', done)
        self.assertEqual(52, len({row[1] for row in first + second}))

    def test_inspect_private_code_before_content_download(self):
        admission, session = self.seat()
        form = claims(contentHash='e' * 64)
        form['room'] = admission.fields['room']
        result = self.service.request('POST', '/v1/admission/inspect', form)
        self.assertEqual(200, result.status, result.body)
        self.assertEqual('a' * 64, result.fields['contentHash'])
        self.assertEqual('0', result.fields['running'])
        self.assertNotIn('grant', result.fields)
        self.assertEqual(409, self.join(form['room'], contentHash='e' * 64).status)
        form['appVersion'] = '0.0.0'
        self.assertEqual(409, self.service.request('POST', '/v1/admission/inspect', form).status)


if __name__ == '__main__':
    unittest.main(verbosity=2)


class ApacheIngressTests(unittest.TestCase):
    def test_apache_routes_every_php_endpoint_and_accepts_manifest_bound(self):
        public = Path(__file__).resolve().parents[1] / 'public'
        entry = (public / 'index.php').read_text()
        apache = (public / '.htaccess').read_text()
        pattern = re.search(r'^RewriteRule (\S+) index.php', apache, re.M)[1]
        routes = " ".join(re.findall(r"const (?:CONTENT|ADMISSION|SIGNALING)_PATHS = \[(.*?)\];", entry, re.S))
        for path in {"/v1/health", *re.findall(r"'(/v1/[^']+)'", routes)}:
            self.assertRegex(path[1:], pattern, path)
        for path in ('v1/content/delete', 'v1/content/blob/secret', 'config.php', 'src/Content.php'):
            self.assertIsNone(re.fullmatch(pattern, path), path)
        limit = int(re.search(r'^LimitRequestBody (\d+)', apache, re.M)[1])
        content_limit = int(re.search(r'\$http->form\((\d+), Content::MAX_MANIFEST', entry)[1])
        self.assertEqual(content_limit, limit)
