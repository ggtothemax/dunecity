#!/usr/bin/env python3
"""Workshop API integration tests against real concurrent PHP HTTP workers."""
import hashlib
import json
import os
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
        self.share({'empty': b''}, kind='mod')
        self.service.write_config(content_quota_bytes=1048576)
        try:
            raw = manifest({'large': b'x' * 700000}, kind='mod', item='d' * 32)
            self.ok(self.begin(raw))
            raw2 = manifest({'large': b'x' * 700000}, kind='mod', item='e' * 32)
            self.assertEqual('quota_exceeded', self.begin(raw2).fields['code'])
            path = Path(self.service.state) / 'content/index.json'
            state = json.loads(path.read_text())
            for upload in state['uploads'].values(): upload['expires'] = 0
            path.write_text(json.dumps(state))
            self.ok(self.begin(raw2))
        finally:
            self.service.write_config()

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
