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

This matches the nominal cruise cap, not every frame of Dynasty's flight
model. Dynasty uses integer direction tables, script-dependent throttle and
approach/turn slowdown. DuneCity retains continuous fixed-point movement and
its existing approach slowdown within ten tiles. Loaded and empty carryalls
share the cap. Game-speed controls continue scaling each game's simulation;
their fastest settings are not equivalent.

## Mod routing and saves

`config/ObjectData.ini.default` supplies Vanilla and Dune City's generated
ObjectData. Dune2R has no ObjectData override and inherits the Vanilla data.
`ModManager` detects changed bundled ObjectData and reseeds these managed
profiles. Tornie has a separate explicit override and is outside this change.

New games use the corrected value. Existing saves serialize their ObjectData
and retain their saved speeds; this change does not rewrite saved games or
custom Workshop revisions.

## Issue 67: repair behavior is separate

The reported version 1.0.737 has the same carryall cap as the pre-fix main
branch. GroundUnit automatically requests repair below half health when a
repair yard and carryall are available, including while the unit has a combat
target (unless forced). The repair yard restores one health point per cycle
when funded: 62.5 HP/s at normal speed, so 100 missing HP takes about 1.6 s.
Changing the cruise cap does not change these pickup rules or repair rate.

## Validation

Local app 1.0.755 builds successfully. Version consistency and the Ninja
dependency audit pass. CTest `dunelegacy_tests` and `menu_navigation_probe`
pass. The menu probe loads the real installed ObjectData for all three modes
and every house, verifying 15 nominal tiles/s at 640, 854 and 1280 widths.
Evidence is in `../outputs/carryall-speed-67/` relative to the checkout.
