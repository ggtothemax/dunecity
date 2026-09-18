# DuneCity browser (Emscripten) build

The browser build script is `tools/web/build-emscripten.sh`. It builds the full
`dunecity` game target using the existing production shell and persistence code.
This directory also holds the DuneCity side of the WebRTC JavaScript bridge
(`dunecity_webrtc_config.js`) used by the browser multiplayer transport; the
generic transport glue comes from the `p2pkit` npm dependency pinned in
`package.json`.

## Prerequisites

- git
- cmake 3.21+
- python3
- node + npm (install the `p2pkit` dependency before building; see below)
- a C++ compiler for the host (used by emsdk)

After a clean checkout, install the pinned p2pkit SDK once — the C++ packet
streams and the WebRTC transport include its headers, so both native and
browser builds need it:

```bash
cd platform/web && npm install
```

## Reproducible build

From a clean checkout:

```bash
./tools/web/build-emscripten.sh
```

The script installs the Emscripten compiler version in
`tools/web/emsdk-version.txt` using the immutable installer revision in
`tools/web/emsdk-revision.txt`, into `.emsdk/` (override with `EMSDK_DIR`).
An existing SDK must have the expected origin, revision and clean tracked files.
Use a fresh SDK directory instead of replacing a different local installation.
The compiler version matches the production browser build (4.0.14).

`BUILD_DIR` overrides the output directory. Existing outputs are preserved for
incremental builds; the script never recursively deletes the supplied directory.
Source-root, home and source-ancestor destinations are refused before SDK setup.

### Output path

```
build/emscripten/bin/
  dunecity.html
  dunecity.js
  dunecity.wasm
  dunecity.data    # preloaded PAK/config/mods/sprites
  shell.js
  shell.css
```

### WebRTC glue

Two `--js-library` files are linked into the Emscripten output via
`src/CMakeLists.txt`:

- `node_modules/p2pkit/emscripten/js/p2pkit_webrtc_glue.cjs` — the installed
  p2pkit SDK's game-neutral glue (`$createP2pkitWasmGlue` plus the
  `$P2PKIT_WASM_*` wire constants).
- `platform/web/dunecity_webrtc_config.js` — DuneCity's adapter: the
  `DUNECITY_WEBRTC_CONFIG` page settings, the C-export shims and the
  `_webrtcOnEvent` pump into wasm memory.

The C++ side reaches them through `include/Network/WebRtcTransport.h`, an
alias of the SDK's header-only `p2pkit_wasm::WebRtcTransport`, and calls the
exported `webrtcFindMatch`, `webrtcCancelMatch`, `webrtcSendTo`, etc.

Run the glue unit tests (Node, no browser):

```bash
cd platform/web && npm test
```

### p2pkit bundle

The SDK glue resolves the p2pkit runtime through the committed IIFE bundle at
`platform/web/dist/p2pkit.iife.js`, which exposes `globalThis.P2PKIT_IIFE`
(`RTCTransport` with its raw multi-channel mode, `DEFAULT_ICE_SERVERS`,
`Emitter`, `randomId`, plus the negotiate/sdp helpers). The bundle is a
verbatim copy of the pinned package's own build output: `npm install` in
`platform/web` runs the package's `prepare` script, which builds
`node_modules/p2pkit/dist/p2pkit.iife.js` itself; the package version is
pinned to an exact commit in `platform/web/package.json`
(`github:QuixThe2nd/p2pkit#<exact-commit>`; bumping the pin is a deliberate
upgrade). `tools/web/build-emscripten.sh` prepends the bundle to `dunecity.js`
so the runtime resolves it without a separate script tag; because the bundle
is committed, no npm/network access is needed at wasm build time.

After bumping the p2pkit pin, regenerate and re-commit the bundle:

```bash
cd platform/web && npm install
node tools/web/build-p2pkit-iife.mjs     # or: cd platform/web && npm run build:iife
```

`platform/web/test/p2pkit-iife.test.cjs` enforces the export surface in CI and
checks that the committed bytes match the installed dependency's build.

## Local smoke test

```bash
cd build/emscripten/bin
python3 -m http.server 8080
# open http://127.0.0.1:8080/dunecity.html
```

You still need original Dune 2 PAK files in `data/` at build time; they are
embedded into `dunecity.data` by `--preload-file`.

## CI

GitHub Actions job `build-emscripten` in `.github/workflows/build.yml` runs the
same `./tools/web/build-emscripten.sh` command.

The verifier checks artifact presence and obvious pthread dependencies; it is
not a security audit or a multiplayer test. Browser builds keep the existing
HTTP implementation, which already separates Emscripten from native libcurl.
The build foundation does not change gameplay routing: the WebRTC handshake
runs through the pinned p2pkit npm dependency while game packets stay on native binary
data channels.
