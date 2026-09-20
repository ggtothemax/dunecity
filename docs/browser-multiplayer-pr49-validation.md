# Browser multiplayer review — PR 49

Reviewed on 20 September 2026. Final candidate 1.0.737 includes released main
f836940 (1.0.735) and all active game branch histories; see
`branch-consolidation-736.md`. Earlier measurements below retain their versions.

## Corrections

- Browser client `connectPeer` aliases an entry in the admission or established
  peer list. Original cleanup freed it twice. All aliases are now removed before
  freeing either allocation; repeated teardown is safe.
- Local rejection used to depend on an SDK Disconnect event that `disconnect()`
  itself discards. Deferred local cleanup now retains packet-adapter borrows until
  handling returns, then frees the peer and reports the cause exactly once.
- Browser messages now pass the existing incoming-byte budget before parsing.
- Corrected the rejected-packet log's missing vararg and the room-session null
  transport query. Duplicate Connect events cannot overwrite owned peer data.
- Cancel handles both queued and already paired clients. Leaving map selection
  releases the pair; a new match resets role, phase, seed and mod-transfer state.
  Restored the missing Back button in the browser matchmaking layout.
- Capped menu frame pacing at 50 ms. A callback can contain a whole game; the
  original code scheduled a measured 484,078 ms browser sleep after returning
  from a match, leaving a blank screen. The real-browser smoke test records
  Emscripten sleep timers and exercises the actual quit path.
- Resolved merge conflicts while retaining current updater, city metadata,
  observer protections, hot-join checkpoint recovery and named join UI. New room packets use the shared
  packet stream API so the merged browser build compiles.
- Preserved bounded ICE failure diagnostics in the direct-play adapter when
  replacing the patched vendored SDK with the pinned package.
- Fixed incremental SDK prepending, added link dependencies, HTTPS dependency
  resolutions, build/service documentation and a pinned upstream fixture check.
  CI runs browser glue tests and the wasm32 AddressSanitizer lifecycle harness.

## Local verification

- Full Apple silicon native build, including native direct WebRTC; Ninja
  dependency audit before and after the build.
- Eight CTest groups pass, including the real SDL menu-navigation probe. The main
  suite reports 770 passed and three skipped cases; no failed cases.
- Full browser build using the repository's pinned Emscripten 4.0.14. Source and
  generated-JS verifiers pass; bundled mod audit verifies 769 Tornie and six
  Dune2R files. A second incremental build leaves `dunecity.js` byte-identical.
- Browser JS suites: 29 passed. Direct SDK/bridge suites: 18 passed. Nine framing
  cross-checks and four web-build safety checks pass.
- The standalone wasm32 ASan harness links the production peer-lifetime methods;
  only game construction and the JS socket ABI are fixtures. It covers host and
  client cleanup before/after admission, orphan host aliases, repeated cleanup,
  reconnection, deferred rejection, remote/local event races, and callbacks that
  reenter cleanup. Fixed code passes with no sanitizer report. Substituting the
  original PR's `clearAllPeers` produces `AddressSanitizer: heap-use-after-free`.
- Two isolated real Chromium contexts load the built game with loopback signaling
  and host-only ICE. Cancel/retry succeeds; both clients pair and enter the
  Habbanya-Penny 128x128 map. The host reached 3,992 sent / 3,993 received command
  packets with zero reported drops while both games remained connected.
- The 1.0.734 build passes the automated real-browser acceptance test in
  `tests/web/matchmaking-smoke.mjs`, including the guest quitting back to Play
  Online promptly, with no browser errors or Emscripten sleeps over 50 ms.
  Screenshots and counters are written under `build/matchmaking-smoke/`.
- The matchmaking server fixture matches the immutable upstream source byte for
  byte after its provenance header; its pin and SHA-256 are checked offline too.

## Refresh after main advanced

The combined 1.0.735 native and browser builds pass. All eight native CTest
groups and all 193 signaling tests pass again. The real three-peer hot-join
replacement probe resumes with matching state at cycle 150. The wasm32 ASan
lifecycle harness and automated two-browser pairing/play/quit test pass again.
The PR description records CI status for the pushed revision.

## Production acceptance on the combined 1.0.736 candidate

Native and Emscripten builds pass; eight CTest groups pass (772 passed, three
skipped in the main suite). The wasm32 ASan lifecycle harness passes again.
Three real peers resume with matching simulation state at cycle 150 after a
hot-join replacement, including the integrated PR28 command-pacing change.

The matcher is now deployed separately from the room directory, from website
commit a2ed864 (website PR12). Two isolated Chromium profiles load the candidate
at the production origin by local request interception; no public game files are
replaced during the test. They use the shipping default public WSS endpoint and
STUN configuration. Find Match, queue cancellation/retry, pairing, host map
selection, guest admission, start, two-way chat, army movement and guest quit
pass. Each client exchanges over 940 command packets with zero reported drops;
there are no browser errors or menu sleeps over 50 ms. Candidate evidence is in
`build/public-matchmaking-736/` (results, selected ICE paths and screenshots).

The new service passes a controlled restart and health recovery. Existing `/p2p`
room health remains OK. Both `/` and `/matchmaking` accept public TLS WebSocket
connections; unrelated browser origins are rejected.

## Limits and release gates

Both gameplay clients run on one machine/network and select host/UDP ICE paths.
This verifies public signaling and real gameplay, not connectivity across every
NAT. There is no default TURN relay. Matchmaking trusts its signaling server and
does not use the direct-room fingerprint admission protocol. Live game assets
remain 735 until the separate 736 publication completes. Evaluate CI on the final
pushed candidate and tag the resulting main merge, then verify the actual public
browser build and desktop artifacts separately.

## Final 1.0.737 integration and publication-path regression

The final tree additionally incorporates the locally committed public-campaign
branch through a50938a. Native and raw CMake Emscripten builds pass, as do all
eight CTest groups. Campaign probes verify online/public defaults, Offline
selection, solo host readiness and the absence of a phantom second controller.
The explanation fits at 640x480 and the larger tested window sizes.

A raw CMake rebuild exposed that only the optional shell wrapper prepended the
P2PKit runtime. Real public pairing then failed with RTCTransport unavailable.
CMake now packages that runtime after every link. The separate website workflow
installs the pinned SDK and invokes the same verified build wrapper as game CI.
The fixed raw-build output passes the public two-browser game test, including
chat, movement and quit. An unchanged incremental CMake build followed by the
wrapper's packaging leaves the runtime byte-identical. All 29 browser glue tests,
four build-safety checks, generated-JS verification and bundled-mod checks pass.
Evidence: `build/public-matchmaking-737/`.

The real solo-host spectator promotion test declines the first play request,
retries it, accepts it into shared control, and resumes with matching state at
cycle 1800. Its stale anonymous-label assertions were updated to the named
approval notices already present in main. The existing roster and state checks
remain in place. Evidence: `build/pr49-solo-promote-737-retry/`.
