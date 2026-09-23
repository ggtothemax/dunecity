# Rocket turret aircraft audit

2026-09-24, based on DuneCity `224b4342` and original Dune Dynasty
`4469449c75f51388ad2725297a95f09a6c601905`. This is the pre-fix audit recorded at `72d8b8da`. That commit changed tests
and documentation only. See [the subsequent alignment](weapon-cadence-alignment.md)
for the authorized gameplay change and its validation.

## Finding

There is a confirmed turret firing-cadence mismatch. DuneCity's shared rocket
reload is **5.76 seconds** (360 game cycles). Original Dynasty's actual turret
script fires every **2.667–2.750 seconds** in a fixed-target control, with the
same 30-damage missile. A test-only 170-cycle (2.72 s) reload raised kills from
**16/64 to 24/64** in the existing attacking-ornithopter matrix, identically
in Vanilla, DuneCity and Dune2R. That experiment isolates reload as a contributor;
it does not establish a universally balanced new value.

Aircraft health is not inflated: ornithopters have 25 HP and carryalls 100 HP,
matching Dynasty. A direct rocket hit kills an ornithopter; four are required
for a full-health carryall. The physics also make interceptions difficult:
missiles and ornithopters both cruise at 11.25 tiles/s, while carryalls travel
at 15 tiles/s. Missiles cannot gain on aircraft flying directly away from them.
Dynasty's original engine also shows surviving flybys and weak lone-turret
interception. These observations do not establish an extra carryall HP bug.

See [the original-engine reference and cadence experiment](dynasty-aircraft-reference.md)
for the measured Dynasty shot times, controls, limitations and reproduction.

## Powered-turret encounter matrix

The additional probe drives actual `Game::updateGameState`, with real turret,
aircraft and projectile objects, normal 16 ms cycles, isolated profiles and
headless rendering. It does not approximate movement or damage.

Per mode: 180 encounters on Habbanya-Autumn, two defender houses (Atreides and
Harkonnen), one/three turrets, start distances 4/8/12 tiles and three seeds.
Turrets stand at x=20, y=15/17/19; aircraft start due east facing west.
The three modes replay the same matrix; identical results are not independent
statistical samples. Nor are repeated seeds in this small fixed geometry.

Power requirements are **enabled**, with real distant WindTraps. The fixture
checks and records produced/required power: 100/25 for one turret, 200/75 for
three. The defender has no other combat units and no AI economy.

| Scenario | Aircraft | One turret kills | Three turret kills |
|---|---|---:|---:|
| Committed attack, up to 20 seconds | Ornithopter | 0/18 | 12/18 |
| Single crossing pass | Ornithopter | 0/18 | 0/18 |
| Single crossing pass | Carryall | 0/18 | 0/18 |
| Idle orbit, 20 seconds | Ornithopter | 0/18 | 0/18 |
| Idle orbit, 20 seconds | Carryall | 0/18 | 0/18 |

Counts are **per mode**. The fixed westward attack matrix differs from the
older 64-case matrix that varies headings and runs for up to 60 seconds;
its 0/18 lone-turret result does not replace the older 16/64 result.

All 72 flybys per mode traversed the turret column and crossed the fixed exit
plane; none timed out. All 36 carryall passes per mode survived. In the eight-tile starting cases,
some carryalls took cannon damage during the crossing, averaging 3.33 HP lost;
in the four/twelve-tile starting cases, none took damage. One or three turrets made no difference to that health loss.
These results cover fast corridor crossings, not pickup, unloading or repair
approaches, where carryalls slow down. They are not full-match win rates.

Idle aircraft actually fly: an unordered ornithopter travels 225 tiles and a
carryall roughly 293 tiles during these 20-second orbit cases. Idle is not hover.

## Diagnostic and accounting checks

