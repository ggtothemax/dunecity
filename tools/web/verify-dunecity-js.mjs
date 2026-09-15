#!/usr/bin/env node
/**
 * Post-build verification for dunecity.js WebRTC glue wiring.
 * Fails on DCE of createP2pkitWasmGlue or literal $-prefixed helper calls at runtime.
 *
 * The wiring now spans two --js-library files: the vendored p2pkit-wasm SDK
 * (platform/web/p2pkit-wasm-glue.js, $createP2pkitWasmGlue + $P2PKIT_WASM_*
 * constants) and the DuneCity adapter (platform/web/dunecity_webrtc_config.js,
 * $webrtcInit + the webrtc* C shims). Both are checked.
 *
 * Usage:
 *   node tools/web/verify-dunecity-js.mjs path/to/dunecity.js
 *   node tools/web/verify-dunecity-js.mjs --source platform/web/dunecity_webrtc_config.js
 */
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const args = process.argv.slice(2);
if (args.length !== 2 || !['--built', '--source'].includes(args[0])) {
  console.error('usage: verify-dunecity-js.mjs --built|--source <path>');
  process.exit(2);
}

const mode = args[0];
const filePath = path.resolve(args[1]);
const text = fs.readFileSync(filePath, 'utf8');
const sdkGluePath = path.join(path.dirname(filePath), 'p2pkit-wasm-glue.js');

function fail(msg) {
  console.error(`ERROR: ${msg}`);
  process.exit(1);
}

function hoistEmscriptenLibraryHelpers(lib, target) {
  for (const [key, value] of Object.entries(lib)) {
    if (key.startsWith('$')) {
      target[key.slice(1)] = value;
    }
  }
}

