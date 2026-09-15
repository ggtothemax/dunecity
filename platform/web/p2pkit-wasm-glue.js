/*
 * p2pkit-wasm WebRTC bridge (browser side), game-generic.
 *
 * This file has two halves:
 *   1. A dependency-injected factory (`createP2pkitWasmGlue(config)`) that
 *      owns all the WebRTC/signaling logic. Every browser API it needs is
 *      injected, so the core can be unit-tested under Node with mocked
 *      RTCPeerConnection / WebSocket (see test/glue.test.cjs). The config
 *      object carries the game name, the channel labels/options, and the
 *      event-pump callback; the defaults reproduce the two-channel wire
 *      contract the SDK was extracted with:
 *        channel 0 ("control")  : RTCDataChannel { ordered: true }
 *        channel 1 ("commands") : RTCDataChannel { ordered: false, maxRetransmits: 0 }
 *   2. An Emscripten `--js-library` block that publishes the factory and its
 *      constants as `$`-prefixed library symbols so a consuming game's own
 *      js-library adapter can retain them through __deps and wire them to its
 *      exported C functions (see README.md for the adapter template).
 *
 * p2pkit integration: the SignallingChannel dialect comes from the committed
 * IIFE bundle at dist/p2pkit.iife.js (globalThis.P2PKIT_IIFE), built by
 * build/build-p2pkit-iife.mjs from the p2pkit pin in package.json. The page
 * must expose the bundle before the first findMatch() (dunecity prepends it
 * to its runtime). Dialect messages travel inside the signaling server's
 * v:1 "signal" envelope, and the match host always creates the offer (no
 * negotiate-module helper needed). Game packets still ride the native binary
 * RTCDataChannels created here — never p2pkit's RTCTransport JSON channel.
 *
 * Signaling: a global matchmaking lobby (the p2pkit bootstrapping server).
 * findMatch() sends {"t":"find","game":<gameName>} when a game name is
 * configured; the lobby pairs the next two finders FIFO and assigns roles —
 * the waiter hosts (creates the WebRTC offer), the newcomer joins (answers).
 * Dialect envelopes ride the lobby's opaque sig channel wrapped as
 * {"t":"sig","data":<envelope>}; there are no rooms, codes, or peer ids (the
 * glue addresses its peer synthetically as 'host'/'joiner').
 *
 * Wire contract (defaults; labels/options/water marks overridable via the
 * config object passed to createP2pkitWasmGlue):
 *   - channel 0 ("control")  : RTCDataChannel { ordered: true }            — reliable
 *   - channel 1 ("commands") : RTCDataChannel { ordered: false, maxRetransmits: 0 } — lossy
 *   - one application packet per DataChannel message; payload untouched.
 *
 * Backpressure (per channel, chosen by the channel's `mode`):
 *   - mode "queued" (default channel 0): if bufferedAmount >= high water mark,
 *     outgoing messages are queued in JS and flushed on `bufferedamountlow`.
 *   - mode "drop"   (default channel 1): if bufferedAmount >= high water mark,
 *     the send is DROPPED and reported as a failure; the game resends its
 *     recent lossy cycles.
 */

'use strict';

// Every P2PKIT_WASM_* constant is declared twice on purpose:
//   - here, as top-level const, so the Node unit tests (and module.exports)
//     see the real values;
//   - again below in the Emscripten mergeInto() block as `$NAME: '=...'`
//     verbatim-string library items, because Emscripten only emits library
//     object members into the built runtime — these top-level declarations
//     never reach the browser.
// test/library.test.cjs fails if the two halves drift apart or if a constant
// used by retained runtime code is missing.

const P2PKIT_WASM_CONTROL_LABEL = 'control';
const P2PKIT_WASM_COMMANDS_LABEL = 'commands';
const P2PKIT_WASM_CONTROL_OPTIONS = { ordered: true };
const P2PKIT_WASM_COMMANDS_OPTIONS = { ordered: false, maxRetransmits: 0 };
const P2PKIT_WASM_CONTROL_HIGH_WATER = 512 * 1024;
const P2PKIT_WASM_CONTROL_LOW_WATER = 128 * 1024;
const P2PKIT_WASM_COMMANDS_HIGH_WATER = 512 * 1024;
const P2PKIT_WASM_MAX_SIGNAL_BYTES = 256 * 1024;

