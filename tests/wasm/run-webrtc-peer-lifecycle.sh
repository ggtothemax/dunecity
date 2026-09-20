#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${ROOT}/build/webrtc-lifecycle"
mkdir -p "$OUT"
em++ -std=c++17 -O1 -g -fno-access-control -fsanitize=address \
    -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sUSE_SDL_TTF=2 \
    -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sENVIRONMENT=node \
    -I"${ROOT}/include" -I"${ROOT}/platform/web/node_modules/p2pkit/emscripten/include" \
    "${ROOT}/tests/wasm/WebRtcPeerLifecycleHarness.cpp" \
    "${ROOT}/src/Network/WebRtcPeerLifecycle.cpp" \
    -o "${OUT}/peer-lifecycle.js"
node "${OUT}/peer-lifecycle.js"
