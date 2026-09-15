// Unit and integration tests for platform/web/webrtc_glue.js (createDuneCityWebRtc).
// Mocks RTCPeerConnection / RTCDataChannel / WebSocket so the DI core can run under Node.

'use strict';

require('./ensure-p2pkit-bundle.cjs');

const { test } = require('node:test');
const assert = require('node:assert/strict');
const WebSocket = require('ws');
const {
  createDuneCityWebRtc,
  resolveP2pkit,
  _createSignallingChannelForTest,
  DUNECITY_WEBRTC_CONTROL_OPTIONS,
  DUNECITY_WEBRTC_COMMANDS_OPTIONS,
  DUNECITY_WEBRTC_CONTROL_HIGH_WATER,
  DUNECITY_WEBRTC_COMMANDS_HIGH_WATER,
  DUNECITY_WEBRTC_EVENT_CONNECT,
  DUNECITY_WEBRTC_EVENT_DISCONNECT,
  DUNECITY_WEBRTC_EVENT_MESSAGE,
  DUNECITY_WEBRTC_EVENT_STATE,
  DUNECITY_WEBRTC_EVENT_MATCHED,
  DUNECITY_WEBRTC_STATE_CONNECTING,
  DUNECITY_WEBRTC_STATE_CONNECTED,
  DUNECITY_WEBRTC_STATE_FAILED,
} = require('../webrtc_glue.js');

const p2pkit = resolveP2pkit({});

const DUNECITY_WEBRTC_CONTROL_LOW_WATER = 128 * 1024;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// ---- mock WebRTC -----------------------------------------------------------

class MockDataChannel {
  constructor(label, options = {}) {
    this.label = label;
    this.options = options;
    this.binaryType = 'arraybuffer';
    this.readyState = 'connecting';
    this.bufferedAmount = 0;
    this.bufferedAmountLowThreshold = 0;
    this.sent = [];
    this._peer = null;
    this.onopen = null;
    this.onclose = null;
    this.onerror = null;
    this.onmessage = null;
    this.onbufferedamountlow = null;
  }

  linkPeer(other) {
    this._peer = other;
    other._peer = this;
  }

  send(data) {
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    this.sent.push(bytes);
    this.bufferedAmount += bytes.length;
    if (this._peer?.onmessage) {
      const payload = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      this._peer.onmessage({ data: payload, type: 'message', target: this._peer });
    }
  }

  open() {
    if (this.readyState === 'open') return;
    this.readyState = 'open';
    this.onopen?.({ type: 'open', target: this });
    if (this._peer && this._peer.readyState !== 'open') {
      this._peer.open();
    }
  }

  close() {
    if (this.readyState === 'closed') return;
    this.readyState = 'closed';
    this.onclose?.({ type: 'close', target: this });
  }

  setBufferedAmount(amount) {
    this.bufferedAmount = amount;
    if (amount <= this.bufferedAmountLowThreshold) {
      this.onbufferedamountlow?.({ type: 'bufferedamountlow', target: this });
    }
  }
}

class MockPeerConnection {
  constructor(config) {
    this.config = config;
    this.connectionState = 'new';
    this.localDescription = null;
    this.remoteDescription = null;
    this.onicecandidate = null;
    this.onconnectionstatechange = null;
    this._ondatachannelHandler = null;
    this._channels = [];
    this._outboundRemoteChannels = [];
    this._incomingChannels = [];
    this._remote = null;
    this._candidates = [];
  }

  get ondatachannel() {
    return this._ondatachannelHandler;
  }

  set ondatachannel(handler) {
    this._ondatachannelHandler = handler;
    this._scheduleDeliverIncoming();
  }

  linkTo(other) {
    this._remote = other;
    other._remote = this;
    for (const remote of this._outboundRemoteChannels) {
      other._enqueueIncomingChannel(remote);
    }
    for (const remote of other._outboundRemoteChannels) {
      this._enqueueIncomingChannel(remote);
    }
  }

  _enqueueIncomingChannel(channel) {
    this._incomingChannels.push(channel);
    this._scheduleDeliverIncoming();
  }

  _scheduleDeliverIncoming() {
    queueMicrotask(() => {
      if (!this._ondatachannelHandler || this._incomingChannels.length === 0) return;
      while (this._incomingChannels.length > 0) {
        const channel = this._incomingChannels.shift();
        this._ondatachannelHandler({ channel, type: 'datachannel', target: this });
      }
    });
  }

