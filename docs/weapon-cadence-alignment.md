# Dynasty turret and launcher cadence alignment

2026-09-24. Follow-up to the [engine-aware timing audit](weapon-reload-comparison.md).
The user requested matching both rocket turrets and launchers to Dynasty after
reviewing the measured mismatch. This changes the shared gameplay behavior in
Vanilla, DuneCity, Dune2R and Tornie (including elite launchers).

## Timing translation

Dynasty runs at 60 game ticks per second, decrements unit cooldowns at 20 Hz,
and executes scripts on a five-tick schedule. DuneCity runs simulation updates
at 62.5 Hz. Matching raw counters would therefore reproduce neither weapon's
actual firing interval.

We use fixed, deterministic DuneCity intervals within the observed Dynasty
steady-fire ranges. This matches normal-speed firing cadence without emulating
Dynasty's random one-tick delay or its EMC interpreter scheduling jitter.
Acquisition, aiming, projectile motion and flight controllers remain separate
engine behaviors; matching these intervals does not promise identical battles.

| Weapon measurement | Before | Aligned target | Measured Dynasty range |
|---|---:|---:|---:|
| Rocket turret missiles | 5.760 s | 2.720 s / 170 cycles | 2.667–2.750 s |
| Rocket turret close cannon | 3.840 s | 2.048 s / 128 cycles | 2.000–2.083 s |
| Launcher within pair | 0.240 s | 0.288 s / 18 cycles | 0.250–0.333 s |
| Launcher pair start to next pair start | 5.760 s | 12.288 s / 768 cycles | 12.250–12.417 s |
| Launcher second shot to next first | 5.520 s | 12.000 s / 750 cycles | 12.000–12.083 s |
| Launcher at or below 50% HP, single shots | 5.760 s | 12.000 s / 750 cycles | 12.000–12.083 s |

The launcher long cooldown begins after its second rocket. The first shot also
starts a fallback cooldown so a cancelled second shot cannot stall the weapon.
Only launchers use the strict above-half-health condition for a pair; changing
global damaged-unit semantics would affect unrelated weapons and movement.

A missed second-shot opportunity is discarded instead of remaining queued across
health loss, target loss or a stop order. Elite launchers use the same pair timing
as ordinary launchers; their damage, health and speed advantages are preserved.
Tornie has a separate bundled ObjectData override, which is updated as well.

The standalone gun turret retains its existing reload. The rocket turret's
close cannon is adjusted separately. Aircraft HP and speed are unchanged.
The multiplayer protocol is advanced so clients with old and new deterministic
weapon behavior cannot join the same lockstep game.

## Validation

Baseline receipts remain in
`../outputs/reload-doublecheck/` and `../outputs/aircraft-aa/final/`; new receipts
are in `../outputs/aircraft-aa-alignment/`.

The powered aircraft matrix reruns 180 encounters and four fixed-target controls
per mode in Vanilla, DuneCity and Dune2R (552 rows total). Results are identical
across modes, which replay the same cases rather than independent samples.

| Encounter | Before kills | After kills |
|---|---:|---:|
| Attacking ornithopter, one turret | 0/18 | 2/18 |
| Attacking ornithopter, three turrets | 12/18 | 18/18 |
| Orbiting ornithopter, three turrets | 0/18 | 8/18 |
| Single carryall crossing, one/three turrets combined | 0/36 | 0/36 |

All flybys completed their crossing with no fixture timeouts. Fast carryall
crossings still survive: the change does not make slower missiles catch a
carryall flying away. Aircraft health, speed and missile motion were not retuned.
These are controlled encounters, not full-match balance or win-rate claims.

In the separate 64-case attacking-ornithopter matrix (four headings, four distances,
four seeds, up to 60 seconds), turret kills rose from **16/64 to 27/64**;
launcher kills fell from **15/64 to 8/64**. Launcher rockets fell from 321 to 188,
consistent with restoring Dynasty's longer launcher reload. This deliberately
makes launchers less rapid while making turrets more rapid; it is not a blanket
anti-air buff. Results repeat identically in the three base modes.

The reload regression measures 120 seconds per case: 30 cases in each base mode
and 48 in Tornie including its exclusive elite launcher. It asserts actual
projectile intervals and independently checks weapon timers. Fifteen additional
interruption controls cover crossing the half-health boundary, losing a target,
and stopping between rockets, followed by normal recovery after the long reload.

An initial Tornie regression exposed stale packaged ObjectData after a data-only
incremental build. Native CMake now includes bundled ObjectData files as link
dependencies, ensuring the post-build copy/signing step runs when they change.
Tornie's integrity manifest is updated with the new ObjectData SHA-256; its
checksummed payload verifies in full.

Final verification: all 32 CTest tests passed across the full run and focused
reruns after correcting the packaged Tornie data/checksum (the final rerun
passed unit tests, menu navigation and weapon reloads). Native build and Ninja
dependency audits passed. Source and packaged default/Tornie ObjectData and
Tornie checksum manifest are byte-identical. Original-engine reference remains
ASan/UBSan-clean. The app is built locally; nothing was published or installed.
