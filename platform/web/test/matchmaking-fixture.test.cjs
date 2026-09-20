const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { createHash } = require('node:crypto');
test('matchmaking fixture tracks the pinned SDK and unchanged upstream source', () => {
  const pin = require('../package.json').dependencies.p2pkit.split('#')[1];
  const text = readFileSync(__dirname + '/fixtures/bootstrapping-server.js', 'utf8');
  assert.ok(text.startsWith('// Copied from QuixThe2nd/p2pkit@' + pin + ': bootstrapping-server/server.js\n'));
  const body = text.split('\n').slice(2).join('\n');
  assert.equal(createHash('sha256').update(body).digest('hex'), '356361dcab71b853631ab2160464908cab01093055f1107fc7945cb3afe7c794');
});