  createDataChannel(label, options) {
    const local = new MockDataChannel(label, options);
    const remote = new MockDataChannel(label, options);
    local.linkPeer(remote);
    this._channels.push(local);
    this._outboundRemoteChannels.push(remote);
    if (this._remote) {
      this._remote._enqueueIncomingChannel(remote);
    }
    return local;
  }

  async createOffer() {
    return { type: 'offer', sdp: 'mock-offer' };
  }

  async createAnswer() {
    return { type: 'answer', sdp: 'mock-answer' };
  }

  async setLocalDescription(desc) {
    this.localDescription = desc;
    this._emitIce();
  }

  async setRemoteDescription(desc) {
    this.remoteDescription = desc;
  }

  async addIceCandidate(candidate) {
    this._candidates.push(candidate);
  }

  _emitIce() {
    const candidateObj = {
      candidate: 'candidate:mock 1 udp 2130706431 127.0.0.1 9 typ host',
      sdpMid: '0',
      sdpMLineIndex: 0,
      toJSON() {
        return { candidate: this.candidate, sdpMid: this.sdpMid, sdpMLineIndex: this.sdpMLineIndex };
      },
    };
    this.onicecandidate?.({
      candidate: candidateObj,
      type: 'icecandidate',
      target: this,
    });
    this.onicecandidate?.({ candidate: null, type: 'icecandidate', target: this });
  }

  openAllChannels() {
    for (const ch of this._channels) ch.open();
    this.connectionState = 'connected';
    this.onconnectionstatechange?.({ type: 'connectionstatechange', target: this });
    if (this._remote) {
      this._remote.connectionState = 'connected';
      this._remote.onconnectionstatechange?.({ type: 'connectionstatechange', target: this._remote });
    }
  }

  async getStats() {
    const report = new Map();
    report.set('pair', {
      type: 'candidate-pair',
      state: 'succeeded',
      currentRoundTripTime: 0.042,
    });
    return report;
  }

  close() {
    this.connectionState = 'closed';
    for (const ch of this._channels) ch.close();
  }
}

MockPeerConnection.generateCertificate = async () => ({});

// ---- mock / real signaling helpers -----------------------------------------

class MockWebSocket {
  static OPEN = 1;
  static CONNECTING = 0;
  static CLOSED = 3;

  constructor(url) {
    this.url = url;
    this.readyState = MockWebSocket.OPEN;
    this.sent = [];
    this.onopen = null;
    this.onclose = null;
    this.onerror = null;
    this.onmessage = null;
    queueMicrotask(() => this.onopen?.({ type: 'open', target: this }));
  }

  send(text) {
    this.sent.push(JSON.parse(text));
  }

  receive(msg) {
    this.onmessage?.({ data: JSON.stringify(msg), type: 'message', target: this });
  }

  close() {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.({ type: 'close', target: this });
  }
}

function makeWebSocketFactory() {
  const sockets = [];
  return {
    sockets,
    WebSocket: class extends MockWebSocket {
      constructor(url) {
        super(url);
        sockets.push(this);
      }
    },
  };
}

function collectEvents(onEvent) {
  const events = [];
  const handler = (type, peer, channel, cause, bytes) => {
    events.push({
      type,
      peer,
      channel,
      cause,
      bytes: bytes ? Array.from(bytes) : null,
    });
    onEvent?.(type, peer, channel, cause, bytes);
  };
  return { events, handler };
}

async function startSignalingServer() {
  const { createSignalingServer } = await import('./fixtures/bootstrapping-server.js');
  const ctx = createSignalingServer();
  await new Promise((resolve, reject) => {
    ctx.httpServer.once('error', reject);
    ctx.httpServer.listen(0, '127.0.0.1', resolve);
  });
  const port = ctx.httpServer.address().port;
  return { ...ctx, url: `ws://127.0.0.1:${port}/` };
}

function makeRealWebSocketClass(urlPrefix) {
  return class NodeWebSocket {
    static OPEN = WebSocket.OPEN;
    static CONNECTING = WebSocket.CONNECTING;
    static CLOSED = WebSocket.CLOSED;

    constructor(path) {
      this._ws = new WebSocket(`${urlPrefix}${path ?? ''}`);
      this.readyState = WebSocket.CONNECTING;
      this._ws.on('open', () => {
        this.readyState = WebSocket.OPEN;
        this.onopen?.({ type: 'open', target: this });
      });
      this._ws.on('message', (raw) => {
        this.onmessage?.({ data: raw.toString('utf8'), type: 'message', target: this });
      });
      this._ws.on('close', () => {
        this.readyState = WebSocket.CLOSED;
        this.onclose?.({ type: 'close', target: this });
      });
      this._ws.on('error', () => {
        this.onerror?.({ type: 'error', target: this });
      });
    }

    send(text) {
      this._ws.send(text);
    }

    close() {
      this._ws.close();
    }
  };
}

