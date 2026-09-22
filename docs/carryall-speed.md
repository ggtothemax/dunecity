# Carryall speed reference

The shared carryall cruise-speed cap for Dune City, Vanilla and Dune2R is
15 tiles per second at the default 16 ms game cycle. This is Dune Dynasty's
nominal normal-speed cap, converted to DuneCity's world coordinates:

`15 tiles/s * 64 world units/tile * 0.016 s/cycle = 15.36 world units/cycle`.

Previously it was 19.2 units/cycle, or 18.75 tiles/s: 25% above that reference.
The correction reduces the cap by 20%. The old comment multiplied Dynasty's
240 pixels/s by five, although a DuneCity tile has four times the coordinate
width of a Dynasty tile (64 versus 16).

## Reference and limits

Reference: gameflorist/dunedynasty commit
`4469449c75f51388ad2725297a95f09a6c601905`.

- `src/table/unitinfo.c`: carryall `movingSpeedFactor = 200`.
- `src/unit.c`, `Unit_SetSpeed`: full throttle 255 becomes 199; default movement
  quantization rounds this down to 192 internal position units per move.
- `src/unit.c`, `GameLoop_Unit`: movement happens every three game ticks.
- `src/timer/timer_a5.c`: normal speed is 60 ticks/s, hence 20 moves/s.
- `src/tools/coord.c`: 256 internal position units make one tile.
- Thus `192 * 20 / 256 = 15 tiles/s` before direction-table rounding.

## Turning and approach correction (1.0.756)

The initial 1.0.755 change corrected only cruise speed. Further testing found
that the Legacy-derived pickup code added up to 16 world units **per axis per
cycle** within two tiles, then ran normal forward movement in the same cycle.
It therefore bypassed both the cap and the intended approach slowdown.

Dune Dynasty instead uses these normal-speed rules:

- `Unit_SetOrientation` multiplies the carryall's turningSpeed 3 by four.
  `Unit_Rotate` advances 12/256 of a revolution at 15 Hz: **253.125 degrees/s**.
  DuneCity's angle convention is eight units per revolution, not radians.
  The corrected shared `TurnSpeed` is **0.09**, replacing 0.099 (278.4375 deg/s).
- `Script_Unit_MoveToTarget` uses `min(distance/8,255)` in Dynasty's 256-units-per-tile
  coordinate system and multiplies by `(255-headingError)/256`. Heading error
  is the shorter angle in 256 units per revolution. `Unit_SetSpeed` then applies
  factor 200/256 and quantizes normal movement steps to multiples of 16.
- Inside **half a tile**, it stops forward movement and moves at most 16/256 tile
  per axis each script invocation. A delay of two skips two subsequent script
  ticks; script ticks occur every five 60 Hz game ticks. That gives **0.25 tile/s
  per axis** averaged over a 250 ms interval. Arrival uses a 32/256-tile threshold.

`CarryallFlight.h` implements these rates in deterministic fixed point, scaled
by the configured cruise cap. `Carryall::move` is the single place that changes
position: normal flight or final docking, never both. Heading and distance follow
an actual moving ground unit's position. Unloaded pickups, structure transfers,
and loaded ground deliveries all use the same slow approach. Completion requires
arrival distance, so a stopped carryall does not drop cargo prematurely.

Reference values and observed integration results at normal speed:

| Measurement | Before (1.0.755) | After (1.0.756) | Dynasty reference |
|---|---:|---:|---:|
| Nominal turn rate | 278.4375 deg/s | 253.125 deg/s | 253.125 deg/s |
| 90-degree stationary turn | 0.336 s | 0.368 s | 0.400 s in six rotation updates |
| 180-degree stationary turn | 0.656 s | 0.720 s | 0.733 s in eleven rotation updates |
| First movement at 1.5 tiles, aligned | 0.303224 tiles | 0.04 tiles | 2.5 tiles/s nominal approach |
| First movement at 0.25 tiles, aligned | 0.25 tiles | 0.004 tiles | 0.25 tiles/s per-axis docking |
| Pickup from 1.5 tiles, aligned | 0.080 s | 2.208 s | Separate script cadence; not a full-game timing measurement |

This matches Dynasty's normal angular rate, throttle law and averaged docking
rate, rather than replaying its discrete VM. DuneCity updates continuously at
62.5 Hz, Dynasty rotates at 15 Hz and runs scripts at 12 Hz. Dynasty's integer
heading table, delayed throttle refreshes and turning-radius suppression are not
copied wholesale; small trajectory and completion-time differences remain.
Idle acceleration/orbit behavior remains as before. The game-speed controls
still scale each engine, and their fastest settings are not equivalent.

## Mod routing and saves

`config/ObjectData.ini.default` supplies Vanilla and Dune City's generated
ObjectData. Dune2R has no ObjectData override and inherits the Vanilla data.
`ModManager` detects changed bundled ObjectData and reseeds these managed
profiles. Workshop capture can leave a materialized ObjectData copy inside
Dune2R; that inherited copy now also refreshes when the base rules change.
Downloaded Dune2R art is preserved by the existing managed-refresh path.
Tornie's explicit speed/turn statistics are unchanged; the engine's corrected
approach logic applies to Carryall-derived units using their configured caps.

New games use the corrected speed and turn values. Existing saves serialize
ObjectData and retain their saved caps, while the engine-level approach fix
applies after loading. This does not rewrite saved games or custom Workshop
revisions. No flight state fields were added. Network protocol 14 rejects older
simulation behavior, including when a saved/custom ruleset has the same hash.

## Issue 67: repair behavior is separate

The reported version 1.0.737 has the same carryall cap as the pre-fix main
branch. GroundUnit automatically requests repair below half health when a
repair yard and carryall are available, including while the unit has a combat
target (unless forced). The repair yard restores one health point per cycle
when funded: 62.5 HP/s at normal speed, so 100 missing HP takes about 1.6 s.
Changing the cruise cap does not change these pickup rules or repair rate.

## Validation

Local app **1.0.756** builds. Version consistency and the Ninja dependency audit
pass. CTest `dunelegacy_tests`, `game_command_regressions`, `menu_navigation_probe`
and `carryall_flight_probe` pass.

The flight probe links the real production game objects, initializes a game,
and runs real Carryall updates for ten starting distances and four headings.
It asserts source-derived turn times, heading throttle and docking rates;
all 40 pickup/delivery cases complete in each mode. It also covers off-centre
moving targets, cancelled/disappeared targets, repair-yard drop-off, collection
and return. With the same seed and starting headings, all three modes produce
identical CSV results. The menu probe verifies the values for all houses and
explicitly upgrades an old materialized Dune2R rules file.

Claude produced an extracted Dynasty C harness; Codex compared its core
rotation/movement/approach functions to the cloned source (normalizing only
the stubbed position field; the game-speed stub is fixed to normal), then
compiled and executed an independent assertion driver with AddressSanitizer
and UndefinedBehaviorSanitizer. Rotation, six distances, four headings and
final-creep checks pass. These are source-function tests, not a live Dynasty
match. Baseline CSVs, reference sources and logs:
`../outputs/carryall-handling-67/` relative to the checkout. The final game
integration CSVs are under `build/carryall-flight-probe/`.
