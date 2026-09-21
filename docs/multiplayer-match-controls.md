# In-game multiplayer controls

Local candidate 1.0.748 includes Pause/Resume to the top bar and multiplayer Options
menu. Space uses the same action. Every human player occupying a playing seat
can pause or resume, regardless of which player paused first. Spectators cannot.
Opening the host's Options menu (including Escape) requests a shared pause.
Guest and spectator menus stay local. Back to Game and Escape resume a pause
created by opening the host menu, including a quick close before its pause command
arrives. A pre-existing manual pause stays paused. Any active player can still
use Resume. The menu
updates its pause status and button while open.

Game Settings is available during multiplayer. The host can change game speed
there or with the existing plus/minus shortcuts. Other players see the current
speed with disabled controls labelled “host only”. Sound volume, scroll speed,
and credits sound remain personal settings. Multiplayer load/restart remain
unchanged; this does not introduce a shared mid-match restart or load operation.

## Synchronization

- `CMD_MATCH_PAUSE(requestCycle)` travels through the existing authorized
  lockstep command stream. Every participant finishes the command's tick and
  pauses at the following boundary. Overlapping requests submitted before that
  boundary coalesce, including commands still queued when play resumes.
- Resume travels outside the frozen simulation to the host, bound to the
  sender's established connection identity. The host validates the human seat
  and pause epoch, then broadcasts an authoritative revision. A resume received
  before a lagging client's pause command prevents that client becoming stuck.
- Game speed controls wall-clock milliseconds per fixed simulation tick. The
  host distributes it together with pause state; it does not change physics dt.
- Polling and controls continue while paused. Host heartbeats refresh state once
  per second. Direct player broadcasts exclude spectators, so established
  spectator streams receive controls separately, tagged with their snapshot
  epoch. A stalled spectator never blocks the active players.
- Observer runtime v4 includes speed and pause state. The reader also accepts
  runtime v3. A spectator joining an already-paused match inherits that state.
- Network protocol 11 prevents mixed older/new control implementations sharing
  a match. Everyone testing together needs the new app. Replay playback ignores
  the waiting time represented by pause commands.

## Verification

Standard checks use `python3 scripts/check-build-deps.py build` and
`ctest --test-dir build --output-on-failure`. Relay routing is checked with
`npm test` in `tools/room-relay` after `npm ci`.

The real-engine three-process regression uses the isolated local PHP service,
WebRTC channels, actual command execution and observer checkpoints:

```sh
JOIN_MATCH_CONTROLS=1 JOIN_AT_CYCLE=700 JOIN_CAPTURE_UI=1 \
  python3 tests/network/run-late-join-probe.py --city --mode spectate \
  --output-dir /tmp/dunecity-match-controls
```

It covers client pause/host resume, simultaneous requests/client resume,
host-only speed edits while paused, host-menu auto-pause, guest-menu isolation,
menu-close semantics, a spectator joining
while paused, spectator authority restrictions, malformed control packets,
and matching world digests after resuming and after spectator departure.
Screenshots include the toolbar, multiplayer Options and Game Settings.
This native probe is not an interactive browser-crossplay or ENet LAN test.
