# Rocket and missile audit: Dynasty versus current Legacy-based build

Audit of DuneCity 1.0.758 (32f92531) against Dynasty 4469449c. The older local
Legacy checkout at a381010 already contains the anti-air modifications; it is
not treated as pristine upstream Legacy. This audit changes no gameplay values.
The current three modes produced identical controlled projectile results.

## Nominal speed and maximum steering rate

Normal speed. Movement is tiles/second; steering is degrees/second, not sprite
animation rotation. Dynasty uses256 coordinates/tile,20 movement updates/second
and15 rotation updates/second. Current code uses64 world units/tile and62.5Hz.

| Shooter / projectile | Current speed | Dynasty speed | Current steering | Dynasty steering |
|---|---:|---:|---:|---:|
| Launcher / Rocket | 18.75 | 15 | 395.508 | 168.75 |
| Deviator / Gas rocket | 18.75 | 15 | 395.508 | 168.75 |
| Rocket turret / ARocket | 18.75 | 11.25 | 395.508 | 675 |
| Trooper at range / MiniRocket | 22.5 | 13.75 | 0 | 421.875 |
| Ornithopter / MiniRocket | 22.5 | 13.75 | 0 | 421.875 |
| Palace / Death Hand | 31.25 | 18.75 | 0 | 168.75 |

Steering capacity is not synonymous with active tracking. Dynasty steers toward
the stored destination while its fireDelay is nonzero, substituting the live
position of any flying target for steering. It does not replace the stored
impact destination with that live position. When the counter expires it stops
issuing new steering targets (an existing turn can still finish).

Our Launcher/Deviator steer continuously; only ornithopters update their live
impact destination. Carryalls retain the scattered launch destination. Turret
rockets continually update both aim and impact destination for any target.
MiniRockets and Death Hand do not steer in the current code.

Current `Bullet.cpp` interprets4.5 in256-units-per-revolution coordinates, so
4.5*360/256*62.5 =395.508 degrees/second. Its comments saying4.5 degrees/tick
are misleading. Numeric speed/steering/fuse settings live in C++, not ObjectData INIs.

Mappings: `src/units/{Launcher,Deviator,Ornithopter,Trooper}.cpp`,
`src/units/UnitBase.cpp:280`, `src/structures/{RocketTurret,Palace}.cpp`;
Dynasty `src/table/unitinfo.c`, `src/script/unit.c:644`,
`src/script/structure.c:577`. Troopers use shells within2tiles; Dynasty reduces
rocket damage by25% at longer range, whereas our code reduces the close-range
shell damage. This is a separate weapon-damage discrepancy, not a speed conversion.
Other standard combat units fire shells or sonic waves, outside this rocket audit.
Mod-only flames/healing projectiles do not have direct Dynasty counterparts.

## Detonation and counters

Dynasty `src/unit.c:1745-1813` declares arrival when distance increases relative
to the previous movement update, or distance is strictly below16 coordinates
(1/16tile). It uses its integer distance metric, max(dx,dy)+min(dx,dy)/2.
For most missiles, arrival detonates only after fireDelay reaches0. Turret
missiles bypass that arming gate. Expiry alone is not a universal self-destruct.
Explosions occur at the missile's newly computed position, not at the target.

Counters are decremented on the20Hz movement clock; air targets double them
(`Unit_CreateBullet`, `src/unit.c:2345-2397`). These are arming/steering durations,
not guaranteed missile lifetimes:

| Projectile | Dynasty ground / air duration | Current setting and effect |
|---|---|---|
| Rocket | 0.40 /0.80 seconds | 22cycles=0.352s ground;50=0.80s air, but air bypasses arming |
| Gas rocket | 0.35 /0.70 seconds | 19cycles=0.304s ground;50=0.80s air, but air bypasses arming |
| Turret rocket | 3 /6 seconds; impact bypasses arming | Constructor overwrites375 with60/120cycles:0.96/1.92s; expiry forces an explosion |
| MiniRocket | 0.15 /0.30 seconds | 7cycles=0.112s or50=0.80s; impact ignores the counter |
| Death Hand | 0.75 seconds for its normal ground target | No arming delay; arrival immediately detonates |

Our `Bullet.cpp:541-583` detects increasing Euclidean distance or distance<4
world units. Launcher/Deviator air impacts bypass the timer. Turret, MiniRocket
and Death Hand snap their coordinates to the destination before detonating.
That snap can turn a miss into a hit, even when the missile is far away.
Rockets in both engines generally pass over terrain/structures; the immediate
wall/building collision branch belongs to ordinary gun projectiles. Off-map
removal also differs: our code permits a5-tile margin.