if (mode === '--source') {
  if (path.basename(filePath) !== 'dunecity_webrtc_config.js') {
    fail('--source expects platform/web/dunecity_webrtc_config.js (the adapter that owns the C shims)');
  }
  if (!fs.existsSync(sdkGluePath)) {
    fail('platform/web/p2pkit-wasm-glue.js (vendored SDK glue) not found next to the adapter; run tools/web/fetch-p2pkit-wasm.mjs');
  }
  const sdkText = fs.readFileSync(sdkGluePath, 'utf8');

  // SDK glue: the factory must be a retained library symbol so it survives DCE.
  if (!sdkText.includes('$createP2pkitWasmGlue: createP2pkitWasmGlue')) {
    fail('p2pkit-wasm-glue.js must export $createP2pkitWasmGlue to survive Emscripten DCE');
  }
  if (!sdkText.includes('$createP2pkitWasmGlue__deps')) {
    fail('p2pkit-wasm-glue.js must declare $createP2pkitWasmGlue__deps (retains the $P2PKIT_WASM_* constants)');
  }

  // Adapter: retain the SDK factory through __deps and emit unprefixed calls only.
  if (!text.includes('$webrtcInit__deps')) {
    fail('dunecity_webrtc_config.js must declare $webrtcInit__deps');
  }
  if (!text.includes("'$createP2pkitWasmGlue'")) {
    fail("dunecity_webrtc_config.js $webrtcInit__deps must retain $createP2pkitWasmGlue (cross-library dep)");
  }
  if (!text.includes('webrtcFindMatch__deps')) {
    fail('dunecity_webrtc_config.js must declare webrtcFindMatch__deps');
  }
  if (!text.includes('webrtcCancelMatch__deps')) {
    fail('dunecity_webrtc_config.js must declare webrtcCancelMatch__deps');
  }
  if (text.includes('$webrtcInit__postset')) {
    fail('dunecity_webrtc_config.js must not use $webrtcInit__postset; $ keys emit unprefixed runtime ids');
  }
  if (/\$webrtcInit\s*\(/.test(text)) {
    fail('dunecity_webrtc_config.js must call webrtcInit(), not literal $webrtcInit() at runtime');
  }
  if (/\$createP2pkitWasmGlue\s*\(/.test(text)) {
    fail('dunecity_webrtc_config.js must call createP2pkitWasmGlue(...), not literal $createP2pkitWasmGlue(...) at runtime');
  }
  if (!/\bcreateP2pkitWasmGlue\s*\(/.test(text)) {
    fail('dunecity_webrtc_config.js must call createP2pkitWasmGlue(...) inside $webrtcInit');
  }
  if (!/\bwebrtcInit\s*\(/.test(text)) {
    fail('dunecity_webrtc_config.js must call webrtcInit() from exported wrappers');
  }
  console.log(`OK: source WebRTC library wiring in ${filePath} (+ ${path.basename(sdkGluePath)})`);
  process.exit(0);
}

// --built: validate emitted dunecity.js
if (
  !/function\s+createP2pkitWasmGlue\s*\(/.test(text) &&
  !/var\s+createP2pkitWasmGlue\s*=/.test(text) &&
  !/createP2pkitWasmGlue\s*=\s*function/.test(text)
) {
  fail('dunecity.js missing createP2pkitWasmGlue factory (DCE or SDK glue not linked)');
}

if (!/function _webrtcFindMatch\(\)\{webrtcInit\(\)/.test(text)) {
  fail('dunecity.js _webrtcFindMatch must call webrtcInit() (Emscripten $ key emits unprefixed id)');
}
if (/function _webrtcFindMatch\(\)\{\$webrtcInit\(\)/.test(text)) {
  fail('dunecity.js _webrtcFindMatch calls literal $webrtcInit() (ReferenceError in browser)');
}
if (/\$createP2pkitWasmGlue\s*\(/.test(text)) {
  fail('dunecity.js must not reference literal $createP2pkitWasmGlue(...) at runtime');
}
if (/\$webrtcInit\s*\(/.test(text)) {
  fail('dunecity.js must not reference literal $webrtcInit() at runtime');
}

const exportNames = [
  '_webrtcFindMatch',
  '_webrtcCancelMatch',
  '_webrtcSendTo',
  '_webrtcGetState',
  '_webrtcGetRttMs',
  '_webrtcDisconnect',
  '_webrtcOnEvent',
];
for (const name of exportNames) {
  if (!text.includes(name)) {
    fail(`dunecity.js missing exported symbol ${name}`);
  }
}

if (/dunecity\.worker\.js|ENVIRONMENT_IS_PTHREAD=true|USE_PTHREADS/.test(text)) {
  fail('dunecity.js appears to require pthread worker (expected single-threaded build)');
}

// Runtime smoke: re-load BOTH library files with mocked Emscripten runtime,
// merged into one LibraryManager exactly like the real link (validates the
// cross-file $createP2pkitWasmGlue dep).
globalThis.mergeInto = (target, lib) => Object.assign(target, lib);
globalThis.LibraryManager = { library: {} };
globalThis.Module = { print: () => {} };
globalThis.HEAPU8 = { set() {}, slice(_s, _e) { return new Uint8Array(0); } };
globalThis._malloc = () => 0;
globalThis._free = () => {};
globalThis._webrtcOnEvent = () => {};
globalThis.UTF8ToString = () => '';
globalThis.stringToUTF8 = () => {};
globalThis.RTCPeerConnection = class {};
globalThis.WebSocket = class { static OPEN = 1; };

const repoRoot = path.resolve(process.cwd());
const libFiles = [
  path.join(repoRoot, 'platform/web/p2pkit-wasm-glue.js'),
  path.join(repoRoot, 'platform/web/dunecity_webrtc_config.js'),
];
for (const libFile of libFiles) {
  if (!fs.existsSync(libFile)) {
    fail(`cannot locate ${libFile} for runtime wrapper smoke test`);
  }
  await import(pathToFileURL(libFile).href);
}

const lib = globalThis.LibraryManager.library;
if (typeof lib.$createP2pkitWasmGlue !== 'function') {
  fail('LibraryManager.library missing $createP2pkitWasmGlue after loading SDK glue');
}
if (typeof lib.$webrtcInit !== 'function') {
  fail('LibraryManager.library missing $webrtcInit after loading adapter');
}
if (typeof lib.webrtcFindMatch !== 'function') {
  fail('LibraryManager.library missing webrtcFindMatch wrapper');
}
if (typeof lib.webrtcCancelMatch !== 'function') {
  fail('LibraryManager.library missing webrtcCancelMatch wrapper');
}

hoistEmscriptenLibraryHelpers(lib, globalThis);

try {
  lib.webrtcFindMatch();
} catch (err) {
  if (err instanceof ReferenceError) {
    fail(`webrtcFindMatch init path threw ReferenceError: ${err.message}`);
  }
  throw err;
}

console.log(`OK: built dunecity.js WebRTC glue checks passed for ${filePath}`);