async function waitFor(predicate, timeoutMs = 3000, label = 'condition') {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const value = predicate();
    if (value) return value;
    await sleep(10);
  }
  throw new Error(`timeout waiting for ${label}`);
}

function isDescriptionOffer(msg) {
  return msg && msg.description && msg.description.type === 'offer';
}

function isDescriptionAnswer(msg) {
  return msg && msg.description && msg.description.type === 'answer';
}

function isSigEnvelope(msg, predicate) {
  return msg && msg.t === 'sig' && predicate(msg.data);
}

// Drives two glued transports through the matchmaking lobby protocol with mock
// sockets: both find, the lobby pairs them (waiter hosts, newcomer joins), and
// the dialect envelopes ride the {"t":"sig"} channel exactly as the server
// relays them (verbatim).
async function connectMockPair() {
  const wsFactory = makeWebSocketFactory();
  const pcs = [];
  const RTCPeerConnection = class extends MockPeerConnection {
    constructor(...args) {
      super(...args);
      pcs.push(this);
      if (pcs.length === 2) pcs[0].linkTo(pcs[1]);
    }
  };

  const { events, handler } = collectEvents();
  const host = createDuneCityWebRtc({ RTCPeerConnection, WebSocket: wsFactory.WebSocket, p2pkit, onEvent: handler });
  const client = createDuneCityWebRtc({ RTCPeerConnection, WebSocket: wsFactory.WebSocket, p2pkit, onEvent: handler });

  // Lobby: the first finder waits; the second finder pairs with them.
  assert.equal(host.findMatch(), true);
  const hostWs = wsFactory.sockets[0];
  await waitFor(() => hostWs.sent.some((m) => m.t === 'find'));
  hostWs.receive({ t: 'waiting' });

  assert.equal(client.findMatch(), true);
  const clientWs = wsFactory.sockets[1];
  await waitFor(() => clientWs.sent.some((m) => m.t === 'find'));
  hostWs.receive({ t: 'matched', role: 'host' });
  clientWs.receive({ t: 'matched', role: 'joiner' });

  // Host offers; the envelope rides the sig channel wrapped verbatim.
  await waitFor(() => pcs.length === 1 && hostWs.sent.some((m) => isSigEnvelope(m, isDescriptionOffer)));
  const offerEnvelope = hostWs.sent.find((m) => isSigEnvelope(m, isDescriptionOffer));
  clientWs.receive(offerEnvelope);

  await waitFor(() => pcs.length === 2 && clientWs.sent.some((m) => isSigEnvelope(m, isDescriptionAnswer)));
  const answerEnvelope = clientWs.sent.find((m) => isSigEnvelope(m, isDescriptionAnswer));
  hostWs.receive(answerEnvelope);

  const hostPc = pcs[0];
  const clientPc = pcs[1];
  hostPc.openAllChannels();
  await waitFor(() => host.getStats().channels[0].state === 'open');

  return { host, client, hostPc, clientPc, hostWs, clientWs, events, handler, wsFactory };
}

// ---- tests -----------------------------------------------------------------

test('createDuneCityWebRtc requires injected dependencies', () => {
  assert.throws(() => createDuneCityWebRtc(null), /RTCPeerConnection is required/);
  assert.throws(() => createDuneCityWebRtc({ RTCPeerConnection: MockPeerConnection }), /WebSocket is required/);
  assert.throws(
    () => createDuneCityWebRtc({ RTCPeerConnection: MockPeerConnection, WebSocket: MockWebSocket }),
    /onEvent is required/,
  );
});