Death Hand uses17 offset blast centres in Dynasty; our implementation uses21
positions in a5x5 footprint without corners. Gas impact invokes deviation, not
ordinary explosive damage. Dynasty's gas search radius is strictly below2tiles
(`Map_DeviateArea`, `src/map.c:656`); current air/ground gas checks use a half-tile
radius and different eligibility/probability handling. Those effects require
separate balancing checks when aligning detonation.

## Anti-ornithopter compensations and measured bugs

1. **Remote-hit snap:** with an eight-tile distant stationary ornithopter and
   a turret missile deliberately facing180degrees away, current production code
   kills the25HP target in one16ms update with a40-damage shot. The missile was
   eight tiles away before that update. Dynasty detonates near the missile after
   two movement updates and does0 damage. The setup deliberately stresses an
   overshoot; it demonstrates the bug, not its frequency in normal battles.
2. **Special full splash damage:** current `src/Map.cpp:184-192` explicitly
   gives ornithopters full damage throughout the half-tile blast radius. Dynasty
   uses distance falloff for them too. With8-damage explosions, measured damage:

   | Offset from ornithopter | Current | Dynasty |
   |---|---:|---:|
   | 0tiles | 8 | 8 |
   | 0.375tiles | 8 | 4 |
   | 0.625tiles | 0 | 2 |

   Dynasty's normal reaction radius is below1tile; damage halves at quarter-tile
   intervals using its integer metric (`src/map.c:432-492`). Current other-aircraft
   damage also starts at half damage at the centre, unlike Dynasty.
3. **Scatter scaling:** Dynasty `Tile_MoveByRandom` multiplies its integer offset
   by16 coordinate units (`src/tools/coord.c:359-360`). At four Dynasty coordinates
   per current world unit, an equivalent offset needs a factor4 here. Our code
   uses radius directly, making ordinary rocket/gas scatter approximately four
   times smaller, with different angular rounding. Ornithopter tracking then
   overwrites that scattered impact destination altogether.
4. **Turret close-range blind spot:** current `RocketTurret::attack` refuses to
   fire at flying targets inside3tiles. Dynasty's firing routine selects a cannon
   bullet below3tiles. Full target-acquisition/combat validation is still needed
   before changing this interaction.

The corrected ornithopter cruise rate is11.25tiles/s. Dynasty's turret missile
has that same nominal cruise rate and much faster turning; Launcher missiles
are faster than the aircraft. Replacing the compensations requires per-type
speed, steering, targeting, arming, actual-position detonation and falloff together.
Simply treating all missiles as one common fast homing projectile is inaccurate.

## Tests and limits

Headless tests execute real Bullet::update/Map::damage in the current game context
and original Dynasty Unit_CreateBullet/GameLoop_Unit/Map_MakeExplosion with the
actual UNIT.EMC. Dynasty source objects are the previously verified ASan/UBSan
reference. No rendering or live user profiles are involved.

- Current: six projectile types x three target types x three trajectory setups,
  plus three blast-offset checks =57 cases per mode;171 total, byte-identical.
- Dynasty: five native projectile types x three target types x three setups,
  plus three blast checks =48 cases. ASan/UBSan pass.
- Setups: stationary target, reversed launch heading, controlled crossing target.
  Initial impact destinations are fixed to the target to isolate motion/fuses
  from scatter. Flight targets are moved manually; this is not full combat AI,
  a natural-hit-rate benchmark or a statistical accuracy comparison. Synthetic
  target combinations include ones a normal shooter would not select.
- Unit tables/source prove maximum steering rates; CSV `turn_deg_s` is the
  observed first-update turn for the current engine and the configured maximum
  for Dynasty, so do not interpret that CSV field as identical measurements.
- Current crossing trajectory is a controlled11.25tiles/s, with map occupancy
  updated; the source fixture uses the equivalent48 coordinates/60Hz frame.

Reproduce using the existing route-reference build (or create it with
`compare-dynasty-routes.py` first):

```sh
python3 tests/units/run-unit-route-probe.py --projectiles \
  --output-dir ../outputs/projectile-audit/repro-city
python3 tests/units/run-dynasty-projectile-probe.py \
  --dynasty-dir ../dunedynasty \
  --reference-build-dir ../outputs/route-speed-alignment/extended3 \
  --output-dir ../outputs/projectile-audit/repro-dynasty
```

Artifacts and initial investigation: `../outputs/projectile-audit/`.
Claude supplied the bounded static-table audit; Codex traced runtime logic,
closed the unverified firing/steering/explosion questions and ran both engines.
No projectile fixes or new app installation were made during this audit.
