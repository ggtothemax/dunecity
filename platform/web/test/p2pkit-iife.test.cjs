// Verifies the committed IIFE bundle at platform/web/dist/p2pkit.iife.js: it
// must load in a bare script context (no Node/Emscripten globals beyond the
// standard web platform ones), expose exactly the surface the p2pkit SDK glue
// resolves as globalThis.P2PKIT_IIFE, and carry exactly the bytes the pinned
// p2pkit dependency built (only the provenance header is DuneCity's).

'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { webcrypto } = require('node:crypto');

const BUNDLE = path.join(__dirname, '..', 'dist', 'p2pkit.iife.js');
const PKG_DIST = path.join(__dirname, '..', 'node_modules', 'p2pkit', 'dist', 'p2pkit.iife.js');

// Surface the p2pkit package's own IIFE build exposes at the commit pinned in
// ../package.json (its "p2pkit/iife" entry / prepare-built dist output).
const EXPECTED_KEYS = [
  'DEFAULT_ICE_SERVERS',
  'DEFAULT_TRANSPORT_ORDER',
  'Emitter',
  'RTCDataChannelSendQueue',
  'RTCTransport',
  'RTCTransportConnectTimeoutError',
  'RTC_SEND_QUEUE_FLUSH_THRESHOLD',
  'capsFor',
  'chooseTransport',
  'directIceServers',
  'extractIP',
  'isInitiator',
  'randomId',
  'validateDirectCandidate',
  'validateDirectDescription',
];

function loadBundleInFreshContext() {
  const source = fs.readFileSync(BUNDLE, 'utf8');
  const sandbox = {
    TextEncoder,
    TextDecoder,
    performance,
    queueMicrotask,
    setInterval,
    clearInterval,
    setTimeout,
    clearTimeout,
    crypto: webcrypto,
    Event,
    console,
  };
  vm.runInNewContext(source, sandbox, { filename: 'p2pkit.iife.js' });
  return sandbox.P2PKIT_IIFE;
}

test('p2pkit IIFE loads in a bare vm context and exposes exactly the glue surface', () => {
  const kit = loadBundleInFreshContext();
  assert.ok(kit, 'bundle must expose globalThis.P2PKIT_IIFE');
  assert.deepEqual(Object.keys(kit).sort(), [...EXPECTED_KEYS].sort());
  assert.equal(typeof kit.RTCTransport, 'function');
  assert.equal(typeof kit.Emitter, 'function');
  assert.equal(typeof kit.RTCDataChannelSendQueue, 'function');
  assert.equal(typeof kit.randomId, 'function');
});

test('committed bundle is the pinned dependency\'s own build output (plus the provenance header)', () => {
  const committed = fs.readFileSync(BUNDLE, 'utf8');
  const headerEnd = committed.indexOf('\n', committed.indexOf('Exposes globalThis.P2PKIT_IIFE'));
  assert.ok(headerEnd > 0, 'bundle must start with the generated-by provenance header');
  const body = committed.slice(headerEnd + 1);
  let packageDist;
  try {
    packageDist = fs.readFileSync(PKG_DIST, 'utf8');
  } catch {
    assert.fail(`p2pkit dependency not installed (${PKG_DIST}); run npm install in platform/web`);
  }
  assert.equal(body, packageDist, 'committed bytes must match node_modules/p2pkit/dist/p2pkit.iife.js; run node tools/web/build-p2pkit-iife.mjs after changing the pin');
});

test('p2pkit IIFE DEFAULT_ICE_SERVERS are STUN-only', () => {
  const kit = loadBundleInFreshContext();
  assert.ok(Array.isArray(kit.DEFAULT_ICE_SERVERS));
  assert.ok(kit.DEFAULT_ICE_SERVERS.length > 0);
  for (const server of kit.DEFAULT_ICE_SERVERS) {
    assert.equal(typeof server.urls, 'string');
    assert.match(server.urls, /^stun:/);
  }
});

test('p2pkit IIFE Emitter and randomId work inside the bundle context', () => {
  const kit = loadBundleInFreshContext();
  const emitter = new kit.Emitter();
  const seen = [];
  emitter.on('msg', (m) => seen.push(m));
  emitter.emit('msg', 'a');
  emitter.emit('msg', 'b');
  assert.deepEqual(seen, ['a', 'b']);
  assert.match(kit.randomId(8), /^[0-9a-f]{16}$/);
  assert.notEqual(kit.randomId(8), kit.randomId(8));
});
