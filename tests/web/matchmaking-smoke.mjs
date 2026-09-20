// Optional real-browser acceptance test. Requires Playwright and Chrome.
// Run after tools/web/build-emscripten.sh; no public lobby or user profile is used.
import assert from 'node:assert/strict';
import { readFileSync, existsSync, createReadStream, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, extname, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { request } from 'node:http';
import { createInterface } from 'node:readline';
import { once } from 'node:events';
import { createSignalingServer } from '../../platform/web/test/fixtures/bootstrapping-server.js';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const artifacts = resolve(process.env.WEB_BUILD_DIR || resolve(root, 'build/emscripten/bin'));
const output = resolve(process.env.WEB_TEST_OUTPUT || resolve(root, 'build/matchmaking-smoke'));
mkdirSync(output, {recursive: true});
const server = createSignalingServer();
let contentPort;
const contentRequests = [];
server.httpServer.removeAllListeners('request');
server.httpServer.on('request', (req, res) => {
  const pathname = new URL(req.url, 'http://localhost').pathname;
  if (pathname.startsWith('/v1/')) {
    const upstream = request({hostname: '127.0.0.1', port: contentPort,
      path: req.url, method: req.method, headers: req.headers}, reply => {
      contentRequests.push({path: pathname, status: reply.statusCode});
      res.writeHead(reply.statusCode, reply.headers); reply.pipe(res);
    });
    upstream.on('error', error => { res.writeHead(502); res.end(String(error)); });
    req.pipe(upstream); return;
  }
  const file = resolve(artifacts, '.' + pathname);
  if (!file.startsWith(artifacts + sep) || !existsSync(file) || pathname.endsWith('/')) {
    res.writeHead(404); res.end(); return;
  }
  res.setHeader('Content-Type', ({'.html':'text/html', '.js':'text/javascript', '.css':'text/css', '.wasm':'application/wasm'})[extname(file)] || 'application/octet-stream');
  createReadStream(file).pipe(res);
});
await new Promise(resolve => server.httpServer.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.httpServer.address().port}`;
const service = spawn(process.env.PYTHON || 'python3', [resolve(root, 'tests/web/workshop-service-fixture.py'), origin],
  {stdio: ['pipe', 'pipe', 'inherit']});
const serviceExit = once(service, 'exit');
const serviceLines = createInterface({input: service.stdout});
let browser;
const errors = [];
const consoleMessages = [];
const pages = [];
const largeMod = process.env.WORKSHOP_LARGE_MOD === '1';
const stats = page => page.evaluate(() => {
  const {role, peerConnectionState, channels} = Module.dunecityWebrtcStats();
  return {role, peerConnectionState, channels};
});
// The fixture sets a fixed 1280x720 game viewport, with the 64px browser toolbar.
const click = async (page, x, y) => { await page.mouse.click(x, y); await page.waitForTimeout(250); };
try {
  const startup = await Promise.race([
    once(serviceLines, 'line').then(([line]) => JSON.parse(line)),
    serviceExit.then(([code]) => { throw new Error(`Content fixture exited during startup (${code})`); }),
  ]);
  contentPort = startup.port;
  browser = await chromium.launch({headless: true, channel: process.env.BROWSER_CHANNEL || 'chrome', args: ['--autoplay-policy=no-user-gesture-required']});
  for (let i = 0; i < 2; ++i) {
    const context = await browser.newContext({viewport: {width: 1280, height: 800}});
    const page = await context.newPage(); pages.push(page);
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => { consoleMessages.push(message.text()); if(consoleMessages.length > 200) consoleMessages.shift(); });
    await page.addInitScript(() => {
      window.DUNECITY_WEBRTC_CONFIG = {iceServers: []};
      window.longGameSleeps = [];
      const schedule = window.setTimeout;
      window.setTimeout = function(callback, delay, ...args) {
        if (delay > 50 && new Error().stack.includes('_emscripten_sleep')) {
          window.longGameSleeps.push(delay);
        }
        return schedule(callback, delay, ...args);
      };
    });
    await page.route('**/shell.js', async route => {
      let script = readFileSync(resolve(artifacts, 'shell.js'), 'utf8');
      const setup = `FS.mkdirTree('/home/web_user/.config/DuneCity');
        FS.writeFile('/home/web_user/.config/DuneCity/dunecity-first-launch.done','test');
        FS.writeFile('/home/web_user/.config/DuneCity/Dune City.ini',
          '[General]\\nPlay Intro = false\\nPlayer Name = Browser${i}\\n[Video]\\nPhysical Width = 1280\\nPhysical Height = 720\\nFullscreen = false\\n');`;
      assert.ok(script.includes("removeRunDependency('dunecity-idbfs');"));
      script = script.replace("removeRunDependency('dunecity-idbfs');", setup + "removeRunDependency('dunecity-idbfs');");
      await route.fulfill({body: script, contentType: 'text/javascript'});
    });
    await page.goto(`${origin}/dunecity.html?relay=${encodeURIComponent(origin)}&relaydev=1`);
    await page.waitForFunction(() => document.querySelector('#loading').hidden, null, {timeout: 90000});
    await click(page, 640, 288); // Play Online
    await click(page, 1150, 762); // Find Match screen
  }
  const [host, guest] = pages;
  if (largeMod) await host.evaluate(() => {
    // Exceed the peer-transfer ceiling so success requires the community path.
    const fs = Module.FS;
    const directory = '/home/web_user/.config/DuneCity/mods/vanilla/graphics';
    fs.mkdirTree(directory);
    const bytes = new Uint8Array(11 * 1024 * 1024);
    for(let i=0; i<bytes.length; ++i) bytes[i] = i % 251;
    fs.writeFile(directory + '/workshop-browser-probe.bin', bytes);
  });
  await click(host, 80, 184); // Queue
  await host.waitForFunction(() => Module.dunecityWebrtcStats().role === 'finding');
  await click(host, 212, 184); // Cancel and retry
  await host.waitForFunction(() => Module.dunecityWebrtcStats().peerConnectionState === 'idle');
  await click(host, 80, 184);
  await click(guest, 80, 184);
  for (const page of pages) await page.waitForFunction(() => Module.dunecityWebrtcStats().peerConnectionState === 'connected', null, {timeout: 30000});
  assert.equal((await stats(host)).role, 'host');
  await host.screenshot({path: resolve(output, 'host-map.png')});
  await click(host, 1112, 748); // Default two-player map -> Players
  await guest.waitForFunction(() => Module.dunecityWebrtcStats().messages.some(m => m.dir === 'recv' && m.packetId === 4), null, {timeout: 120000});
  assert.ok(contentRequests.filter(r => r.path === '/v1/content/commit' && r.status === 200).length >= 2,
    'Find Match must publish the exact mod and map before sending game setup');
  await host.waitForFunction(() => Module.dunecityWebrtcStats().messages.some(m => m.dir === 'recv' && m.packetId === 18), null, {timeout: 120000});
  if (largeMod) {
    assert.ok(contentRequests.some(r => r.path === '/v1/content/blob' && r.status === 200),
      'A missing large mod must download from the community service');
    assert.equal(await guest.evaluate(() => {
      const fs = Module.FS, root = '/home/web_user/.config/DuneCity/mods';
      for(const name of fs.readdir(root).filter(name => name.startsWith('ws-'))) {
        const path = root + '/' + name + '/graphics/workshop-browser-probe.bin';
        if(!fs.analyzePath(path).exists) continue;
        const bytes = fs.readFile(path);
        return bytes.length === 11 * 1024 * 1024 && bytes.every((byte,i) => byte === i % 251);
      }
      return false;
    }), true, 'Guest must install the complete exact asset bytes');
  }
  await guest.screenshot({path: resolve(output, 'guest-lobby.png')});
  await click(host, 1105, 748); // Start Game
  for (const page of pages) await page.waitForFunction(() => Module.dunecityWebrtcStats().channels[1].sent > 300, null, {timeout: 60000});
  const before = await Promise.all(pages.map(stats));
  await guest.waitForTimeout(10000);
  const after = await Promise.all(pages.map(stats));
  for (let i = 0; i < 2; ++i) {
    assert.ok(after[i].channels[1].sent > before[i].channels[1].sent + 200);
    assert.ok(after[i].channels[1].received > before[i].channels[1].received + 200);
    assert.equal(after[i].channels[1].dropped, 0);
    await pages[i].screenshot({path: resolve(output, `game-${i}.png`)});
  }
  // Exercise the real client teardown, including the nested-menu pacing regression.
  await guest.evaluate(() => { window.longGameSleeps = []; });
  await guest.keyboard.press('Escape'); await guest.waitForTimeout(300);
  await click(guest, 640, 500); // Quit to Menu
  await click(guest, 595, 436); // Confirm
  await guest.waitForFunction(() => Module.dunecityWebrtcStats().peerConnectionState === 'idle', null, {timeout: 10000});
  await guest.waitForTimeout(1000);
  assert.deepEqual(await guest.evaluate(() => window.longGameSleeps), [], 'Nested match duration must not become a browser sleep');
  await guest.screenshot({path: resolve(output, 'guest-after-quit.png')});
  const savedMaps = await guest.evaluate(() => {
    const fs=Module.FS, root='/home/web_user/.config/DuneCity/maps/multiplayer';
    return fs.readdir(root).filter(name => name.endsWith('.ini')).sort()
      .map(name => ({name,bytes:Array.from(fs.readFile(root+'/'+name))}));
  });
  assert.ok(savedMaps.some(map => map.name.endsWith('.workshop.ini')), 'Received map needs revision metadata');
  await guest.reload();
  await guest.waitForFunction(() => document.querySelector('#loading').hidden, null, {timeout:90000});
  assert.deepEqual(await guest.evaluate(() => {
    const fs=Module.FS, root='/home/web_user/.config/DuneCity/maps/multiplayer';
    return fs.readdir(root).filter(name => name.endsWith('.ini')).sort()
      .map(name => ({name,bytes:Array.from(fs.readFile(root+'/'+name))}));
  }), savedMaps, 'Received map and its numbered metadata must survive a browser reload');
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, 'results.json'), JSON.stringify({before, after, errors, largeMod, contentRequests, longGameSleeps: [], clientQuit: true, savedMapReload: true}, null, 2));
  console.log('PASS: real browser pairing, cancel/retry, lobby/start, command exchange, and client quit');
} catch (error) {
  writeFileSync(resolve(output, 'failure.json'), JSON.stringify({error: String(error), errors, consoleMessages, contentRequests}, null, 2));
  for (let i=0; i<pages.length; ++i) await pages[i].screenshot({path: resolve(output, `failure-${i}.png`)}).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close();
  await server.close();
  service.stdin.end();
  await serviceExit;
  serviceLines.close();
}
