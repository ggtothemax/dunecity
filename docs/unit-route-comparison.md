# Complete route comparison: DuneCity 1.0.757 versus Dynasty

Measured healthy units crossing empty, flat sand with no roads, combat or cargo.
Default timing: DuneCity16 ms/cycle, Dynasty60 Hz. Ground routes are8 and16tiles;
initial heading is aligned or90degrees away. Results average5 DuneCity start
phases and60 Dynasty phases (full LCM of3/4/5-tick scheduling). Identical DuneCity,
Vanilla and Dune2R CSVs:280 routes each. Dynasty:3,360 scenarios, repeated for
scenario and produced units with identical results.

Dynasty runs unmodified core unit, map, pathfinding and script VM sources from
4469449c, with the actual UNIT.EMC extracted from the locally installed DUNE.PAK.
Script SHA256: da9d00b178cd4a96034698c0fcc3435a9428822dbee1530089e76fdb527c2583.
Only presentation/audio/network callbacks and normal-speed timer plumbing are
stubbed; a flat map and human house are initialized by the harness. Sprite IDs
provide correct sand classification; icon drawing data is inert. No rendering,
GUI or realtime timer is required. Original code includes tile arrival stops,
speed-accumulator resets, script delays, turning and subsequent route steps.
Reference reruns under ASan/UBSan reproduce the initial CSV byte-for-byte.

DuneCity runs the production Game::updateGameState on an empty sand corridor,
including normal path requests and tile transitions. This is a controlled
movement comparison, not a claim that whole matches or all terrain cases match.

## Eight-tile straight routes

Positive difference means DuneCity takes longer; negative means it arrives sooner.

| Unit | Dynasty seconds | DuneCity seconds | Time difference |
|---|---:|---:|---:|
| Tank | 11.917 | 11.552 | -3.06% |
| Trike | 7.917 | 7.712 | -2.59% |
| Raider Trike | 4.017 | 4.512 | +12.33% |
| Quad | 7.917 | 7.712 | -2.59% |
| Harvester | 13.883 | 14.112 | +1.65% |
| Soldier | 35.917 | 33.328 | -7.21% |
| Trooper | 18.017 | 17.328 | -3.82% |
| Devastator | 26.067 | 26.912 | +3.24% |
| Launcher | 8.117 | 9.024 | +11.18% |
| Siege Tank | 13.967 | 14.112 | +1.04% |
| MCV | 13.883 | 14.112 | +1.65% |
| Deviator | 8.117 | 9.024 | +11.18% |
| Sonic Tank | 8.117 | 9.024 | +11.18% |
| Saboteur | 7.917 | 7.104 | -10.27% |

Most straight routes differ by about1–4%. Raiders and launcher-family units
need11–12% longer; Soldiers arrive7% sooner and Saboteurs10–11% sooner.
Sixteen-tile routes show the same pattern, so it is not just command latency.

Initial90-degree turning is also quicker in DuneCity: tank route penalty0.80s
versus Dynasty1.25s; trike0.40s versus0.625s; soldier0.32s versus0.50s.
DuneCity starts a ground step when its rounded eight-direction sprite heading
matches the next tile, whereas Dynasty's route script waits for exact orientation.
Nominal angular velocity alone therefore does not equal initial turn time.

This supersedes any implication that1.0.757 matches complete travel timing.
It matches nominal rates/modifiers; scheduling, sub-tile infantry movement and
turn-completion rules still cause measured differences. No production behavior
was changed during this comparison.

## Reproduce

```sh
python3 tests/units/compare-dynasty-routes.py \
  --dynasty-dir ../dunedynasty \
  --output-dir ../outputs/route-speed-comparison/repro
```

Requires the existing macOS Ninja DuneCity build and its original DUNE.PAK data.
Original game data is not committed. The runner compiles the headless reference,
checks both scenario/produced units, runs all three local modes and writes
comparison.csv. Tests intentionally measure normal speed, regardless of saved
player preferences. The Air's saved settings at audit time were Vanilla4ms
(4x nominal simulation rate), Dune City18ms (0.889x), and Dune2R16ms defaults.
Those preferences were inspected but not modified.