// Event codes passed to the C++ side (must match webrtc_transport.h)
const P2PKIT_WASM_EVENT_CONNECT = 0;
const P2PKIT_WASM_EVENT_DISCONNECT = 1;
const P2PKIT_WASM_EVENT_MESSAGE = 2;
const P2PKIT_WASM_EVENT_STATE = 3;
const P2PKIT_WASM_EVENT_MATCHED = 4;

// Transport states (must match webrtc_transport.h)
const P2PKIT_WASM_STATE_IDLE = 0;
const P2PKIT_WASM_STATE_CONNECTING = 1;
const P2PKIT_WASM_STATE_CONNECTED = 2;
const P2PKIT_WASM_STATE_FAILED = 3;

// The default channel table: channel 0 is the reliable queued channel,
// channel 1 the lossy drop channel. Labels and options are overridable via
// config.channels; modes and water marks too.
const P2PKIT_WASM_DEFAULT_CHANNELS = [
    {
        label: P2PKIT_WASM_CONTROL_LABEL,
        options: P2PKIT_WASM_CONTROL_OPTIONS,
        mode: 'queued',
        highWater: P2PKIT_WASM_CONTROL_HIGH_WATER,
        lowWater: P2PKIT_WASM_CONTROL_LOW_WATER,
    },
    {
        label: P2PKIT_WASM_COMMANDS_LABEL,
        options: P2PKIT_WASM_COMMANDS_OPTIONS,
        mode: 'drop',
        highWater: P2PKIT_WASM_COMMANDS_HIGH_WATER,
        lowWater: 0,
    },
];

function resolveP2pkit(config) {
    if (config && config.p2pkit) return config.p2pkit;
    if (typeof globalThis !== 'undefined' && globalThis.P2PKIT_IIFE) {
        return globalThis.P2PKIT_IIFE;
    }
    return null;
}

function validateSignallingMessage(message) {
    if (!message || typeof message !== 'object' || Array.isArray(message)) return 'message must be an object';
    if (typeof message.from !== 'string' || message.from.length === 0) return 'missing from';
    if (message.announce === true) return null;
    if (message.description !== undefined) {
        if (typeof message.to !== 'string' || message.to.length === 0) return 'description requires to';
        if (typeof message.description !== 'object' || message.description === null || Array.isArray(message.description)) {
            return 'description must be an object';
        }
        return null;
    }
    if (message.iceCandidate !== undefined) {
        if (typeof message.to !== 'string' || message.to.length === 0) return 'iceCandidate requires to';
        if (typeof message.iceCandidate !== 'object' || message.iceCandidate === null || Array.isArray(message.iceCandidate)) {
            return 'iceCandidate must be an object';
        }
        return null;
    }
    return 'unknown signalling message shape';
}

function createSignallingChannel({ sendRaw, p2pkit, log, maxSignalBytes }) {
    const maxSize = maxSignalBytes || P2PKIT_WASM_MAX_SIGNAL_BYTES;
    let readyResolve;
    let readyDone = false;
    const ready = new Promise(function (resolve) {
        readyResolve = resolve;
    });
    const emitter = new p2pkit.Emitter();

    function send(message) {
        const err = validateSignallingMessage(message);
        if (err) {
            log('webrtc: invalid signalling send: ' + err);
            return false;
        }
        // sendRaw receives the p2pkit dialect envelope (from/to + payload).
        // The websocket glue wraps it as {"t":"sig","data":<dialect>} once.
        const text = JSON.stringify(message);
        if (text.length > maxSize) {
            log('webrtc: signal message too large');
            return false;
        }
        return sendRaw(text);
    }

    return {
        send: send,
        onMessage: function (handler) {
            return emitter.on('message', handler);
        },
        ready: ready,
        _deliver: function (msg) {
            emitter.emit('message', msg);
        },
        _setReady: function () {
            if (readyDone) return;
            readyDone = true;
            readyResolve();
        },
    };
}