Four extra stationary diagnostics per mode separately test each aircraft at
two tiles (cannon) and five tiles (rocket). Only these diagnostic instances have
speed set to zero; movement uses the normal engine with no position teleport.
Both sampled displacement and actual per-cycle movement must remain zero.
These controls prove aircraft damage and kill paths independently of tracking;
they are not normal gameplay scenarios.

Shots are counted from newly observed projectiles and separated into rockets
and cannon shells. Counts are checked against each turret's weapon-timer resets.
Health is recorded before terminating on a kill, so fatal damage is included.
Health loss is capped at remaining HP; it is not nominal damage or a missile
hit probability. Same-cycle projectile disappearance is only a tentative weapon
attribution, not proof of which projectile caused damage.

The runner fails on missing encounters, differing mode results, invalid flyby
termination/traversal, underpowered fixtures, stationary movement/survival, or
shot-count disagreement. It checks Ninja dependencies before and after running.

## Relevant code paths

- `config/ObjectData.ini.default:279`: Rocket Turret damage 30, range 8,
  reload 360 cycles. `TurretBase.cpp:178` decrements the reload once per update.
- `src/structures/RocketTurret.cpp:98`: below three tiles, switch to cannon
  damage 20 and GunTurret reload 240 cycles (3.84 seconds). Close-range aircraft
  damage works; the existing projectile mechanics probe verifies it.
- `src/structures/TurretBase.cpp:79`: target loss and scan scheduling;
  idle scans use 50–69 cycles (0.80–1.104 seconds). Scanning/aiming can consume
  much of a short flyby even before reload matters.
- `include/DynastyProjectile.h:15`: turret missile step/turn/delay.
- `src/Bullet.cpp:419`: a one-eighth-tile swept contact test helps turret
  missiles hit flying targets. It is an intentional DuneCity addition.
- `src/Bullet.cpp:443`: ordinary arrival/overshoot is checked against the
  launch-time destination; steering uses the live airborne target. Thus tracking
  does not guarantee the missile survives until interception. This is a source
  explanation, not an isolated causal experiment in the powered matrix.
- `src/structures/RocketTurret.cpp:79` and `src/ObjectBase.cpp:1069`:
  ornithopters receive triple acquisition/retention range; carryalls do not.
  Dynasty likewise reserves its triple-range exception for ornithopters.
- `src/ObjectBase.cpp:463`: carryalls are deprioritized when competing targets
  exist. This matrix has one enemy aircraft, so it does not measure the effect
  of that policy and cannot blame it for the observed failures.

## Recommended next change

Use the measured cadence mismatch as the first controlled balance candidate,
rather than lowering HP or increasing missile speed. The 170-cycle experiment
already improves ornithopter defence in one matrix. Decide whether cadence
alignment should affect ground fire too before changing the shared reload;
then test ground combat, wider air geometry, and deterministic replay/protocol
compatibility. No production correction is included in this audit.

## Reproduce

Build the native game with the repository's AGENTS.md instructions, then:

```sh
python3 tests/units/run-aircraft-aa-probe.py --output-dir ../outputs/aircraft-aa/final
python3 tests/units/summarize-aircraft-aa.py \
  --results ../outputs/aircraft-aa/final/encounters.csv \
  --flags ../outputs/aircraft-aa/final/flags.csv
```

Final receipts are in `../outputs/aircraft-aa/final/`: 552 rows across three
modes (540 moving encounters plus 12 stationary diagnostics), per-mode tables,
flags, summaries, logs and the separately linked diagnostic executable.
The original-engine and cadence receipts are described in the linked report.
Initial `run1`/`run2` incorrectly called orbiting aircraft stationary and failed
to end flybys; those files are superseded. Worker `run3`/`run4` used a position
pin after movement; the final diagnostic instead freezes actual motion.

Claude Max supplied the powered encounter fixture. Codex reviewed and corrected
fixture semantics/validity checks, ran the original Dynasty reference and cadence
experiment, and independently verified the existing projectile tests.
