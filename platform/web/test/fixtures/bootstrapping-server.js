// src/server.ts
import { createServer } from "node:http";
import { pathToFileURL } from "node:url";
import { WebSocketServer } from "ws";
var DEFAULTS = {
  host: "127.0.0.1",
  port: 8788,
  maxMessageBytes: 256 * 1024,
  // cap on one incoming text frame
  wsMaxPayloadBytes: 2 * 1024 * 1024,
  // transport cap (room for a too_large error)
  rateLimit: { max: 120, windowMs: 5e3, strikeLimit: 3 },
  // sig frames per socket
  maxQueue: 200,
  // waiting finders beyond this -> lobby_full
  pingIntervalMs: 3e4
  // WS protocol keepalive
};
function envInt(name) {
  const raw = process.env[name];
  if (!raw) return void 0;
  const value = Number(raw);
  return Number.isFinite(value) ? value : void 0;
}
function originAllowed(req) {
  const origin = req.headers.origin;
  if (!origin) return true;
  let originHost;
  try {
    originHost = new URL(origin).host.toLowerCase();
  } catch {
    return false;
  }
  const hostname = originHost.split(":")[0];
  return originHost === String(req.headers.host ?? "").toLowerCase() || hostname === "localhost" || hostname === "127.0.0.1" || hostname === "[::1]";
}
function createSignalingServer(userOptions = {}) {
  const options = {
    ...DEFAULTS,
    ...userOptions,
    rateLimit: { ...DEFAULTS.rateLimit, ...userOptions.rateLimit ?? {} }
  };
  const queue = [];
  const states = /* @__PURE__ */ new Set();
  const send = (state, payload) => {
    if (state.ws.readyState === state.ws.OPEN) state.ws.send(JSON.stringify(payload));
  };
  const sendError = (state, code) => send(state, { t: "error", code });
  function stats() {
    let paired = 0;
    for (const state of states) if (state.partner) paired += 1;
    return { waiting: queue.length, pairs: paired / 2, peers: states.size };
  }
  const httpServer = createServer((req, res) => {
    res.writeHead(404, { "content-type": "application/json; charset=utf-8" });
    res.end(JSON.stringify({ error: "not found" }));
  });
  const wss = new WebSocketServer({ noServer: true, maxPayload: options.wsMaxPayloadBytes });
  httpServer.on("upgrade", (req, socket, head) => {
    if (!originAllowed(req)) {
      socket.write("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit("connection", ws, req));
  });
  wss.on("connection", (ws) => {
    const state = {
      ws,
      partner: null,
      queued: false,
      rate: { count: 0, windowStart: Date.now(), strikes: 0 },
      alive: true
    };
    states.add(state);
    ws.on("pong", () => {
      state.alive = true;
    });
    ws.on("message", (data) => handleIncoming(state, data));
    ws.on("close", () => handleDisconnected(state));
    ws.on("error", () => {
    });
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
    if (++r.strikes >= strikeLimit) state.ws.close(1008, "rate limit exceeded");
    return true;
  }
  function handleIncoming(state, data) {
    const text = data.toString("utf8");
    let message = null;
    if (Buffer.byteLength(text, "utf8") > options.maxMessageBytes) return sendError(state, "too_large");
    try {
      message = JSON.parse(text);
    } catch {
    }
    if (message === null || typeof message !== "object" || Array.isArray(message)) {
      return sendError(state, "invalid_message");
    }
    const frame = message;
    switch (frame.t) {
      case "find":
        return handleFind(state);
      case "cancel":
        return handleCancel(state);
      case "sig":
        if (sigRateLimited(state)) return sendError(state, "rate_limited");
        if (state.partner) send(state.partner, { t: "sig", data: frame.data });
        return;
      default:
        return sendError(state, "unknown_type");
    }
  }
  function handleFind(state) {
    if (state.queued || state.partner) return;
    const waiter = queue.shift();
    if (!waiter) {
      if (queue.length >= options.maxQueue) return sendError(state, "lobby_full");
      state.queued = true;
      queue.push(state);
      return send(state, { t: "waiting" });
    }
    waiter.queued = false;
    waiter.partner = state;
    state.partner = waiter;
    send(waiter, { t: "matched", role: "host" });
    send(state, { t: "matched", role: "joiner" });
  }
  function handleCancel(state) {
    if (!state.queued) return;
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
      send(partner, { t: "peer_left" });
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
  }, Math.max(1e3, options.pingIntervalMs));
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
      wss.close(() => {
      });
      httpServer.close(() => resolve());
      httpServer.closeIdleConnections?.();
    });
  }
  return { httpServer, wss, queue, states, options, stats, close };
}
var entry = process.argv[1];
var isMain = entry !== void 0 && import.meta.url === pathToFileURL(entry).href;
function main() {
  const port = envInt("PORT") ?? DEFAULTS.port;
  const host = process.env.HOST || DEFAULTS.host;
  const ctx = createSignalingServer();
  ctx.httpServer.listen(port, host, () => {
    const address = ctx.httpServer.address();
    const boundPort = typeof address === "object" && address ? address.port : port;
    console.log(`p2pkit-bootstrap listening on ws://${host}:${boundPort}/`);
  });
  const shutdown = () => ctx.close().then(() => process.exit(0));
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}
if (isMain) {
  main();
}
export {
  DEFAULTS,
  createSignalingServer
};