function createP2pkitWasmGlue(config) {
    if (!config || !config.RTCPeerConnection) throw new Error('config.RTCPeerConnection is required');
    if (!config.WebSocket) throw new Error('config.WebSocket is required');
    if (typeof config.onEvent !== 'function') throw new Error('config.onEvent is required');

    const log = config.log || function () {};
    const now = config.now || function () { return Date.now(); };
    const gameName = typeof config.gameName === 'string' ? config.gameName : null;

    // The default channel table, composed from the individual wire constants so
    // the emitted Emscripten runtime keeps a closed reference set (see the
    // $createP2pkitWasmGlue__deps list below): channel 0 reliable+queued,
    // channel 1 lossy+drop.
    const defaultChannels = [
        {
            label: P2PKIT_WASM_CONTROL_LABEL,
            options: P2PKIT_WASM_CONTROL_OPTIONS,
            mode: 'queued',
            highWater: P2PKIT_WASM_CONTROL_HIGH_WATER,
            lowWater: P2PKIT_WASM_CONTROL_LOW_WATER,
        },
        {
            label: P2PKIT_WASM_COMMANDS_LABEL,
            options: P2PKIT_WASM_COMMANDS_OPTIONS,
            mode: 'drop',
            highWater: P2PKIT_WASM_COMMANDS_HIGH_WATER,
            lowWater: 0,
        },
    ];

    // Normalizes the caller's channel table into the internal shape:
    // [{ label, options, mode: 'queued'|'drop', highWater, lowWater }, ...].
    // Entries may be partial; anything omitted falls back to the matching
    // default for that array position.
    const rawChannels = config.channels;
    const source = Array.isArray(rawChannels) && rawChannels.length > 0 ? rawChannels : defaultChannels;
    const seenLabels = new Set();
    const channelSpecs = source.map(function (entry, index) {
        const fallback = defaultChannels[Math.min(index, defaultChannels.length - 1)];
        const spec = entry || {};
        const label = typeof spec.label === 'string' && spec.label.length > 0 ? spec.label : fallback.label;
        if (seenLabels.has(label)) {
            throw new Error('p2pkit-wasm: duplicate channel label "' + label + '"');
        }
        seenLabels.add(label);
        const mode = spec.mode === 'queued' || spec.mode === 'drop' ? spec.mode : fallback.mode;
        const highWater = Number.isFinite(spec.highWater) ? spec.highWater : fallback.highWater;
        const lowWater = Number.isFinite(spec.lowWater) ? spec.lowWater
            : (mode === 'queued' ? P2PKIT_WASM_CONTROL_LOW_WATER : 0);
        return {
            label: label,
            options: spec.options || fallback.options,
            mode: mode,
            highWater: highWater,
            lowWater: lowWater,
        };
    });
    const maxSignalBytes = Number.isFinite(config.maxSignalBytes) ? config.maxSignalBytes : P2PKIT_WASM_MAX_SIGNAL_BYTES;
    // Cause code reported on the Disconnect event when the peer goes away
    // (games map this to their own disconnect-reason enum).
    const disconnectCause = Number.isFinite(config.disconnectCause) ? config.disconnectCause : 1;

    // ---- passive telemetry (diagnostics only; no behavior depends on it) ----
    const stats = {
        game: gameName,             // matchmaking scope riding {"t":"find"}
        role: null,                 // 'finding' | 'host' | 'joiner'
        roomCode: null,             // always null: the lobby has no room codes
        signalingState: 'idle',     // idle|connecting|open|closed|error
        peerConnectionState: 'new',
        channels: channelSpecs.map(function (spec) {
            return {
                label: spec.label, mode: spec.mode, state: 'new',
                sent: 0, received: 0, dropped: 0, queued: 0,
                lastPacketId: -1, lastPacketLen: 0,
            };
        }),
        messages: [],               // capped ring of {dir, channel, packetId, len, t}
    };

    function recordMessage(dir, channel, bytes) {
        const ch = stats.channels[channel];
        if (!ch) return;
        // first 4 bytes LE = application packet type (game wire format)
        let packetId = -1;
        if (bytes && bytes.length >= 4) {
            packetId = (bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24)) >>> 0;
        }
        ch.lastPacketId = packetId;
        ch.lastPacketLen = bytes ? bytes.length : 0;
        stats.messages.push({ dir: dir, channel: channel, packetId: packetId, len: bytes ? bytes.length : 0, t: now() });
        if (stats.messages.length > 512) stats.messages.splice(0, stats.messages.length - 512);
    }

    // ---- signaling ----
    let ws = null;
    let selfPeerId = null;      // synthetic dialect address: 'host' | 'joiner'
    let remotePeerId = null;
    let peerHandle = 0;         // stable C++-facing peer id (assigned on connect)
    let signallingChannel = null;
    let p2pkit = null;

    function getP2pkit() {
        if (!p2pkit) p2pkit = resolveP2pkit(config);
        return p2pkit;
    }

    function signalSend(obj) {
        if (!ws || ws.readyState !== config.WebSocket.OPEN) {
            log('webrtc: cannot signal, socket not open');
            return false;
        }
        const text = JSON.stringify(obj);
        if (text.length > maxSignalBytes) {
            log('webrtc: signal message too large');
            return false;
        }
        ws.send(text);
        return true;
    }

    function ensureSignallingChannel() {
        if (signallingChannel) return signallingChannel;
        const kit = getP2pkit();
        if (!kit) return null;
        signallingChannel = createSignallingChannel({
            sendRaw: function (text) {
                if (!ws || ws.readyState !== config.WebSocket.OPEN) {
                    log('webrtc: cannot signal, socket not open');
                    return false;
                }
                // Adapter send() emits dialect JSON; wrap once for the lobby relay.
                let dialect;
                try {
                    dialect = JSON.parse(text);
                } catch (e) {
                    log('webrtc: signalling adapter produced invalid JSON');
                    return false;
                }
                return signalSend({ t: 'sig', data: dialect });
            },
            p2pkit: kit,
            log: log,
            maxSignalBytes: maxSignalBytes,
        });
        signallingChannel.onMessage(function (msg) {
            handleDialectMessage(msg).catch(function (e) { fail('dialect: ' + e); });
        });
        return signallingChannel;
    }

    // ---- peer connection ----
    let pc = null;
    let channels = channelSpecs.map(function () { return null; }); // RTCDataChannel per game channel
    let outboxes = channelSpecs.map(function (spec) { return spec.mode === 'queued' ? [] : null; });
    let paused = channelSpecs.map(function () { return false; });
    let allChannelsOpen = false;

    function setState(next) {
        if (stats.peerConnectionState === next) return;
        stats.peerConnectionState = next;
        if (config.onStateChange) config.onStateChange(next);
    }

    function fail(reason) {
        log('webrtc: failed: ' + reason);
        setState('failed');
        if (config.onEvent) config.onEvent(P2PKIT_WASM_EVENT_STATE, 0, 0, P2PKIT_WASM_STATE_FAILED, null);
        closeEverything();
    }

    function makePeerConnection() {
        const kit = getP2pkit();
        const iceServers = config.iceServers || (kit && kit.DEFAULT_ICE_SERVERS) || [];
        const pcConfig = { iceServers: iceServers };
        const p = new config.RTCPeerConnection(pcConfig);
        p.onicecandidate = function (evt) {
            if (evt.candidate && signallingChannel && selfPeerId && remotePeerId) {
                signallingChannel.send({
                    iceCandidate: evt.candidate.toJSON ? evt.candidate.toJSON() : evt.candidate,
                    from: selfPeerId,
                    to: remotePeerId,
                });
            }
        };
        p.onconnectionstatechange = function () {
            setState(p.connectionState);
            if (p.connectionState === 'failed') fail('peer connection failed');
        };
        return p;
    }

    function everyChannelOpen() {
        return channels.every(function (ch) { return ch && ch.readyState === 'open'; });
    }

    function attachChannel(channel, gameChannel) {
        const spec = channelSpecs[gameChannel];
        channels[gameChannel] = channel;
        channel.binaryType = 'arraybuffer';
        stats.channels[gameChannel].state = channel.readyState;

        channel.onopen = function () {
            stats.channels[gameChannel].state = 'open';
            if (everyChannelOpen() && !allChannelsOpen) {
                allChannelsOpen = true;
                peerHandle += 1;
                log('webrtc: all data channels open (peer ' + peerHandle + ')');
                startRttPolling();
                if (config.onStateChange) config.onStateChange('connected');
                config.onEvent(P2PKIT_WASM_EVENT_CONNECT, peerHandle, 0, 0, null);
                if (config.onEvent) {
                    config.onEvent(P2PKIT_WASM_EVENT_STATE, 0, 0, P2PKIT_WASM_STATE_CONNECTED, null);
                }
            }
        };
        channel.onclose = function () {
            stats.channels[gameChannel].state = 'closed';
            notifyPeerLeft();
        };
        channel.onerror = function () {
            log('webrtc: channel ' + gameChannel + ' error');
        };
        channel.onmessage = function (evt) {
            const data = evt.data;
            if (typeof data === 'string') {
                log('webrtc: ignoring unexpected text message on channel ' + gameChannel);
                return;
            }
            const bytes = new Uint8Array(data);
            stats.channels[gameChannel].received += 1;
            recordMessage('recv', gameChannel, bytes);
            config.onEvent(P2PKIT_WASM_EVENT_MESSAGE, peerHandle, gameChannel, 0, bytes);
        };

        if (spec.mode === 'queued') {
            channel.bufferedAmountLowThreshold = spec.lowWater;
            channel.onbufferedamountlow = function () {
                if (paused[gameChannel]) {
                    paused[gameChannel] = false;
                    flushOutbox(gameChannel);
                }
            };
        }
    }

    function notifyPeerLeft() {
        if (!allChannelsOpen) return;
        allChannelsOpen = false;
        stopRttPolling();
        if (config.onEvent) {
            config.onEvent(P2PKIT_WASM_EVENT_DISCONNECT, peerHandle, 0, disconnectCause, null);
            config.onEvent(P2PKIT_WASM_EVENT_STATE, 0, 0, P2PKIT_WASM_STATE_FAILED, null);
        }
    }

    // ---- RTT estimation via WebRTC stats (polled; cached for the sync C++ api) ----
    let rttMs = 0;
    let rttTimer = null;
    function startRttPolling() {
        if (rttTimer || !pc || !pc.getStats) return;
        rttTimer = setInterval(function () {
            if (!pc) { stopRttPolling(); return; }
            pc.getStats().then(function (report) {
                let best = 0;
                report.forEach(function (entry) {
                    if (entry.type === 'candidate-pair' && entry.state === 'succeeded' &&
                        typeof entry.currentRoundTripTime === 'number') {
                        const ms = entry.currentRoundTripTime * 1000;
                        if (best === 0 || ms < best) best = ms;
                    }
                });
                if (best > 0) rttMs = Math.round(best);
            }).catch(function () {});
        }, 2000);
        if (typeof rttTimer === 'object' && rttTimer && typeof rttTimer.unref === 'function') rttTimer.unref();
    }
    function stopRttPolling() {
        if (rttTimer) { clearInterval(rttTimer); rttTimer = null; }
        rttMs = 0;
    }

    // ---- offer/answer via p2pkit dialect ------------------------------------
    async function createOfferAndSend() {
        if (!pc) pc = makePeerConnection();
        channelSpecs.forEach(function (spec, gameChannel) {
            attachChannel(pc.createDataChannel(spec.label, spec.options), gameChannel);
        });
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        if (!signallingChannel || !signallingChannel.send({
            description: { type: 'offer', sdp: pc.localDescription.sdp },
            from: selfPeerId,
            to: remotePeerId,
        })) {
            fail('offer send failed');
        }
    }

    async function handleDescription(description) {
        if (!pc) pc = makePeerConnection();
        if (description.type === 'offer') {
            await pc.setRemoteDescription(description);
            if (channels.every(function (ch) { return ch == null; })) {
                pc.ondatachannel = function (evt) {
                    const label = evt.channel.label;
                    const gameChannel = channelSpecs.findIndex(function (spec) { return spec.label === label; });
                    if (gameChannel >= 0) attachChannel(evt.channel, gameChannel);
                    else log('webrtc: ignoring unknown data channel ' + label);
                };
            }
            const answer = await pc.createAnswer();
            await pc.setLocalDescription(answer);
            if (!signallingChannel || !signallingChannel.send({
                description: { type: 'answer', sdp: pc.localDescription.sdp },
                from: selfPeerId,
                to: remotePeerId,
            })) {
                fail('answer send failed');
            }
        } else if (description.type === 'answer') {
            await pc.setRemoteDescription(description);
        } else {
            log('webrtc: unknown description type ' + description.type);
        }
    }

    async function handleDialectMessage(msg) {
        if (!msg || typeof msg.from !== 'string') return;
        if (typeof msg.to === 'string' && msg.to !== selfPeerId) return;
        if (!remotePeerId && typeof msg.to === 'string' && msg.to === selfPeerId) {
            remotePeerId = msg.from;
        }
        if (msg.from !== remotePeerId) {
            log('webrtc: dialect from unknown peer ' + msg.from);
            return;
        }
        if (msg.description) {
            await handleDescription(msg.description);
        } else if (msg.iceCandidate) {
            if (!pc) pc = makePeerConnection();
            try {
                await pc.addIceCandidate(msg.iceCandidate);
            } catch (e) {
                log('webrtc: addIceCandidate failed: ' + e);
            }
        }
    }

    // The lobby paired us with a peer and assigned a role: host = offerer
    // (creates the data channels and the SDP offer), joiner = answerer
    // (passively waits for the offer on pc.ondatachannel).
    function handleMatched(role) {
        if (role !== 'host' && role !== 'joiner') {
            fail('signaling: bad matched role ' + role);
            return;
        }
        stats.role = role;
        selfPeerId = role;
        remotePeerId = role === 'host' ? 'joiner' : 'host';
        log('webrtc: matched as ' + role);
        if (config.onEvent) config.onEvent(P2PKIT_WASM_EVENT_STATE, 0, 0, P2PKIT_WASM_STATE_CONNECTING, null);
        if (config.onEvent) config.onEvent(P2PKIT_WASM_EVENT_MATCHED, 0, 0, role === 'joiner' ? 1 : 0, null);
        if (!ensureSignallingChannel()) {
            fail('p2pkit unavailable');
            return;
        }
        if (role === 'host' && !pc) {
            createOfferAndSend().catch(function (e) { fail('offer: ' + e); });
        }
    }

    function handleLobbyMessage(msg) {
        if (!msg || typeof msg !== 'object') return;
        switch (msg.t) {
            case 'waiting':
                log('webrtc: waiting for an opponent');
                break;
            case 'matched':
                handleMatched(msg.role);
                break;
            case 'sig': {
                // Opaque passthrough: the payload is the peer's dialect envelope.
                const ch = ensureSignallingChannel();
                if (ch && msg.data && typeof msg.data === 'object') ch._deliver(msg.data);
                break;
            }
            case 'peer_left':
                log('webrtc: peer left');
                notifyPeerLeft();
                break;
            case 'error':
                fail('signaling: ' + msg.code + (msg.message ? ' ' + msg.message : ''));
                break;
            default:
                break;
        }
    }

    // ---- websocket lifecycle ----
    function connectSignaling(onOpen) {
        const url = resolveSignalingUrl(config.signaling);
        stats.signalingState = 'connecting';
        ws = new config.WebSocket(url);
        ws.onopen = function () {
            stats.signalingState = 'open';
            log('webrtc: signaling connected (' + url + ')');
            const ch = ensureSignallingChannel();
            if (ch) ch._setReady();
            if (config.onSignalingOpen) config.onSignalingOpen();
            if (onOpen) onOpen();
        };
        ws.onclose = function () {
            if (stats.signalingState !== 'error') stats.signalingState = 'closed';
            log('webrtc: signaling closed');
            if (!allChannelsOpen) fail('signaling closed before connect');
        };
        ws.onerror = function () {
            stats.signalingState = 'error';
            fail('signaling error');
        };
        ws.onmessage = function (evt) {
            let msg;
            try {
                msg = JSON.parse(evt.data);
            } catch (e) {
                log('webrtc: invalid signaling JSON');
                return;
            }
            handleLobbyMessage(msg);
        };
    }

    function resolveSignalingUrl(cfg) {
        if (cfg) return cfg;
        if (typeof location !== 'undefined' && location.host) {
            const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
            return scheme + '//' + location.host;
        }
        return 'ws://127.0.0.1:8788';
    }

    function closeEverything() {
        outboxes = channelSpecs.map(function (spec) { return spec.mode === 'queued' ? [] : null; });
        paused = channelSpecs.map(function () { return false; });
        channels.forEach(function (ch, k) {
            if (ch) {
                try { ch.close(); } catch (e) {}
                channels[k] = null;
                stats.channels[k].state = 'closed';
            }
        });
        if (pc) {
            try { pc.close(); } catch (e) {}
            pc = null;
        }
        if (ws) {
            // Detach handlers first so a deliberate close (disconnect/cancel)
            // does not report itself as a signaling failure.
            ws.onclose = null;
            ws.onerror = null;
            ws.onmessage = null;
            try { ws.close(); } catch (e) {}
            ws = null;
            stats.signalingState = 'closed';
        }
        signallingChannel = null;
        remotePeerId = null;
    }

    // ---- outgoing game traffic ----
    function flushOutbox(gameChannel) {
        const channel = channels[gameChannel];
        const outbox = outboxes[gameChannel];
        if (!channel || channel.readyState !== 'open' || !outbox) return;
        while (outbox.length > 0) {
            const bytes = outbox[0];
            if (channel.bufferedAmount >= channelSpecs[gameChannel].highWater) {
                paused[gameChannel] = true;
                return;
            }
            outbox.shift();
            channel.send(bytes);
            stats.channels[gameChannel].sent += 1;
            recordMessage('send', gameChannel, bytes);
        }
    }

    function send(gameChannel, bytes) {
        const channel = channels[gameChannel];
        if (!channel || channel.readyState !== 'open') return false;
        if (channelSpecs[gameChannel].mode === 'queued') {
            if (paused[gameChannel] || channel.bufferedAmount >= channelSpecs[gameChannel].highWater) {
                paused[gameChannel] = true;
                outboxes[gameChannel].push(bytes);
                stats.channels[gameChannel].queued += 1;
                return true;   // queued, will be delivered in order
            }
            channel.send(bytes);
            stats.channels[gameChannel].sent += 1;
            recordMessage('send', gameChannel, bytes);
            return true;
        }
        // drop channels are lossy by contract — drop under congestion
        if (channel.bufferedAmount >= channelSpecs[gameChannel].highWater) {
            stats.channels[gameChannel].dropped += 1;
            return false;
        }
        channel.send(bytes);
        stats.channels[gameChannel].sent += 1;
        recordMessage('send', gameChannel, bytes);
        return true;
    }

    // ---- public api ----
    const api = {
        findMatch: function () {
            if (stats.role) return false;
            stats.role = 'finding';
            connectSignaling(function () {
                const find = { t: 'find' };
                if (gameName) find.game = gameName;
                signalSend(find);
            });
            return true;
        },
        cancelMatchmaking: function () {
            // Only meaningful while queued: a pairing is left via disconnect().
            if (stats.role !== 'finding') return false;
            signalSend({ t: 'cancel' });
            closeEverything();
            stats.role = null;
            return true;
        },
        send: send,
        getRole: function () { return stats.role; },
        getStats: function () { return stats; },
        getPeerHandle: function () { return peerHandle; },
        getRemotePeerId: function () { return remotePeerId; },
        // RTT estimate from WebRTC getStats (candidate-pair currentRoundTripTime),
        // refreshed every 2 s while connected; 0 while not connected.
        getRttMs: function () {
            return rttMs;
        },
        disconnect: function () {
            notifyPeerLeft();
            closeEverything();
            stats.role = null;
        },
        _flushOutboxForTest: flushOutbox,
    };

    return api;
}