test('findMatch sends {"t":"find"} after signaling opens and duplicate find is idempotent', async () => {
  const wsFactory = makeWebSocketFactory();
  const { events, handler } = collectEvents();
  const rtc = createDuneCityWebRtc({
    RTCPeerConnection: MockPeerConnection,
    WebSocket: wsFactory.WebSocket,
    config: { signaling: 'ws://mock/' },
    onEvent: handler,
  });

  assert.equal(rtc.findMatch(), true);
  assert.equal(rtc.findMatch(), false, 'second findMatch is rejected');
  assert.equal(rtc.getRole(), 'finding');

  await waitFor(() => wsFactory.sockets.length === 1 && wsFactory.sockets[0].sent.length === 1);
  const ws = wsFactory.sockets[0];
  assert.deepEqual(ws.sent[0], { t: 'find' });

  ws.receive({ t: 'waiting' });
  assert.equal(rtc.getRole(), 'finding', 'still queued while waiting');
  assert.ok(!events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MATCHED), 'no pairing while queued');

  assert.equal(rtc.cancelMatchmaking(), true);
  assert.equal(rtc.getRole(), null, 'cancel leaves the lobby');
  assert.equal(rtc.cancelMatchmaking(), false, 'cancel is a no-op when not queued');
});

test('matched joiner takes the answerer path and answers the host offer', async () => {
  const wsFactory = makeWebSocketFactory();
  const pcs = [];
  const RTCPeerConnection = class extends MockPeerConnection {
    constructor(...args) {
      super(...args);
      pcs.push(this);
    }
  };

  const { events, handler } = collectEvents();
  const joiner = createDuneCityWebRtc({
    RTCPeerConnection,
    WebSocket: wsFactory.WebSocket,
    p2pkit,
    onEvent: handler,
  });

  assert.equal(joiner.findMatch(), true);
  const ws = wsFactory.sockets[0];
  await waitFor(() => ws.sent.some((m) => m.t === 'find'));
  ws.receive({ t: 'waiting' });
  ws.receive({ t: 'matched', role: 'joiner' });

  assert.equal(joiner.getRole(), 'joiner');
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MATCHED && e.cause === 1), 'joiner role code is 1');
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_STATE && e.cause === DUNECITY_WEBRTC_STATE_CONNECTING));
  assert.equal(pcs.length, 0, 'joiner creates no peer connection before the offer arrives');

  ws.receive({ t: 'sig', data: { description: { type: 'offer', sdp: 'mock-offer' }, from: 'host', to: 'joiner' } });

  await waitFor(() => pcs.length === 1 && ws.sent.some((m) => isSigEnvelope(m, isDescriptionAnswer)));
  assert.equal(pcs[0].remoteDescription.type, 'offer');
  const answer = ws.sent.find((m) => isSigEnvelope(m, isDescriptionAnswer));
  assert.equal(answer.data.from, 'joiner');
  assert.equal(answer.data.to, 'host');
});

test('matched host creates the offer and both data channels with expected options', async () => {
  const wsFactory = makeWebSocketFactory();
  const pcs = [];
  const RTCPeerConnection = class extends MockPeerConnection {
    constructor(...args) {
      super(...args);
      pcs.push(this);
    }
  };

  const { events, handler } = collectEvents();
  const host = createDuneCityWebRtc({
    RTCPeerConnection,
    WebSocket: wsFactory.WebSocket,
    p2pkit,
    onEvent: handler,
  });

  assert.equal(host.findMatch(), true);
  const ws = wsFactory.sockets[0];
  await waitFor(() => ws.sent.some((m) => m.t === 'find'));
  ws.receive({ t: 'waiting' });
  ws.receive({ t: 'matched', role: 'host' });

  assert.equal(host.getRole(), 'host');
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MATCHED && e.cause === 0), 'host role code is 0');

  await waitFor(() => pcs.length === 1 && ws.sent.some((m) => isSigEnvelope(m, isDescriptionOffer)));
  const pc = pcs[0];
  assert.equal(pc._channels.length, 2);
  assert.deepEqual(pc._channels[0].options, DUNECITY_WEBRTC_CONTROL_OPTIONS);
  assert.deepEqual(pc._channels[1].options, DUNECITY_WEBRTC_COMMANDS_OPTIONS);
  const offer = ws.sent.find((m) => isSigEnvelope(m, isDescriptionOffer));
  assert.equal(offer.data.from, 'host');
  assert.equal(offer.data.to, 'joiner');
});

test('both channels open emits CONNECT and CONNECTED state', async () => {
  const { events } = await connectMockPair();
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_CONNECT));
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_STATE && e.cause === DUNECITY_WEBRTC_STATE_CONNECTED));
});

