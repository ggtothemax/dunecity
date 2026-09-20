/*
 * DuneCity adapter for the p2pkit Emscripten SDK (the "p2pkit" npm dependency
 * pinned in platform/web/package.json; its glue is required here under Node
 * and linked as a sibling --js-library in Emscripten builds).
 *
 * The SDK glue is game-neutral: it publishes $createP2pkitWasmGlue and the
 * $P2PKIT_WASM_* wire constants as Emscripten library symbols and knows
 * nothing about DuneCity. This file supplies everything DuneCity-specific:
 *
 *   - the page-provided DUNECITY_WEBRTC_CONFIG { signaling, iceServers },
 *   - the C export shims (webrtcFindMatch, webrtcSendTo, ...) that the SDK's
 *     header-only transport (emscripten/include/p2pkit-wasm/webrtc_transport.h,
 *     aliased by include/Network/WebRtcTransport.h) declares, including the
 *     _webrtcOnEvent event pump into wasm memory.
 *
 * It is linked next to the SDK glue:
 *   --js-library node_modules/p2pkit/emscripten/js/p2pkit_webrtc_glue.cjs
 *   --js-library platform/web/dunecity_webrtc_config.js
 * Cross-library $ deps work because Emscripten merges every --js-library into
 * one LibraryManager before linking.
 *
 * Under Node (unit tests) this module exports createDunecityWebRtc(), which
 * wraps the SDK factory with the same DuneCity defaults and resolves the
 * p2pkit namespace from the committed IIFE bundle (platform/web/dist/).
 */

// Under Node the SDK glue is the installed p2pkit package's CommonJS export.
// Under Emscripten this file is evaluated in the builder sandbox, where
// require does not exist; the wiring block below then reaches the factory
// through the retained library symbol instead.
const sdkGlue = (typeof require === 'function' && typeof module !== 'undefined' && module.exports)
    ? require('p2pkit/emscripten/glue')
    : null;

// DuneCity page-side WebRTC settings (web/shell.js may define
// window.DUNECITY_WEBRTC_CONFIG = { signaling, iceServers }). Returned value
// has undefined members when the page provided none; the SDK then applies its
// own defaults (page-host lobby, p2pkit.DEFAULT_ICE_SERVERS).
function dunecityWebrtcConfig() {
    const page = (typeof DUNECITY_WEBRTC_CONFIG !== 'undefined') ? DUNECITY_WEBRTC_CONFIG : null;
    return {
        signaling: (page && page.signaling) || undefined,
        iceServers: (page && page.iceServers) || undefined,
    };
}

// Node-only fallback: evaluate the committed p2pkit IIFE bundle in a fresh vm
// context and return the P2PKIT_IIFE namespace it exposes. The browser runtime
// gets the same namespace because build-emscripten.sh prepends the bundle to
// dunecity.js, so this path never runs there.
function loadP2pkitBundleForNode() {
    if (typeof require !== 'function') return null;
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');
    const bundlePath = path.join(__dirname, 'dist', 'p2pkit.iife.js');
    let source;
    try {
        source = fs.readFileSync(bundlePath, 'utf8');
    } catch (err) {
        return null;
    }
    // The bundle expects the standard web-platform globals a browser page
    // provides (RTCTransport schedules its connect deadline with setTimeout,
    // randomId uses crypto, ...). Node keeps them on the host context, so
    // hand them through; the vm context itself starts empty.
    const sandbox = {
        console: console,
        setTimeout: setTimeout,
        clearTimeout: clearTimeout,
        setInterval: setInterval,
        clearInterval: clearInterval,
        queueMicrotask: queueMicrotask,
        performance: (typeof performance !== 'undefined') ? performance : null,
        TextEncoder: (typeof TextEncoder !== 'undefined') ? TextEncoder : undefined,
        TextDecoder: (typeof TextDecoder !== 'undefined') ? TextDecoder : undefined,
        crypto: (typeof crypto !== 'undefined') ? crypto : undefined,
    };
    vm.createContext(sandbox);
    vm.runInContext(source, sandbox, { filename: bundlePath });
    return sandbox.P2PKIT_IIFE || (typeof globalThis !== 'undefined' ? globalThis.P2PKIT_IIFE : null) || null;
}

// explicit override (config.p2pkit) -> globalThis.P2PKIT_IIFE -> committed bundle.
function resolveDunecityP2pkit(overrides) {
    if (overrides && overrides.p2pkit) return overrides.p2pkit;
    if (typeof globalThis !== 'undefined' && globalThis.P2PKIT_IIFE) return globalThis.P2PKIT_IIFE;
    return loadP2pkitBundleForNode();
}

// Node-facing factory used by platform/web/test/webrtc-glue.test.cjs: the SDK
// factory with DuneCity defaults, overridable key by key (tests inject mock
// RTCPeerConnection/WebSocket/p2pkit). The p2pkit lobby is a single global
// FIFO (no per-game name), so there is no gameName key to set.
function createDunecityWebRtc(overrides) {
    if (!sdkGlue) throw new Error('dunecity_webrtc_config.js: SDK glue not loadable in this environment');
    const page = dunecityWebrtcConfig();
    const config = Object.assign({
        signaling: page.signaling,
        iceServers: page.iceServers,
        disconnectCause: 2, // NETWORKDISCONNECT_TIMEOUT: the remote connection vanished
        p2pkit: null, // resolved below, after overrides are merged
        RTCPeerConnection: (typeof RTCPeerConnection !== 'undefined') ? RTCPeerConnection
            : ((typeof window !== 'undefined' && window.RTCPeerConnection) ? window.RTCPeerConnection : undefined),
        WebSocket: (typeof WebSocket !== 'undefined') ? WebSocket : undefined,
        log: function (msg) {
            if (typeof console !== 'undefined' && console.log) console.log('[webrtc] ' + msg);
        },
    }, overrides || {});
    if (!config.p2pkit) config.p2pkit = resolveDunecityP2pkit(config);
    return sdkGlue.createP2pkitWasmGlue(config);
}