// Node export (unit tests); Emscripten library wiring below.
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        createP2pkitWasmGlue,
        createSignallingChannel,
        resolveP2pkit,
        validateSignallingMessage,
        P2PKIT_WASM_DEFAULT_CHANNELS,
        P2PKIT_WASM_CONTROL_LABEL,
        P2PKIT_WASM_COMMANDS_LABEL,
        P2PKIT_WASM_CONTROL_OPTIONS,
        P2PKIT_WASM_COMMANDS_OPTIONS,
        P2PKIT_WASM_CONTROL_HIGH_WATER,
        P2PKIT_WASM_CONTROL_LOW_WATER,
        P2PKIT_WASM_COMMANDS_HIGH_WATER,
        P2PKIT_WASM_MAX_SIGNAL_BYTES,
        P2PKIT_WASM_EVENT_CONNECT,
        P2PKIT_WASM_EVENT_DISCONNECT,
        P2PKIT_WASM_EVENT_MESSAGE,
        P2PKIT_WASM_EVENT_STATE,
        P2PKIT_WASM_EVENT_MATCHED,
        P2PKIT_WASM_STATE_IDLE,
        P2PKIT_WASM_STATE_CONNECTING,
        P2PKIT_WASM_STATE_CONNECTED,
        P2PKIT_WASM_STATE_FAILED,
        _createSignallingChannelForTest: createSignallingChannel,
    };
}

