#!/usr/bin/env node
/**
 * Fetches the committed p2pkit IIFE bundle for the browser build.
 *
 * The bundle (and the rest of the vendored p2pkit-wasm SDK) is built and
 * committed in QuixThe2nd/p2pkit-wasm; dunecity no longer builds it from the
 * npm p2pkit dependency. This script is now a thin wrapper around
 * tools/web/fetch-p2pkit-wasm.mjs --only-bundle, kept under its historical
 * name because build-emscripten.sh and platform/web/test/ensure-p2pkit-bundle.cjs
 * invoke it when platform/web/dist/p2pkit.iife.js is missing.
 *
 * Set P2PKIT_SKIP_BUILD=1 (or legacy P2PKIT_SKIP_INSTALL=1) to make a missing
 * bundle a hard error instead of fetching (offline sandboxes).
 */
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const FETCH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), 'fetch-p2pkit-wasm.mjs');
const result = spawnSync(process.execPath, [FETCH, '--only-bundle'], { stdio: 'inherit' });
process.exit(result.status === null ? 1 : result.status);
