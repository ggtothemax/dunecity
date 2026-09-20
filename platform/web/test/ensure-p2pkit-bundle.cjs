'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '../../..');
const BUNDLE = path.join(ROOT, 'platform/web/dist/p2pkit.iife.js');
const BUILD_SCRIPT = path.join(ROOT, 'tools/web/build-p2pkit-iife.mjs');

let built = false;

function ensureP2pkitBundle() {
  if (fs.existsSync(BUNDLE) && fs.statSync(BUNDLE).size > 0) {
    return;
  }
  if (built) {
    throw new Error(`p2pkit bundle still missing after build: ${BUNDLE}`);
  }
  built = true;

  // The bundle is committed; this only runs after a checkout that lost it or a
  // pin bump. The build script installs the pinned package if needed.
  const result = spawnSync(process.execPath, [BUILD_SCRIPT], {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  if (result.status !== 0) {
    const detail = (result.stderr || result.stdout || '').trim();
    throw new Error(
      `failed to build p2pkit bundle via ${BUILD_SCRIPT}` +
        (detail ? `: ${detail}` : ` (exit ${result.status})`),
    );
  }

  if (!fs.existsSync(BUNDLE) || fs.statSync(BUNDLE).size === 0) {
    throw new Error(`p2pkit bundle missing or empty after build: ${BUNDLE}`);
  }
}

ensureP2pkitBundle();

module.exports = { ensureP2pkitBundle, BUNDLE };