/*
 * Emscripten --js-library wiring. Consumed via `--js-library js/p2pkit_webrtc_glue.js`.
 * This block publishes the factory and its constants as $-prefixed library
 * symbols so a game's own js-library adapter can retain them through __deps
 * (see README.md for the adapter template); the C-export shims
 * (webrtcFindMatch & friends) are game-owned because they name the game's
 * exported event pump. When this file is loaded under Node (unit tests),
 * mergeInto/LibraryManager do not exist and this block is skipped.
 */
if (typeof mergeInto === 'function' && typeof LibraryManager !== 'undefined') {
    mergeInto(LibraryManager.library, {
        // Emscripten emits $NAME library items whose value is a string starting
        // with '=' as `var NAME = <verbatim>;` in the built runtime. The values
        // below must match the top-level const declarations above exactly;
        // test/library.test.cjs enforces that.
        $P2PKIT_WASM_CONTROL_LABEL: "='control'",
        $P2PKIT_WASM_COMMANDS_LABEL: "='commands'",
        $P2PKIT_WASM_CONTROL_OPTIONS: '={ ordered: true }',
        $P2PKIT_WASM_COMMANDS_OPTIONS: '={ ordered: false, maxRetransmits: 0 }',
        $P2PKIT_WASM_CONTROL_HIGH_WATER: '=(512 * 1024)',
        $P2PKIT_WASM_CONTROL_LOW_WATER: '=(128 * 1024)',
        $P2PKIT_WASM_COMMANDS_HIGH_WATER: '=(512 * 1024)',
        $P2PKIT_WASM_MAX_SIGNAL_BYTES: '=(256 * 1024)',
        $P2PKIT_WASM_EVENT_CONNECT: '=0',
        $P2PKIT_WASM_EVENT_DISCONNECT: '=1',
        $P2PKIT_WASM_EVENT_MESSAGE: '=2',
        $P2PKIT_WASM_EVENT_STATE: '=3',
        $P2PKIT_WASM_EVENT_MATCHED: '=4',
        $P2PKIT_WASM_STATE_IDLE: '=0',
        $P2PKIT_WASM_STATE_CONNECTING: '=1',
        $P2PKIT_WASM_STATE_CONNECTED: '=2',
        $P2PKIT_WASM_STATE_FAILED: '=3',

        $resolveP2pkit: resolveP2pkit,
        $validateSignallingMessage: validateSignallingMessage,
        $createSignallingChannel__deps: [
            '$validateSignallingMessage', '$P2PKIT_WASM_MAX_SIGNAL_BYTES',
        ],
        $createSignallingChannel: createSignallingChannel,

        // Retain the factory in emitted JS; Emscripten only keeps $-prefixed library
        // symbols. __deps recursively retains every $P2PKIT_WASM_* constant above,
        // so the emitted factory has no free missing identifiers.
        $createP2pkitWasmGlue__deps: [
            '$P2PKIT_WASM_CONTROL_LABEL', '$P2PKIT_WASM_COMMANDS_LABEL',
            '$P2PKIT_WASM_CONTROL_OPTIONS', '$P2PKIT_WASM_COMMANDS_OPTIONS',
            '$P2PKIT_WASM_CONTROL_HIGH_WATER', '$P2PKIT_WASM_CONTROL_LOW_WATER',
            '$P2PKIT_WASM_COMMANDS_HIGH_WATER', '$P2PKIT_WASM_MAX_SIGNAL_BYTES',
            '$P2PKIT_WASM_EVENT_CONNECT', '$P2PKIT_WASM_EVENT_DISCONNECT',
            '$P2PKIT_WASM_EVENT_MESSAGE', '$P2PKIT_WASM_EVENT_STATE',
            '$P2PKIT_WASM_EVENT_MATCHED',
            '$P2PKIT_WASM_STATE_IDLE', '$P2PKIT_WASM_STATE_CONNECTING',
            '$P2PKIT_WASM_STATE_CONNECTED', '$P2PKIT_WASM_STATE_FAILED',
            '$resolveP2pkit', '$createSignallingChannel',
        ],
        $createP2pkitWasmGlue: createP2pkitWasmGlue,
    });
}
