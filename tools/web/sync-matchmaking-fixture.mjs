// Keep the local server fixture identical to the SDK revision consumed by the game.
import { readFileSync, writeFileSync } from 'node:fs';
const root = new URL('../../', import.meta.url);
const manifest = JSON.parse(readFileSync(new URL('platform/web/package.json', root)));
const pin = manifest.dependencies.p2pkit.split('#')[1];
if (!/^[a-f0-9]{40}$/.test(pin)) throw new Error('SDK must be pinned to a commit');
const response = await fetch(`https://raw.githubusercontent.com/QuixThe2nd/p2pkit/${pin}/bootstrapping-server/server.js`);
if (!response.ok) throw new Error(`Upstream fixture fetch failed: ${response.status}`);
const expected = `// Copied from QuixThe2nd/p2pkit@${pin}: bootstrapping-server/server.js\n// Refresh/check with node tools/web/sync-matchmaking-fixture.mjs [--check]\n` + await response.text();
const fixture = new URL('platform/web/test/fixtures/bootstrapping-server.js', root);
if (process.argv.includes('--check')) {
  if (readFileSync(fixture, 'utf8') !== expected) throw new Error('Matchmaking fixture differs from pinned upstream');
  console.log('Matchmaking fixture matches pinned upstream');
} else writeFileSync(fixture, expected);
