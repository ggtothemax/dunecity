// Vendored from QuixThe2nd/p2pkit bootstrapping-server/server.js
// (master@1ee02b3c6590d8e96249eddf2090817b431c5864) so the integration tests
// run the real lobby logic with no p2pkit checkout. Keep in sync when the
// lobby protocol changes.

import { createServer } from 'node:http';
import { pathToFileURL } from 'node:url';
import { WebSocketServer } from 'ws';

export const DEFAULTS = {
  host: '127.0.0.1',
  port: 8788,
  maxMessageBytes: 256 * 1024,          // cap on one incoming text frame
  wsMaxPayloadBytes: 2 * 1024 * 1024,   // transport cap (room for a too_large error)
  rateLimit: { max: 120, windowMs: 5_000, strikeLimit: 3 }, // sig frames per socket
  maxQueue: 200,                        // waiting finders beyond this -> lobby_full
  pingIntervalMs: 30_000,               // WS protocol keepalive
};

function envInt(name) {
  const value = Number(process.env[name]);
  return process.env[name] && Number.isFinite(value) ? value : undefined;
}

// Allow non-browser clients (no Origin), same-host pages, and localhost.
function originAllowed(req) {
  const origin = req.headers.origin;
  if (!origin) return true;
  let originHost;
  try {
    originHost = new URL(origin).host.toLowerCase();
  } catch {
    return false;
  }
  const hostname = originHost.split(':')[0];
  return originHost === String(req.headers.host ?? '').toLowerCase()
    || hostname === 'localhost' || hostname === '127.0.0.1' || hostname === '[::1]';
}

export function createSignalingServer(userOptions = {}) {
  const options = {
    ...DEFAULTS,
    ...userOptions,
    rateLimit: { ...DEFAULTS.rateLimit, ...(userOptions.rateLimit ?? {}) },
  };
  // Global FIFO of waiting clients. state = { ws, partner, queued, rate, alive }.
  const queue = [];
  const states = new Set();
  const send = (state, payload) => {
    if (state.ws.readyState === state.ws.OPEN) state.ws.send(JSON.stringify(payload));
  };
  const sendError = (state, code) => send(state, { t: 'error', code });

  function stats() {
    let paired = 0;
    for (const state of states) if (state.partner) paired += 1;
    return { waiting: queue.length, pairs: paired / 2, peers: states.size };
  }

  const httpServer = createServer((req, res) => {
    res.writeHead(404, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'not found' }));
  });
  const wss = new WebSocketServer({ noServer: true, maxPayload: options.wsMaxPayloadBytes });
  httpServer.on('upgrade', (req, socket, head) => {
    if (!originAllowed(req)) {
      socket.write('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
      socket.destroy();
      return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit('connection', ws, req));
  });
  wss.on('connection', (ws) => {
    const state = { ws, partner: null, queued: false, rate: { count: 0, windowStart: Date.now(), strikes: 0 }, alive: true };
    states.add(state);
    ws.on('pong', () => { state.alive = true; });
    ws.on('message', (data) => handleIncoming(state, data));
    ws.on('close', () => handleDisconnected(state));
    ws.on('error', () => { /* close handler cleans up */ });
  });

  function sigRateLimited(state) {
    const { max, windowMs, strikeLimit } = options.rateLimit;
    const r = state.rate;
    const at = Date.now();
    if (at - r.windowStart >= windowMs) {
      r.windowStart = at;
      r.count = 0;
      r.strikes = 0;
    }
    if (++r.count <= max) return false;
    if (++r.strikes >= strikeLimit) state.ws.close(1008, 'rate limit exceeded');
    return true;
  }

  function handleIncoming(state, data) {
    const text = data.toString('utf8');
    let message = null;
    if (Buffer.byteLength(text, 'utf8') > options.maxMessageBytes) return sendError(state, 'too_large');
    try {
      message = JSON.parse(text);
    } catch {
      /* handled by the shape check below */
    }
    if (message === null || typeof message !== 'object' || Array.isArray(message)) {
      return sendError(state, 'invalid_message');
    }
    switch (message.t) {
      case 'find':
        return handleFind(state);
      case 'cancel':
        return handleCancel(state);
      case 'sig':
        if (sigRateLimited(state)) return sendError(state, 'rate_limited');
        // Routed only while paired; forwarded verbatim, data is never parsed.
        if (state.partner) send(state.partner, { t: 'sig', data: message.data });
        return;
      default:
        return sendError(state, 'unknown_type');
    }
  }

  function handleFind(state) {
    if (state.queued || state.partner) return; // idempotent
    const waiter = queue.shift();
    if (!waiter) {
      if (queue.length >= options.maxQueue) return sendError(state, 'lobby_full');
      state.queued = true;
      queue.push(state);
      return send(state, { t: 'waiting' });
    }
    // Pair: the waiter hosts, the newcomer joins; both learn it in this tick.
    waiter.queued = false;
    waiter.partner = state;
    state.partner = waiter;
    send(waiter, { t: 'matched', role: 'host' });
    send(state, { t: 'matched', role: 'joiner' });
  }

  function handleCancel(state) {
    if (!state.queued) return; // a pairing is only left by closing the socket
    state.queued = false;
    const index = queue.indexOf(state);
    if (index !== -1) queue.splice(index, 1);
  }

  function handleDisconnected(state) {
    states.delete(state);
    if (state.queued) handleCancel(state);
    const partner = state.partner;
    state.partner = null;
    if (partner) {
      partner.partner = null;
      send(partner, { t: 'peer_left' });
    }
  }

  const pingTimer = setInterval(() => {
    for (const state of states) {
      if (!state.alive) {
        state.ws.terminate();
        continue;
      }
      state.alive = false;
      state.ws.ping();
    }
  }, Math.max(1_000, options.pingIntervalMs));
  pingTimer.unref?.();

  let closed = false;
  function close() {
    if (closed) return Promise.resolve();
    closed = true;
    clearInterval(pingTimer);
    for (const ws of wss.clients) ws.terminate();
    queue.length = 0;
    states.clear();
    return new Promise((resolve) => {
      wss.close(() => {});
      httpServer.close(() => resolve());
      httpServer.closeIdleConnections?.();
    });
  }

  return { httpServer, wss, queue, states, options, stats, close };
}

const isMain = process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

function main() {
  const port = envInt('PORT') ?? DEFAULTS.port;
  const host = process.env.HOST || DEFAULTS.host;
  const ctx = createSignalingServer();
  ctx.httpServer.listen(port, host, () => {
    const address = ctx.httpServer.address();
    const boundPort = typeof address === 'object' && address ? address.port : port;
    console.log(`p2pkit-bootstrap listening on ws://${host}:${boundPort}/`);
  });
  const shutdown = () => ctx.close().then(() => process.exit(0));
  process.once('SIGINT', shutdown);
  process.once('SIGTERM', shutdown);
}

if (isMain) {
  main();
}
