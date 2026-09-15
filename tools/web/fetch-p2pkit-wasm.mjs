#!/usr/bin/env node
/**
 * Fetches the vendored p2pkit-wasm SDK from QuixThe2nd/p2pkit-wasm via the
 * GitHub tarball API (git-subrepo style: the fetched copy is COMMITTED here;
 * re-run this after bumping the ref, then commit the result).
 *
 *   include/p2pkit-wasm/*.h            -> include/p2pkit-wasm/            (C++ SDK headers)
 *   js/p2pkit_webrtc_glue.js           -> platform/web/p2pkit-wasm-glue.js (generic --js-library)
 *   dist/p2pkit.iife.js                -> platform/web/dist/p2pkit.iife.js (committed p2pkit bundle)
 *   test/support/lobby-server.mjs      -> platform/web/test/support/p2pkit-wasm-lobby-server.mjs
 *
 * The fetched commit sha is recorded in platform/web/p2pkit-wasm.ref and used
 * as the default ref on the next run (pass --ref <sha|branch> to override).
 *
 * Usage:
 *   node tools/web/fetch-p2pkit-wasm.mjs [--ref <sha-or-branch>] [--only-bundle]
 *
 * Requires: gh (authenticated or not; the repo is public), tar.
 * Set P2PKIT_SKIP_BUILD=1 (or legacy P2PKIT_SKIP_INSTALL=1) to make a missing
 * bundle a hard error instead of fetching (offline sandboxes).
 */
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const REPO = 'QuixThe2nd/p2pkit-wasm';
const REF_FILE = path.join(ROOT, 'platform', 'web', 'p2pkit-wasm.ref');

const FILES = [
  ['include/p2pkit-wasm/webrtc_transport.h', 'include/p2pkit-wasm/webrtc_transport.h'],
  ['include/p2pkit-wasm/packet.h', 'include/p2pkit-wasm/packet.h'],
  ['include/p2pkit-wasm/packet_view.h', 'include/p2pkit-wasm/packet_view.h'],
  ['include/p2pkit-wasm/transport_types.h', 'include/p2pkit-wasm/transport_types.h'],
  ['js/p2pkit_webrtc_glue.js', 'platform/web/p2pkit-wasm-glue.js'],
  ['dist/p2pkit.iife.js', 'platform/web/dist/p2pkit.iife.js'],
  ['test/support/lobby-server.mjs', 'platform/web/test/support/p2pkit-wasm-lobby-server.mjs'],
];

function fail(message) {
  console.error(`ERROR: ${message}`);
  process.exit(1);
}

const args = process.argv.slice(2);
let ref = null;
let onlyBundle = false;
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--ref' && args[i + 1]) ref = args[++i];
  else if (args[i] === '--only-bundle') onlyBundle = true;
  else fail(`unknown argument: ${args[i]}`);
}

const targets = onlyBundle ? FILES.filter(([, to]) => to.endsWith('dist/p2pkit.iife.js')) : FILES;

function run(cmd, cmdArgs, opts = {}) {
  const result = spawnSync(cmd, cmdArgs, { encoding: 'utf8', ...opts });
  if (result.status !== 0) {
    fail(`${cmd} ${cmdArgs.join(' ')} failed (exit ${result.status}): ${(result.stderr || result.stdout || '').trim()}`);
  }
  return result;
}

function resolveRef() {
  const pinned = ref || (fs.existsSync(REF_FILE) ? fs.readFileSync(REF_FILE, 'utf8').trim() : null);
  const query = pinned || 'HEAD';
  const sha = run('gh', ['api', `repos/${REPO}/commits/${encodeURIComponent(query)}`, '--jq', '.sha']).stdout.trim();
  if (!/^[0-9a-f]{40}$/.test(sha)) fail(`could not resolve ${REPO}@${query} to a commit sha`);
  return sha;
}

const sha = resolveRef();

// The offline escape hatch: never touch the network, just verify the vendored
// copy is present (used by tools/web/build-p2pkit-iife.mjs in sandboxes).
if (process.env.P2PKIT_SKIP_BUILD || process.env.P2PKIT_SKIP_INSTALL) {
  const missing = targets.filter(([, to]) => !fs.existsSync(path.join(ROOT, to)));
  if (missing.length > 0) {
    fail(
      `P2PKIT_SKIP_BUILD is set and the vendored p2pkit-wasm copy is missing:\n` +
        missing.map(([, to]) => `       ${to}`).join('\n') +
        `\n       Run: node tools/web/fetch-p2pkit-wasm.mjs`,
    );
  }
  console.log(`OK: vendored p2pkit-wasm copy present (offline check, ${REPO}@${fs.existsSync(REF_FILE) ? fs.readFileSync(REF_FILE, 'utf8').trim() : 'unknown ref'})`);
  process.exit(0);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'p2pkit-wasm-'));
try {
  console.log(`==> fetching ${REPO}@${sha.slice(0, 12)} tarball`);
  const tarball = run('gh', ['api', `repos/${REPO}/tarball/${sha}`], { encoding: 'buffer' }).stdout;
  if (!(tarball.length > 0 && tarball[0] === 0x1f)) fail('tarball download did not return gzip data (auth or rate limit?)');
  fs.writeFileSync(path.join(tmp, 'sdk.tar.gz'), tarball);
  run('tar', ['-xzf', path.join(tmp, 'sdk.tar.gz'), '-C', tmp]);
  // GitHub tarballs extract under '<owner>-<repo>-<sha>/'; there is exactly one.
  const extracted = fs.readdirSync(tmp).filter((name) => name !== 'sdk.tar.gz');
  if (extracted.length !== 1) fail(`unexpected tarball layout: [${extracted.join(', ')}]`);
  const srcRoot = path.join(tmp, extracted[0]);

  for (const [from, to] of targets) {
    const src = path.join(srcRoot, from);
    if (!fs.existsSync(src)) fail(`tarball is missing ${from} (upstream layout changed?)`);
    const dst = path.join(ROOT, to);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.copyFileSync(src, dst);
    console.log(`    ${to}`);
  }

  fs.writeFileSync(REF_FILE, `${sha}\n`);
  console.log(`OK: vendored p2pkit-wasm updated to ${sha}`);
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