test('send delivers binary payloads on the control channel', async () => {
  const { host, hostPc, events } = await connectMockPair();
  const payload = new Uint8Array([0x01, 0x00, 0x00, 0x00, 0x42]);
  assert.equal(host.send(0, payload), true);

  await waitFor(() => hostPc._channels[0].sent.length === 1);
  assert.deepEqual(Array.from(hostPc._channels[0].sent[0]), Array.from(payload));

  await waitFor(() => events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MESSAGE && e.channel === 0));
  const msg = events.find((e) => e.type === DUNECITY_WEBRTC_EVENT_MESSAGE && e.channel === 0);
  assert.deepEqual(msg.bytes, Array.from(payload));
});

test('control channel backpressure queues and flushes on bufferedamountlow', async () => {
  const { host, hostPc } = await connectMockPair();
  const control = hostPc._channels[0];
  control.setBufferedAmount(DUNECITY_WEBRTC_CONTROL_HIGH_WATER);

  const bytes = new Uint8Array([9, 9, 9, 9]);
  assert.equal(host.send(0, bytes), true);
  assert.equal(host.getStats().channels[0].queued, 1);
  assert.equal(control.sent.length, 0);

  control.setBufferedAmount(DUNECITY_WEBRTC_CONTROL_LOW_WATER);
  await waitFor(() => control.sent.length === 1, 3000, 'queued control flush');
  assert.deepEqual(Array.from(control.sent[0]), Array.from(bytes));
});

test('commands channel drops when bufferedAmount is at high water', async () => {
  const { host, hostPc } = await connectMockPair();
  hostPc._channels[1].bufferedAmount = DUNECITY_WEBRTC_COMMANDS_HIGH_WATER;

  const payload = new Uint8Array([1, 2, 3, 4]);
  assert.equal(host.send(1, payload), false);
  assert.equal(host.getStats().channels[1].dropped, 1);
});

test('peer_left emits DISCONNECT after connect', async () => {
  const { hostWs, events } = await connectMockPair();
  hostWs.receive({ t: 'peer_left' });
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_DISCONNECT));
});

test('integration: two finders pair through the real matchmaking lobby', async () => {
  const server = await startSignalingServer();
  const NodeWebSocket = makeRealWebSocketClass(server.url);
  const pcs = [];

  const RTCPeerConnection = class extends MockPeerConnection {
    constructor(...args) {
      super(...args);
      pcs.push(this);
      if (pcs.length === 2) pcs[0].linkTo(pcs[1]);
    }
  };

  const { events, handler } = collectEvents();
  const first = createDuneCityWebRtc({
    RTCPeerConnection,
    WebSocket: NodeWebSocket,
    config: { signaling: server.url },
    p2pkit,
    onEvent: handler,
  });
  const second = createDuneCityWebRtc({
    RTCPeerConnection,
    WebSocket: NodeWebSocket,
    config: { signaling: server.url },
    p2pkit,
    onEvent: handler,
  });

  try {
    // The first finder queues; the lobby pairs the second finder with them and
    // assigns the roles itself (waiter -> host, newcomer -> joiner).
    assert.equal(first.findMatch(), true);
    await waitFor(() => server.queue.length === 1, 3000, 'first finder queued');
    assert.equal(second.findMatch(), true);
    await waitFor(
      () => first.getRole() === 'host' && second.getRole() === 'joiner',
      5000,
      'lobby pairing',
    );

    // Negotiation rides the lobby's sig relay from here on.
    await waitFor(() => pcs.some((pc) => pc._channels.length === 2), 5000, 'host data channels');
    await waitFor(() => pcs.every((pc) => pc.localDescription), 5000, 'local descriptions');
    const initiatorPc = pcs.find((pc) => pc._channels.length === 2);
    initiatorPc.openAllChannels();

    await waitFor(
      () => events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_CONNECT),
      5000,
      'CONNECT event',
    );

    const ping = new Uint8Array([0x04, 0x00, 0x00, 0x00, 0x7]);
    assert.equal(first.send(0, ping), true);

    await waitFor(
      () => events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MESSAGE && e.channel === 0),
      3000,
      'MESSAGE event',
    );
    const msg = events.find((e) => e.type === DUNECITY_WEBRTC_EVENT_MESSAGE && e.channel === 0);
    assert.deepEqual(msg.bytes, Array.from(ping));
    assert.equal(first.getStats().channels[0].sent, 1);
  } finally {
    first.disconnect();
    second.disconnect();
    server.close();
  }
});

test('module exports include channel option constants', () => {
  assert.equal(DUNECITY_WEBRTC_CONTROL_OPTIONS.ordered, true);
  assert.equal(DUNECITY_WEBRTC_COMMANDS_OPTIONS.ordered, false);
  assert.equal(DUNECITY_WEBRTC_COMMANDS_OPTIONS.maxRetransmits, 0);
  assert.equal(DUNECITY_WEBRTC_CONTROL_HIGH_WATER, 512 * 1024);
  assert.equal(DUNECITY_WEBRTC_COMMANDS_HIGH_WATER, 512 * 1024);
});

