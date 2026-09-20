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
npm ci --prefix platform/web
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
(`git+https://github.com/QuixThe2nd/p2pkit.git#<exact-commit>`; bumping the pin is a deliberate
upgrade). `tools/web/build-emscripten.sh` prepends the bundle to `dunecity.js`
so the runtime resolves it without a separate script tag; the installed SDK headers and glue are also required at wasm build time.
After dependencies are installed, a build can run without network access.

After bumping the p2pkit pin, regenerate and re-commit the bundle:

```bash
npm ci --prefix platform/web
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

## Matchmaking service (required for Find Match)

This is a separate WebSocket service from the PHP direct-play room directory.
Building or merging the game does not deploy it. Deploy the
[`bootstrapping-server`](https://github.com/QuixThe2nd/p2pkit/tree/257f3c8eb0c0cf373298225e54c8d09d2f896a42/bootstrapping-server)
from the same immutable revision as `package.json`. Install its dependencies
and run `node server.js`; it listens on `127.0.0.1:8788` by default.
For local testing, `node platform/web/test/fixtures/bootstrapping-server.js`
runs an exact copy of that entry point using the installed `ws` dependency.

In production, terminate TLS at the web proxy and forward WebSocket upgrades
to that loopback service. Preserve the external Host and Origin headers: the
server accepts same-host browser origins (and localhost for development).
The default SDK endpoint is `wss://<page host>` (or `ws://` on HTTP pages).
To use a dedicated proxy path, load this configuration before `dunecity.js`:

```js
window.DUNECITY_WEBRTC_CONFIG = {
  signaling: 'wss://your-game.example/matchmaking',
  iceServers: [{ urls: 'stun:your-stun.example:3478' }]
};
```

The service pairs the next two finders globally via `find`, `cancel`, and `sig`
frames. Run one shared process for this queue; it has no authentication or
persistence, and its configured queue cap defaults to 200. Restarting it loses
waiting/pairing state. It sees both peers' SDP and ICE candidates and must be
trusted: this flow does not pin peer fingerprints through the direct-room
admission protocol, so a malicious signaling service can substitute peers.

Without `iceServers`, the SDK contacts Google and Twilio STUN servers. An empty
array uses only host candidates. There is no default TURN relay, so some NAT
pairs cannot connect; operators may explicitly supply their own ICE configuration.
This matchmaking configuration does not change the room flow's STUN-only policy.

The fixture records its source revision and SHA-256 in a test. Verify upstream
bytes with `node tools/web/sync-matchmaking-fixture.mjs --check`; after a pin
upgrade, run it without `--check`, review the diff, and update the recorded hash.

Browser peer lifetime regressions run under wasm32 AddressSanitizer with
`tests/wasm/run-webrtc-peer-lifecycle.sh` after sourcing the pinned emsdk.

For an optional two-browser acceptance run, install Playwright and Chrome, then
run `node tests/web/matchmaking-smoke.mjs` from the repository root. Set
`PLAYWRIGHT_MODULE` to a Playwright module path if it is installed elsewhere,
and `BROWSER_CHANNEL` to select a different installed Chromium channel. The
fixture uses isolated profiles, loopback signaling and host-only ICE. It saves
screenshots and packet counters under `build/matchmaking-smoke/`, including a
check that quitting a match cannot schedule a multi-minute browser sleep.
