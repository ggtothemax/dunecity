# Unit movement reference

Shared standard-unit rules for Vanilla, Dune City and Dune2R, local 1.0.757.
Reference: gameflorist/dunedynasty commit `4469449c75f51388ad2725297a95f09a6c601905`.

## Normal-speed rates

Dynasty moves every three 60 Hz game ticks and uses 256 position units/tile.
DuneCity uses 64 world units/tile at 62.5 Hz. A nominal Dynasty movement step
of N therefore becomes N * 0.08 world units per DuneCity update. Rotation is
turningSpeed * 4/256 revolutions at 15 Hz; our eight-angle-unit representation
needs TurnSpeed = Dynasty turningSpeed * 0.03.

| Unit | Concrete/cruise (tiles/s) | Sand (tiles/s) | Turn (degrees/s) |
|---|---:|---:|---:|
| Ornithopter | 11.25 | 11.25 | 168.75 |
| Frigate | 10 | 10 | 168.75 |
| Devastator | 0.703125 | 0.3125 | 84.375 |
| Deviator | 1.25 | 1.01562 | 84.375 |
| Harvester (empty) | 1.25 | 0.625 | 84.375 |
| Launcher | 1.25 | 1.01562 | 84.375 |
| MCV | 1.25 | 0.625 | 84.375 |
| Quad | 2.5 | 1.25 | 168.75 |
| Raider Trike | 3.75 | 2.5 | 168.75 |
| Saboteur | 2.5 | 1.25 | 253.125 |
| Sandworm | — | 1.25 | 253.125 |
| Siege Tank | 1.25 | 0.625 | 84.375 |
| Soldier | 0.546875 | 0.234375 | 253.125 |
| Sonic Tank | 1.25 | 1.01562 | 84.375 |
| Tank | 1.25 | 0.78125 | 84.375 |
| Trike | 2.5 | 1.25 | 168.75 |
| Trooper | 1.09375 | 0.46875 | 253.125 |

These are healthy, straight-line nominal coordinate rates, not door-to-door
travel times. Normal terrain slows ground units below their concrete cap.
Ornithopter cruise was 14.0625 tiles/s and is now 11.25; its turn rate was
198.84375 degrees/s and is now 168.75. A healthy tank on sand was 1.708984375
tiles/s and is now 0.78125. Many old ground caps were too high, not just aircraft.

## Ground modifiers

`src/unit.c:1342-1401` (`Unit_StartMovement`) chooses the **entered** terrain,
reduces throttle by floor(throttle/4) below half health, then calls
`Unit_SetSpeed` (`src/unit.c:2289-2331`). `src/table/landscapeinfo.c` and
`src/table/unitinfo.c` supply terrain throttles and per-unit factors.

`Unit_SetSpeed` applies the harvester's 0–100 cargo amount using
floor((255-amount)*throttle/256), multiplies by the unit factor /256 with integer
truncation, then rounds speeds >=16 down to a multiple of16. Below16 it moves
16 units intermittently; our smooth simulation uses that average rate.
Dynasty defaults `true_unit_movement_speed=false` and `true_game_speed=true`
(`src/enhancement.c:228-234`). The reference uses those defaults at normal speed.

`DynastyMovement.h` applies that order and scales the result relative to each
configured MaxSpeed, so custom caps remain meaningful. Fully rocky tiles map to
Dynasty's entirely-rock terrain; DuneCity does not expose its partial/mostly-rock
movement categories. Spice colors use the corresponding ordinary spice rate.
Infantry changing its sub-tile position now uses the same terrain/damage rate
as infantry retaining its slot. Damaged standard vehicles no longer receive
a second half-speed penalty in `UnitBase::move`. Both moderate (<50%) and severe
(<25%) damage use the one Dynasty throttle reduction. Tank and siege-tank turret
turns now use their configured body-turn rate, as Dynasty does.

City roads deliberately retain the existing 4x multiplier. Mod-only units
(Rocket Trike, Elite Launcher, special harvesters, ambient planes, etc.) have no
Dynasty counterpart and retain their explicit caps/legacy rules. Standard units
in custom rulesets use their configured speed and turn caps with the corrected
movement modifiers. Original Infantry/Troopers squad map entries become three
individual Soldiers/Troopers in DuneCity; this existing representation remains.

## Boundaries

This aligns nominal movement and angular rates, terrain throttle, load and damage.
It does not replace DuneCity's pathfinder, tile-entry pauses, infantry-slot movement,
flight combat AI or smooth trigonometry with Dynasty's script VM and integer heading
tables. Full-route durations and diagonal rounding can therefore differ. Ornithopter
attack passes and the frigate's existing approach slowdown remain engine behaviors;
frigate cruise/turn caps are corrected. Optional Dynasty enhanced unquantized speed
and its alternative game-speed settings are outside this reference.

New games receive the corrected data. Existing saves retain serialized speed/turn
caps but use the new engine modifiers. No save fields were added. Network protocol15
prevents mixing these simulation rules with1.0.756/protocol14 or earlier peers.
Managed Vanilla/Dune City/Dune2R rules refresh through the existing ObjectData
reseed mechanism; independent Workshop revisions are not overwritten.

## Verification

`unit_speed_probe` links production objects, loads installed rules for each mode,
and checks measured translation and actual rotation against source-derived CSV
values. Cases cover cardinal/diagonal movement, terrain, moderate/severe damage,
empty/full harvesters and both infantry slot paths. It also verifies all-house
caps, destination-terrain selection, custom cap scaling and the road bonus.
The real headless game loop completes eight-tile routes for14 ground unit types
and two ornithopter attack passes that damage a target, in all three modes.
CSV results must be identical across modes. Positional wobble is subtracted from
translation measurements because it is an animation offset, not travel distance.

The oracle CSV is generated from **verbatim Dynasty Unit_SetSpeed** with parsed
unit and terrain tables and a256-tick accumulator measurement. The reference
harness runs with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
python3 tests/units/generate-dynasty-speed-reference.py \
  --dynasty-dir ../dunedynasty --output-dir ../outputs/unit-speed-audit/reference
cmp tests/units/dynasty-unit-speeds.csv ../outputs/unit-speed-audit/reference/dynasty-unit-speeds.csv
ctest --test-dir build -R 'unit_speed_probe|carryall_flight_probe|dunelegacy_tests|game_command_regressions|menu_navigation_probe' --output-on-failure
```

Audit receipts: `../outputs/unit-speed-audit/`. Baseline and corrected nominal
measurements are kept separately. The current full-game CSVs are in
`build/unit-speed-probe/`. Tests retain isolated profiles and never use the
player's saved games or settings.

Final validation passed all five listed CTest groups. The strict oracle run
checks792 movement/rotation cases per mode (2,376 across three modes), plus
42 full-game ground routes and repeated aircraft attacks. The757 app was
installed on the MacBook Air with its prior756 app backed up; runtime and
hidden rendering checks passed on the Air at the installed path.

## Follow-up: full-route comparison

The subsequent [route comparison](unit-route-comparison.md) executes Dynasty's
original unit loop and UNIT.EMC. Most straight sand routes are within1–4%, but
raider/launcher-family travel takes11–12% longer in DuneCity, while infantry can
arrive7–11% sooner. Ground units also start moving earlier during an initial
turn. Nominal alignment therefore does not establish identical route timing.