test('signalling adapter send() emits p2pkit dialect with from/to and rejects malformed messages', () => {
  const sent = [];
  const channel = _createSignallingChannelForTest({
    sendRaw: (text) => {
      sent.push(JSON.parse(text));
      return true;
    },
    p2pkit,
    log: () => {},
  });

  assert.equal(
    channel.send({ description: { type: 'offer', sdp: 'x' }, from: 'pa', to: 'pb' }),
    true,
  );
  assert.equal(sent.length, 1);
  assert.equal(sent[0].from, 'pa');
  assert.equal(sent[0].to, 'pb');
  assert.deepEqual(sent[0].description, { type: 'offer', sdp: 'x' });

  assert.equal(channel.send({ description: { type: 'offer', sdp: 'x' }, from: 'pa' }), false);
});

test('p2pkit dialect envelopes produce the expected webrtcOnEvent sequence', async () => {
  const { host, client, hostWs, events } = await connectMockPair();

  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_STATE && e.cause === DUNECITY_WEBRTC_STATE_CONNECTING));
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MATCHED && e.cause === 0), 'host matched first');
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_CONNECT));
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_STATE && e.cause === DUNECITY_WEBRTC_STATE_CONNECTED));

  const payload = new Uint8Array([0x02, 0x00, 0x00, 0x00, 0x01]);
  assert.equal(host.send(0, payload), true);
  await waitFor(() => events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_MESSAGE && e.channel === 0));

  hostWs.receive({ t: 'peer_left' });
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_DISCONNECT));
  assert.ok(events.some((e) => e.type === DUNECITY_WEBRTC_EVENT_STATE && e.cause === DUNECITY_WEBRTC_STATE_FAILED));

  host.disconnect();
  client.disconnect();
});

test('stock RTCTransport negotiates over the signalling adapter wire format', async () => {
  assert.ok(p2pkit && p2pkit.RTCTransport, 'p2pkit bundle must export RTCTransport');

  const hostSent = [];
  const clientSent = [];
  const hostAdapter = _createSignallingChannelForTest({
    sendRaw: (text) => {
      hostSent.push(JSON.parse(text));
      return true;
    },
    p2pkit,
    log: () => {},
  });
  const clientAdapter = _createSignallingChannelForTest({
    sendRaw: (text) => {
      clientSent.push(JSON.parse(text));
      return true;
    },
    p2pkit,
    log: () => {},
  });
  hostAdapter._setReady();
  clientAdapter._setReady();

  hostAdapter.onMessage((msg) => {
    if (msg.to === 'pb') clientAdapter._deliver(msg);
  });
  clientAdapter.onMessage((msg) => {
    if (msg.to === 'pa') hostAdapter._deliver(msg);
  });

  const pcs = [];
  const RTCPeerConnection = class extends MockPeerConnection {
    constructor(...args) {
      super(...args);
      pcs.push(this);
      if (pcs.length === 2) pcs[0].linkTo(pcs[1]);
    }
  };

  const hostTransport = new p2pkit.RTCTransport({
    self: 'pa',
    remote: 'pb',
    signalling: hostAdapter,
    backend: { RTCPeerConnection },
    initiator: p2pkit.isInitiator('pa', 'pb'),
  });
  const clientTransport = new p2pkit.RTCTransport({
    self: 'pb',
    remote: 'pa',
    signalling: clientAdapter,
    backend: { RTCPeerConnection },
    initiator: p2pkit.isInitiator('pb', 'pa'),
  });

  await waitFor(() => hostSent.some(isDescriptionOffer) || clientSent.some(isDescriptionOffer), 3000, 'offer envelope');
  const offerWire = hostSent.find(isDescriptionOffer) || clientSent.find(isDescriptionOffer);
  assert.equal(typeof offerWire.from, 'string');
  assert.equal(typeof offerWire.to, 'string');
  assert.equal(offerWire.description.type, 'offer');

  await waitFor(() => pcs.length === 2, 3000, 'peer connections');
  const initiatorPc = pcs.find((pc) => pc._channels.length > 0) || pcs[0];
  initiatorPc.openAllChannels();

  hostTransport.disconnect();
  clientTransport.disconnect();
});
