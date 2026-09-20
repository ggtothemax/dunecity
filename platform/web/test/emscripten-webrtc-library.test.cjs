'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '../../..');
// The two --js-library files the browser build links (src/CMakeLists.txt):
// the installed p2pkit SDK's glue and DuneCity's adapter on top of it.
const SDK_GLUE = path.join(ROOT, 'platform/web/node_modules/p2pkit/emscripten/js/p2pkit_webrtc_glue.cjs');
const ADAPTER = path.join(ROOT, 'platform/web/dunecity_webrtc_config.js');
const VERIFY = path.join(ROOT, 'tools/web/verify-dunecity-js.mjs');

function hoistEmscriptenLibraryHelpers(lib, target) {
  for (const [key, value] of Object.entries(lib)) {
    if (key.startsWith('$')) {
      target[key.slice(1)] = value;
    }
  }
}

test('installed SDK glue retains createP2pkitWasmGlue', () => {
  assert.ok(fs.existsSync(SDK_GLUE), `p2pkit SDK glue missing at ${SDK_GLUE}; run npm install in platform/web`);
  const text = fs.readFileSync(SDK_GLUE, 'utf8');
  assert.match(text, /\$createP2pkitWasmGlue:\s*createP2pkitWasmGlue/);
  assert.match(text, /createP2pkitWasmGlue\s*\(/);
  assert.doesNotMatch(text, /\$createP2pkitWasmGlue\s*\(/);
});

test('adapter wires $webrtcInit deps over the SDK glue', () => {
  const text = fs.readFileSync(ADAPTER, 'utf8');
  assert.match(text, /\$webrtcInit__deps:\s*\[\s*'\$createP2pkitWasmGlue'\s*\]/);
  assert.doesNotMatch(text, /dunecityBridgeP2pkit/);
  assert.match(text, /webrtcFindMatch__deps:\s*\[\s*'\$webrtcInit'\s*\]/);
  assert.match(text, /webrtcCancelMatch__deps:\s*\[\s*'\$webrtcInit'/);
  assert.match(text, /\bwebrtcInit\s*\(/);
  assert.doesNotMatch(text, /\$webrtcInit\s*\(/);
});

test('verify-dunecity-js.mjs accepts current adapter + SDK glue sources', () => {
  const result = spawnSync(process.execPath, [VERIFY, '--source', ADAPTER], {
    cwd: ROOT,
    encoding: 'utf8',
  });
  assert.equal(result.status, 0, result.stderr || result.stdout);
});

// Loads both libraries the way the Emscripten link does: one shared
// LibraryManager, SDK glue first (defines $createP2pkitWasmGlue and the
// $P2PKIT_WASM_* constants), then the adapter (retains the factory through
// $webrtcInit__deps and contributes the webrtc* C shims).
function loadMergedLibrary() {
  global.mergeInto = (target, lib) => Object.assign(target, lib);
  global.LibraryManager = { library: {} };
  delete require.cache[require.resolve(SDK_GLUE)];
  delete require.cache[require.resolve(ADAPTER)];
  const nodeExports = require(SDK_GLUE);
  require(ADAPTER);
  return { lib: global.LibraryManager.library, nodeExports };
}

// Seeds with the C-facing wrappers the wasm module imports (declared by the
// SDK header aliased at include/Network/WebRtcTransport.h) and walks __deps
// recursively, like Emscripten.
function computeRetainedSymbols(lib) {
  const retained = new Set();
  const queue = Object.keys(lib).filter((key) => !key.startsWith('$') && !key.includes('__'));
  while (queue.length > 0) {
    const key = queue.shift();
    if (retained.has(key)) continue;
    retained.add(key);
    for (const dep of lib[`${key}__deps`] || []) queue.push(dep);
  }
  return retained;
}

// Emits retained $ symbols the way Emscripten does: functions keep their
// source, '='-verbatim strings become `var NAME = <verbatim>;`.
function emitRetainedLibrary(lib, retained, { drop } = {}) {
  const declarations = [];
  const functions = [];
  const declared = new Set();
  for (const key of [...retained].sort()) {
    const name = key.replace(/^\$/, '');
    const value = lib[key];
    if (typeof value !== 'function') {
      assert.equal(typeof value, 'string', `retained symbol ${key} must be a function or '='-verbatim string`);
      assert.ok(value.startsWith('='), `retained symbol ${key} must use '=' verbatim emission`);
      if (name === drop) continue;
      declarations.push(`var ${name} = ${value.slice(1)};`);
      declared.add(name);
    } else {
      functions.push(`var ${name} = ${value.toString()};`);
    }
  }
  return { declarationCode: declarations.join('\n'), functionCode: functions.join('\n'), declared };
}

function referencedConstants(code) {
  return new Set(code.match(/\bP2PKIT_WASM_[A-Z0-9_]+\b/g) || []);
}

function makeRuntimeSandbox() {
  return {
    Module: { print() {} },
    HEAPU8: { set() {}, slice() { return new Uint8Array(0); } },
    _malloc: () => 0,
    _free: () => {},
    _webrtcOnEvent: () => {},
    UTF8ToString: () => 'ABCD',
    stringToUTF8: () => {},
    RTCPeerConnection: class {},
    WebSocket: class { static OPEN = 1; },
    queueMicrotask: (fn) => Promise.resolve().then(fn),
    setInterval: () => 0,
    clearInterval: () => {},
  };
}

test('Emscripten library wrappers init without ReferenceError', () => {
  const { lib } = loadMergedLibrary();

  global.Module = { print: () => {} };
  global.HEAPU8 = {
    set() {},
    slice(start, end) {
      return new Uint8Array(end - start);
    },
  };
  global._malloc = (n) => 1024;
  global._free = () => {};
  global._webrtcOnEvent = () => {};
  global.RTCPeerConnection = class MockPC {};
  global.WebSocket = class MockWS {
    static OPEN = 1;
    static CONNECTING = 0;
    static CLOSED = 3;
    constructor() {
      this.readyState = MockWS.OPEN;
      this.onopen = null;
      queueMicrotask(() => this.onopen?.({}));
    }
    send() {}
    close() {}
  };

  assert.equal(typeof lib.$createP2pkitWasmGlue, 'function', 'SDK glue must contribute the factory');
  assert.equal(typeof lib.$webrtcInit, 'function', 'adapter must contribute $webrtcInit');
  assert.equal(typeof lib.webrtcFindMatch, 'function');
  assert.equal(typeof lib.webrtcCancelMatch, 'function');
  assert.equal(typeof lib.webrtcGetState, 'function');

  // Emscripten emits $-prefixed library keys as unprefixed runtime identifiers.
  hoistEmscriptenLibraryHelpers(lib, global);

  assert.doesNotThrow(() => lib.webrtcFindMatch());
  assert.equal(lib.webrtcGetState(), 1, 'findMatch leaves transport connecting');
  assert.doesNotThrow(() => lib.webrtcCancelMatch());
  assert.equal(lib.webrtcGetState(), 0, 'cancel returns the transport to idle');
  assert.doesNotThrow(() => lib.webrtcDisconnect());
});

test('$-prefixed runtime helper calls throw ReferenceError (browser regression)', () => {
  const { lib } = loadMergedLibrary();
  hoistEmscriptenLibraryHelpers(lib, global);

  const buggyWrapper = new Function('$webrtcInit()');
  assert.throws(
    () => buggyWrapper(),
    (err) => err instanceof ReferenceError && /\$webrtcInit/.test(err.message),
    'literal $webrtcInit() in emitted JS must fail at runtime',
  );

  assert.doesNotThrow(() => webrtcInit());
});

// ---- Emscripten emission model for the $P2PKIT_WASM_* constants -------------
//
// Emscripten (pinned by tools/web/emsdk-version.txt) emits a $NAME library
// item whose value is a string starting with '=' as `var NAME = <verbatim>;`
// in dunecity.js, and __deps retains $ items recursively. Only library object
// members reach the emitted file: the top-level `const P2PKIT_WASM_*`
// declarations in the SDK glue never do, so a constant that is not declared
// as an '='-verbatim $ item retained through __deps becomes a free identifier
// in the browser. The tests below model that retention/emission and fail when
// a constant referenced by retained runtime code is missing.

// Wire contract shared by the SDK's Node consts and its emitted library constants.
const EXPECTED_WIRE_CONSTANTS = {
  P2PKIT_WASM_CONTROL_LABEL: 'control',
  P2PKIT_WASM_COMMANDS_LABEL: 'commands',
  P2PKIT_WASM_CONTROL_OPTIONS: { ordered: true },
  P2PKIT_WASM_COMMANDS_OPTIONS: { ordered: false, maxRetransmits: 0 },
  P2PKIT_WASM_CONTROL_MODE: 'queued',
  P2PKIT_WASM_COMMANDS_MODE: 'drop',
  P2PKIT_WASM_CONTROL_HIGH_WATER: 512 * 1024,
  P2PKIT_WASM_CONTROL_LOW_WATER: 128 * 1024,
  P2PKIT_WASM_COMMANDS_HIGH_WATER: 512 * 1024,
  P2PKIT_WASM_COMMANDS_LOW_WATER: 0,
  P2PKIT_WASM_MAX_SIGNAL_BYTES: 256 * 1024,
  P2PKIT_WASM_EVENT_CONNECT: 0,
  P2PKIT_WASM_EVENT_DISCONNECT: 1,
  P2PKIT_WASM_EVENT_MESSAGE: 2,
  P2PKIT_WASM_EVENT_STATE: 3,
  P2PKIT_WASM_EVENT_MATCHED: 4,
  P2PKIT_WASM_STATE_IDLE: 0,
  P2PKIT_WASM_STATE_CONNECTING: 1,
  P2PKIT_WASM_STATE_CONNECTED: 2,
  P2PKIT_WASM_STATE_FAILED: 3,
};

// p2pkit-owned helpers the factory needs retained next to it.
const EXPECTED_FACTORY_HELPERS = ['resolveP2pkit', 'resolveSignalingUrl'];

test('SDK glue declares every P2PKIT_WASM_* constant as =verbatim matching the Node consts', () => {
  const { lib, nodeExports } = loadMergedLibrary();
  for (const [name, expected] of Object.entries(EXPECTED_WIRE_CONSTANTS)) {
    const item = lib[`$${name}`];
    assert.equal(typeof item, 'string', `$${name} must be an '='-verbatim library item, not ${typeof item}`);
    assert.ok(item.startsWith('='), `$${name} must start with '=' so it emits var ${name} = <verbatim>;`);
    // Evaluate in the host realm (new Function) so deepEqual sees host prototypes.
    const emittedValue = new Function(`return (${item.slice(1)});`)();
    assert.deepEqual(emittedValue, expected, `$${name} verbatim must match the wire contract`);
    if (name in nodeExports) {
      assert.deepEqual(nodeExports[name], expected, `Node export ${name} must match the wire contract`);
    }
  }
  // No stray library constants beyond the contract.
  const libConstantKeys = Object.keys(lib).filter((key) => /^\$P2PKIT_WASM_/.test(key)).sort();
  assert.deepEqual(
    libConstantKeys,
    Object.keys(EXPECTED_WIRE_CONSTANTS).map((name) => `$${name}`).sort(),
  );
});

test('emitted symbol retention covers every P2PKIT_WASM_* reference and fails on a missing constant', () => {
  const { lib } = loadMergedLibrary();
  const retained = computeRetainedSymbols(lib);
  assert.ok(retained.has('$createP2pkitWasmGlue'), 'factory must be retained through the adapter __deps chain');
  assert.ok(retained.has('$webrtcInit'), '$webrtcInit must be retained through __deps');

  // The SDK factory's deps list is the retention guarantee: wire constants
  // plus its own helpers.
  const expectedDeps = [
    ...Object.keys(EXPECTED_WIRE_CONSTANTS).map((name) => `$${name}`),
    ...EXPECTED_FACTORY_HELPERS.map((name) => `$${name}`),
  ].sort();
  assert.deepEqual([...lib.$createP2pkitWasmGlue__deps].sort(), expectedDeps);

  const emitted = emitRetainedLibrary(lib, retained);
  const referenced = referencedConstants(emitted.functionCode);
  assert.ok(referenced.size > 0, 'model must observe constant references in retained code');
  const missing = [...referenced].filter((name) => !emitted.declared.has(name));
  assert.deepEqual(missing, [], 'retained runtime code references constants that are not emitted');

  // Regression guard: dropping any referenced constant must surface as an
  // unresolved reference, i.e. this model fails when a constant goes missing.
  for (const name of referenced) {
    const mutated = emitRetainedLibrary(lib, retained, { drop: name });
    const stillMissing = [...referencedConstants(mutated.functionCode)].filter((n) => !mutated.declared.has(n));
    assert.ok(stillMissing.includes(name), `dropping ${name} must leave an unresolved reference`);
  }
});

test('emitted library code runs in a bare vm context and throws ReferenceError when a constant is dropped', () => {
  const { lib } = loadMergedLibrary();
  const retained = computeRetainedSymbols(lib);
  const emitted = emitRetainedLibrary(lib, retained);

  const sandbox = makeRuntimeSandbox();
  vm.runInNewContext(`${emitted.declarationCode}\n${emitted.functionCode}`, sandbox);
  assert.equal(typeof sandbox.createP2pkitWasmGlue, 'function');
  assert.doesNotThrow(() => sandbox.webrtcInit());
  assert.ok(sandbox.Module.__dunecityWebrtc, 'webrtcInit must instantiate the factory');
  assert.equal(sandbox.webrtcGetState(), 0, 'state is idle before hosting/joining');

  const dropped = emitRetainedLibrary(lib, retained, { drop: 'P2PKIT_WASM_CONTROL_LABEL' });
  const bare = makeRuntimeSandbox();
  vm.runInNewContext(`${dropped.declarationCode}\n${dropped.functionCode}`, bare);
  // Errors thrown inside a vm context are not host-realm instances; match by name.
  assert.throws(
    () => bare.webrtcInit(),
    (err) => err.name === 'ReferenceError' && err.message.includes('P2PKIT_WASM_CONTROL_LABEL'),
    'missing constant must fail at runtime',
  );
});
