# Original Dynasty aircraft reference and cadence experiment

Audit date: 2026-09-24. DuneCity base `224b4342` (current origin/main when
fetched). Dynasty reference `4469449c75f51388ad2725297a95f09a6c601905`.
This investigation changes tests/documentation only, not shipped combat data.

## Original engine reference

`tests/units/dynasty-aircraft-reference.c` runs the original `GameLoop_Unit`
and `GameLoop_Structure`, including original `UNIT.EMC` and `BUILD.EMC`,
normal-speed configuration and enhanced Dynasty rules. Presentation/network
callbacks are stubbed. Houses are explicitly enemies and human-controlled;
there is no AI economy or construction. The fixture directly places complete
pool/map/script turret state because the original `Structure_Place` path
hits an unrelated `upgradeCampaign[3]` out-of-bounds read in
`Structure_IsUpgradable` under UBSan. That source was not patched and UBSan
was not disabled. Only original units/structures participate after setup.

Stationary and flyby diagnostic aircraft have their order script disabled.
Stationary aircraft have speed zero; flybys use actual full-speed engine
movement due west until crossing a fixed exit plane. Attacking ornithopters
use their real script and attack a turret. These are NOT identical flight
controllers or initial turret orientations to the DuneCity encounters, and
must not be described as a matched cross-engine balance benchmark.

Results, per nine encounters (distances 4/8/12, seeds 1/2/3):

| Case | One turret kills | Three turret kills |
|---|---:|---:|
| Stationary ornithopter | 9/9 | 9/9 |
| Stationary carryall | 6/9 | 6/9 |
| Straight ornithopter flyby | 0/9 | 0/9 |
| Straight carryall flyby | 0/9 | 0/9 |
| Attacking ornithopter, 20 seconds | 0/9 | 6/9 |

Stationary carryalls at 12 tiles are out of acquisition range. All flybys
escaped; no flyby timed out. Repeated seeds often produce identical paths;
these rows are not independent statistical estimates of match win rates.
The result demonstrates that difficult interceptions also occur in Dynasty.
It does not prove that DuneCity's complete anti-air balance matches Dynasty.

## Measured firing cadence mismatch

A subsequent [engine-aware reload recheck](weapon-reload-comparison.md) confirmed
this turret interval over 120 seconds against both ground and air targets. It
also found the opposite launcher discrepancy: DuneCity repeats a healthy pair
every 5.76 s, Dynasty every 12.25–12.417 s. Do not generalize the turret result
to all missile weapons.


A separate stationary carryall control at eight tiles restores target health
after each hit to measure repeated shots without ending the encounter.
Original Dynasty turret shots occur at ticks 220, 380, 545, 710, 870 and 1035.
At 60 Hz, successive intervals are 2.667, 2.750, 2.750, 2.667 and 2.750 seconds
(mean **2.717 seconds**). Actual structure-script execution overhead is included.
`Script_General_Delay` divides its argument by five; treating the raw launcher
fire-delay value as either a complete firing interval or a count of structure
script updates would be incorrect.

DuneCity's shipped shared Rocket Turret `WeaponReloadTime=360` is decremented
once per 16 ms game cycle: **5.760 seconds**, before any extra aiming delay.
Both turrets fire 30-damage missiles. DuneCity's configured interval is about
**2.12 times** the original engine's measured interval in this control.
The previous projectile port changed flight but retained this host reload.

The existing DuneCity full-game attacking-ornithopter matrix has 64 encounters
per shooter per mode: four seeds, distances 4/8/12/18, four headings, up to
60 seconds, ending earlier on death of either combatant. A diagnostic override
sets the turret reload to 170 cycles (2.720 seconds); no other parameter changes.

| Per mode | Shipped 360 cycles | Experimental 170 cycles |
|---|---:|---:|
| Turret ornithopter kills | 16/64 | 24/64 |
| Encounters with aircraft damage | 19/64 | 29/64 |
| Turret missiles fired | 124 | 219 |
| Launcher kills (unchanged control) | 15/64 | 15/64 |

Vanilla, DuneCity and Dune2R each produce identical results. Do not count them
as three independent samples. This controlled intervention supports firing
cadence as a contributor: eight extra kills, or a rise from 25% to 37.5% in
this specific matrix. It does not establish 170 cycles as universally balanced
or precisely emulate Dynasty's variable script timing.

## Interpretation

Aircraft HP matches Dynasty: ornithopter 25 and carryall 100. Turret missile
speed is 11.25 tiles/s, equal to ornithopter cruise speed and slower than
carryall cruise speed (15 tiles/s). A stern chase cannot reliably intercept
these aircraft. DuneCity also retains Dynasty's fixed launch destination for
arrival/overshoot checks while guiding toward the live airborne target; this
can end a flight before physical interception. DuneCity's small swept contact
allowance deliberately helps actual interceptions, without teleporting hits.

Correcting cadence is a better-supported first candidate than reducing aircraft
HP or inventing faster missiles. It still needs normal ground-combat and wider
balance checks before changing the shared turret data: reload affects ground
fire as well. No gameplay change, installation, release or push was made.

## Reproduction and evidence

Build the native game using AGENTS.md. Build reference objects once with
`tests/units/compare-dynasty-routes.py`; keep that reference directory unchanged.
Then:

```sh
python3 tests/units/run-dynasty-aircraft-reference.py \
  --dynasty-dir ../dunedynasty \
  --reference-build-dir ../outputs/route-speed-alignment/extended3 \
  --output-dir ../outputs/aircraft-aa/dynasty-final
python3 tests/units/run-unit-route-probe.py --projectile-combat \
  --output-dir ../outputs/aircraft-aa/baseline-repro
python3 tests/units/run-unit-route-probe.py --projectile-combat \
  --rocket-reload-cycles 170 --output-dir ../outputs/aircraft-aa/cadence-170-final
```

Original-engine reference: 91 cases, ASan/UBSan passed; `encounters.csv`,
`shots.csv`, script hashes and logs in `../outputs/aircraft-aa/dynasty-final/`.
Independent existing Dynasty projectile audit also passed ASan/UBSan.
Baseline DuneCity mechanics/combat/continuation CTest probes all passed.
Production native build and Ninja dependency audit passed. Experimental
cadence output: `../outputs/aircraft-aa/cadence-170-final/`.
