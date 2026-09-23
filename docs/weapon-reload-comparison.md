# Turret and launcher firing intervals: engine-aware recheck

2026-09-24. Diagnostic simulations on the same DuneCity audit branch and pinned
Dune Dynasty `4469449c` reference. This records the pre-fix measurements at
`58bd45f4`; see [the subsequent alignment](weapon-cadence-alignment.md) for the
authorized gameplay change and its validation.

## Verified result at each engine's normal speed

These are **steady firing intervals with an acquired, stationary target**,
measured from actual projectile creation over 120 seconds. They include the
normal unit/structure update paths and original Dynasty EMC execution, rather
than treating raw configuration values as seconds. Initial acquisition/turning
time is excluded from the interval; real moving-target encounters can be slower.

| Measurement | DuneCity | Dune Dynasty |
|---|---:|---:|
| Rocket turret, successive missiles | 5.760 s | 2.667–2.750 s |
| Rocket turret's close-range cannon, successive shells | 3.840 s | 2.000–2.083 s |
| Healthy launcher, first-to-second rocket within a pair | 0.240 s | 0.250–0.333 s |
| Healthy launcher, first rocket of one pair to first of next | 5.760 s | 12.250–12.417 s |
| Healthy launcher, second rocket to next pair's first | 5.520 s | 12.000–12.083 s |
| Launcher at 33% HP, successive single rockets | 5.760 s | 12.000–12.083 s |

Ground and air targets gave the same measured timing ranges. All three DuneCity
modes gave byte-identical timestamp traces. Each case uses three seeds.

At **exactly 50% HP**, another engine difference applies: DuneCity still fires
pairs (`isBadlyDamaged` means strictly below 50%); Dynasty only fires twice
when HP is strictly above 50%, so it fires singles at the boundary. Full-health
and 33%-health cases are unambiguous on both sides.

The earlier approximate **2.7-second turret interval is confirmed**. More precise
wording: it is the observed interval including structure-script work, not the
raw Dynasty cooldown. The raw turret script delay is 2 seconds. Nor should this
result be generalized to launchers: **DuneCity launchers fire pairs about twice
as often as Dynasty**, whereas DuneCity turret missiles fire less than half as
often under continuous, steady conditions.

## Why identical-looking numbers are not interchangeable

### DuneCity

Normal game updates are 16 ms (62.5 Hz), independent of rendering frame rate.
`config/ObjectData.ini.default` gives Rocket-Turret and Launcher a reload of
360 cycles; Gun-Turret's reload is 240. `ObjectBase::getWeaponReloadTime` returns
that value directly. Unit and turret timers decrement once per simulation cycle.

- Rocket turret: 360 × 0.016 = **5.76 s**.
- Close cannon: 240 × 0.016 = **3.84 s**.
- Launcher primary starts its 360-cycle timer when the first missile fires.
  Secondary timer is 15 cycles (**0.24 s**). It does not restart the primary,
  leaving 345 cycles (**5.52 s**) after the second missile.

Verified samples, seed 1, ticks relative to control start:

- Turret rockets: 13, 373, 733, 1093, …
- Turret cannon: 13, 253, 493, 733, …
- Healthy launcher: 0, 15, 360, 375, 720, 735, …
- 33%-health launcher: 0, 360, 720, 1080, …

Relevant source: `src/structures/RocketTurret.cpp:98`,
`src/structures/TurretBase.cpp:178`, `src/units/UnitBase.cpp:253-332` and
`:2042-2045`, `src/ObjectBase.cpp:356` and `:843`, and
`include/Definitions.h:97`/`:141`.

### Dynasty

`src/timer/timer_a5.c:25` defines normal game time as **60 Hz**. Unit movement
and the unit `fireDelay` countdown run every three game ticks: **20 Hz**.
Structure and unit script execution is scheduled every five game ticks.
These are different clocks in the same engine.

- `Script_Structure_Fire` uses the launcher's table `fireDelay=120` as a
  **script delay**, returning 120 for rocket fire (80 from the tank for cannon).
  `Script_General_Delay` divides by five for the structure script scheduler.
  Nominal delay is therefore 120/60 = **2 s**; EMC instruction/loop work adds
  about 0.67–0.75 s before the next missile in this control. Cannon's nominal
  80/60 = **1.333 s**, becoming **2.000–2.083 s** including script work.
- `Script_Unit_Fire` instead sets launcher `fireDelay` to **2 × 120 + random
  0/1**, and `GameLoop_Unit` decrements it at **20 Hz**: about **12 s**, plus
  script scheduling before the next shot.
- A healthy launcher's first shot replaces that long delay with **5 + random
  0/1** movement ticks, creating the short gap within a pair. The second shot
  starts the long delay; unlike DuneCity, the long timer starts on the second
  shot, not the first.

Verified samples, seed 1, ground target:

- Turret rockets: 220, 380, 545, 710, 870, 1035, … (160/165-tick gaps).
- Turret cannon: 220, 340, 465, 590, 710, 835, … (120/125-tick gaps).
- Healthy launcher: 0, 15, 740, 755, 1475, 1490, …
- 33%-health launcher: 0, 720, 1445, 2165, …

Relevant Dynasty source: `src/script/structure.c:558-600`,
`src/script/general.c:29-45`, `src/structure.c:76`/`:423-447`,
`src/script/unit.c:644`/`:714-730`, `src/unit.c:365-367`/`:432-449`, and
`src/table/unitinfo.c:601`.

## Controls and limits

30 cases per engine: rocket turret, close cannon, and launcher at 100/50/33% HP,
against ground/air stationary targets, three seeds, 120 seconds each. DuneCity
replays all 30 in each of Vanilla, DuneCity and Dune2R. These are 30 configured
cases repeated across modes, not 90 independent balance trials.

Targets cannot retaliate or move and have diagnostic HP high enough to survive
the entire run. Turret power is provided in DuneCity. Shooter weapon/reload data
is untouched. DuneCity projectile counts must match independent weapon-timer
transitions. Dynasty records native projectile pool creation/reuse and source
IDs; its real unit and structure loops and original UNIT/BUILD scripts run under
ASan/UBSan. The reference placement workaround is documented in
[dynasty-aircraft-reference.md](dynasty-aircraft-reference.md).

The original reference's `Tools_AdjustToGameSpeed` stub returns the normal
argument. That is exactly what Dynasty's actual `timer.c:71-83` does at normal
speed, and with `enhancement_true_game_speed_adjustment=true`. The timer source
also confirms 60 Hz; **60 versus 62.5 Hz has been normalized to seconds**, and
the larger unit/structure scheduling differences are explicitly preserved.
Faster/slower game settings change wall-clock time; no claim is made about an
unverified user-selected speed or about render FPS.

Startup/reacquisition, turret rotation, movement, range, target losses and
combat interruption can change practical output. In particular, the shorter
Dynasty steady turret interval is not proof its first shot arrives sooner.
The result supports a turret-specific discrepancy, not a global rate increase
or an automatic decision to copy all Dynasty balance values.

## Reproduce

```sh
python3 tests/units/run-dynasty-aircraft-reference.py --reload \
  --dynasty-dir ../dunedynasty \
  --reference-build-dir ../outputs/route-speed-alignment/extended3 \
  --output-dir ../outputs/reload-doublecheck/dynasty
python3 tests/units/run-unit-route-probe.py --weapon-reloads \
  --output-dir ../outputs/reload-doublecheck/current
```

The first directory contains native `shots.csv`, `encounters.csv`, script hashes,
ASan/UBSan logs and its executable. The second contains per-mode timestamp CSVs,
logs and the diagnostic executable. All runs and dependency checks passed.