// Node export (unit tests); the Emscripten wiring block is below.
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        createDunecityWebRtc,
        resolveDunecityP2pkit,
        dunecityWebrtcConfig,
    };
}

/*
 * Emscripten --js-library wiring. Linked (with the SDK glue) into dunecity.js
 * via the two --js-library lines in src/CMakeLists.txt. When this file is
 * loaded under Node (unit tests), mergeInto/LibraryManager do not exist and
 * this block is skipped.
 */
if (typeof mergeInto === 'function' && typeof LibraryManager !== 'undefined') {
    mergeInto(LibraryManager.library, {
        // $createP2pkitWasmGlue lives in the installed p2pkit SDK's glue
        // (emscripten/js/p2pkit_webrtc_glue.cjs); naming it here retains it
        // (and, through its own __deps, the whole $P2PKIT_WASM_* constant
        // set) in the emitted runtime.
        $webrtcInit__deps: ['$createP2pkitWasmGlue'],
        $webrtcInit: function () {
            if (Module.__dunecityWebrtc) return;
            Module.__dunecityWebrtc = createP2pkitWasmGlue({
                disconnectCause: 2, // NETWORKDISCONNECT_TIMEOUT
                signaling: (typeof DUNECITY_WEBRTC_CONFIG !== 'undefined' && DUNECITY_WEBRTC_CONFIG && DUNECITY_WEBRTC_CONFIG.signaling) || undefined,
                iceServers: (typeof DUNECITY_WEBRTC_CONFIG !== 'undefined' && DUNECITY_WEBRTC_CONFIG && DUNECITY_WEBRTC_CONFIG.iceServers) || undefined,
                p2pkit: (typeof globalThis !== 'undefined') ? globalThis.P2PKIT_IIFE : undefined,
                RTCPeerConnection: (typeof RTCPeerConnection !== 'undefined') ? RTCPeerConnection : window.RTCPeerConnection,
                WebSocket: WebSocket,
                // Emscripten only READS Module.print (runtime: "if(Module['print'])out=...")
                // and never defines it, and web/shell.js does not either — so calling
                // Module.print unconditionally threw TypeError on the glue's FIRST log
                // line, which aborted ws.onopen before the matchmaking {"t":"find"} was
                // sent: both peers ended up connected to the lobby (signalingState
                // 'open', verified on the wire) yet never paired. Honor a page-provided
                // Module.print (the runtime contract for capturing stdout) and fall
                // back to console.log, like the Node factory in this file.
                log: function (msg) {
                    const emit = (typeof Module !== 'undefined' && typeof Module.print === 'function')
                        ? Module.print : ((typeof console !== 'undefined' && console.log) || function () {});
                    emit('[' + msg + ']');
                },
                onEvent: function (type, peer, channel, cause, bytes) {
                    if (type === 2 /* MESSAGE */ && bytes) {
                        const ptr = _malloc(bytes.length);
                        if (!ptr) return;
                        HEAPU8.set(bytes, ptr);
                        _webrtcOnEvent(type, peer, channel, cause, ptr, bytes.length);
                        _free(ptr);
                    } else {
                        _webrtcOnEvent(type, peer, channel, cause, 0, 0);
                    }
                },
            });
            Module.dunecityWebrtcStats = Module.__dunecityWebrtc.getStats;
        },

        webrtcFindMatch__deps: ['$webrtcInit'],
        webrtcFindMatch: function () {
            webrtcInit();
            return Module.__dunecityWebrtc.findMatch() ? 1 : 0;
        },

        webrtcCancelMatch__deps: ['$webrtcInit'],
        webrtcCancelMatch: function () {
            webrtcInit();
            return Module.__dunecityWebrtc.cancelMatchmaking() ? 1 : 0;
        },

        webrtcSendTo: function (peer, channel, ptr, len) {
            if (!Module.__dunecityWebrtc) return 0;
            const bytes = HEAPU8.slice(ptr, ptr + len);
            return Module.__dunecityWebrtc.send(channel, bytes) ? 1 : 0;
        },

        webrtcGetState: function () {
            if (!Module.__dunecityWebrtc) return 0; /* IDLE */
            const s = Module.__dunecityWebrtc.getStats();
            // The SDK's stats table keeps channels[].state at 'new' (diagnostic
            // only); RTCTransport's 'connect' event — "all data channels open" —
            // is what flips peerConnectionState to 'connected', so that is the
            // Connected (=2) authority the C++ State contract needs.
            if (s.peerConnectionState === 'connected') return 2; /* CONNECTED */
            if (s.role) return 1; /* CONNECTING */
            return 0;
        },

        webrtcGetRttMs: function () {
            return (Module.__dunecityWebrtc && Module.__dunecityWebrtc.getRttMs()) | 0;
        },

        webrtcDisconnect: function () {
            if (Module.__dunecityWebrtc) Module.__dunecityWebrtc.disconnect();
        },
    });
}
