const { test } = require('node:test');
const assert = require('node:assert/strict');
const { mkdtempSync, writeFileSync, readFileSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const { join, resolve } = require('node:path');
const { execFileSync } = require('node:child_process');
test('preparing an incremental browser build does not duplicate the SDK runtime', () => {
  const dir = mkdtempSync(join(tmpdir(), 'dunecity-prelude-'));
  try {
    const bundle = join(dir, 'sdk.js'), runtime = join(dir, 'game.js');
    writeFileSync(bundle, 'const SDK = {};\n');
    writeFileSync(runtime, 'const Game = {};\n');
    const prepare = () => execFileSync(process.execPath, [resolve(__dirname, '../../../tools/web/prepend-p2pkit.mjs'), bundle, runtime]);
    prepare();
    const first = readFileSync(runtime, 'utf8');
    prepare();
    assert.equal(readFileSync(runtime, 'utf8'), first);
    assert.equal(first, 'const SDK = {};\nconst Game = {};\n');
  } finally { rmSync(dir, {recursive: true, force: true}); }
});
