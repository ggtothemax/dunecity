# Implemented candidate — 1.0.745

The matrix below is now implemented for **Dune City campaign enemy QuantBot only**.
Vanilla, custom games, other city mods, and the human-side helper keep their existing policies.
The older design discussion follows as historical rationale; its “not implemented” wording
records the earlier proposal, not the current status.

| Campaign level | Easy H / total RCI | Medium H / total RCI | Hard H / total RCI | Brutal H / total RCI |
|---:|---:|---:|---:|---:|
| 1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 2 | 1 / 4 | 2 / 5 | 3 / 12 | 6 / 18 |
| 3 | 1 / 4 | 1 / 8 | 3 / 12 | 5 / 20 |
| 4 | 1 / 4 | 1 / 8 | 2 / 16 | 4 / 24 |
| 5 | 1 / 8 | 2 / 16 | 1 / 20 | 3 / 28 |
| 6 | 1 / 8 | 2 / 16 | 1 / 20 | 3 / 28 |
| 7 | 1 / 8 | 2 / 16 | 1 / 20 | 3 / 28 |
| 8 | 1 / 8 | 2 / 16 | 1 / 20 | 3 / 28 |
| 9 | 1 / 8 | 2 / 16 | 1 / 20 | 3 / 28 |

H is a production ceiling, not a grant or an order to remove starting workers. It cannot
exceed the original refinery multiplier/minimum allowance, the positive Game Options
ceiling, or current spice capacity. On levels 5–9, small original budgets use Easy
1H/4 zones (one original worker allowance), or Medium 1H/8 zones (at most two).
Original zero-refinery houses receive neither workers nor zones, regardless of difficulty.

The income goal is frozen at 450 credits/min per original allowed worker: 150% of the
300-credit reference. Discretionary zone orders stop at either the shared placed+queued
physical cap or the combined income forecast. That forecast uses current tax/population/
land value, city police/power costs, a conservative half-weight forecast for queued and
developing zones, and the larger of planned versus committed workers while spice remains.
The AI chooses its R/I/C mix. Earned revenue is never clipped or granted by this policy;
normal maturation, combat, storage losses and harvesting paths can change actual income.

No new harvesting capacity is ordered once a fresh map-wide scan finds no spice. After
30 game seconds of sustained zero, with no cargo left in any owned harvester (fractional
spice and inactive/transit cargo count), Hard's **total** zone cap becomes 24 and Brutal's
40. Easy/Medium retain their normal zone cap. The original income goal stays fixed.
A fresh spice bloom removes the additional expansion allowance without demolishing zones.

RTS building types must have existed at mission start; replacement is allowed. A military
factory's city employment role does not grant permission. City-only zones, roads, nuclear
power and civic services remain eligible. Actual production checks aggregate all builders;
refinery worker deliveries also respect paid/queued worker reservations. Older pending
orders for disallowed RTS structures are cancelled when the builder is next planned.

## Validation and limits

- Claude produced the pure decision table and 11 unit cases, including all 36 matrix cells;
  Codex completed integration, reviewed the patch and ran verification.
- Native build and all eight CTest suites pass. The unit suite was rerun after the final
  legacy-save correction. Dependency audits pass before and after the build.
- Real-engine probes pass on Easy level 2, Hard level 5 and Brutal level 9: two-yard RCI
  ordering, queued-worker/refinery delivery reservation, original building permissions,
  scope exclusion, fractional spice/cargo, the depletion delay, bloom recovery, new-save
  round trip, and old-save permission recovery.
- In fixed-seed ordinary campaigns, 134 enemy snapshots stayed within the shared cap and
  introduced no unauthored RTS types. Easy L2 peaked at 2 committed zones/1 worker before
  defeat at 7.9 minutes; Hard L5 reached 15/1 over 15 minutes; Brutal L5 reached 19/3 before
  winning at 14.27 minutes. Observed whole-run net harvest+city income was approximately
  353, 1748 and 3030 credits/min, respectively. The reference goals were 450, 1800 and 3150.
  These runs include opening/combat effects and are not paired vanilla calibration runs.
- Local native spectator hot-join passed with matching resumed state at cycle 1800.
  Browser crossplay was not rerun for this native candidate.
- Large post-spice caps are provisional. The probes verify enforcement and transitions;
  they do not prove that 40 zones will produce the desired tax revenue on every map.
- Save format 9842 stores the original budget, building types and exhaustion state.
  This app reads older saves; older apps cannot read its new saves. For bundled older
  campaign saves, recover the authored layout through `openCampaignFile` (not generic
  file lookup, which can select another mod's same-named scenario). Missing external
  scenario content falls back to saved permissions and emits a warning.

Evidence: `../outputs/mba-test-1.0.745/validation/`.

---

# QuantBot campaign: 150% income target and post-spice expansion

User-confirmed direction on 21 September 2026: target 50% more combined income than vanilla; higher difficulties may expand R/I/C after map spice is exhausted. This is a revised design target, not an implemented or measured gameplay result. It supersedes the previous lower-income target matrix. The source-verified refinery formulas and historical measurements in the previous comparison remain valid.

## Target definition

For each original economic enemy house, freeze its vanilla planning budget at mission start, using the verified refinery multiplier/minimum rules and applicable explicit ceilings, before later resource depletion reduces the target. Use 300 credits/min per allowed functioning harvester as the existing planning reference. Total Dune City target = 1.5 × that original budget. All tax is included in this total; do not add another 50% in taxes on top of it. This is an income goal, not a direct credit grant, AI-only tax multiplier or confiscation of excess income.

Because original house budgets differ, the same level may have different targets. Level 1 and other zero-economy scripted houses remain zero. Use the original mission/house budget for other campaign branches, not a blanket level number. The 300-per-worker reference is not a measured universal yield. Money targets below describe the desired mature economy, not expected income at mission start or a proven average.

AI still chooses its R/I/C mix. A single queued-plus-placed zone ceiling applies, with no individual R/I/C quotas. Physical zone ceilings from the previous matrix must be recalibrated for the new income goal; they are not sufficient merely because the table now says +50%. Existing controlled samples are nonlinear: on the large reference map two residential zones yielded76.5/min, while three RRC zones yielded335.7/min. Do not fabricate a precise zone count by dividing a tax goal by a universal per-zone rate. Higher difficulty's proposed harvest share below decreases so that more of the same combined goal comes from city taxes.

## Normal mature economy matrix

Counts are per house. H columns show vanilla/current Dune City runtime allowance and the proposed mature harvester allowance. Money columns are DESIGN TARGETS in credits/min, not new measurements. Net tax contribution means tax after city operating costs; additional RTS-base operating costs must be treated consistently when validating actual total income. Existing current measured harvest/tax/power/net fields are retained in the CSV. Counts above starting assets do not grant harvesters or buildings; original RTS building-type restrictions remain required.

**Easy**

| Level | Vanilla/current H | Target H | Vanilla budget/min | Target harvest/min | Required net tax/min | Combined goal/min | Increase/min | Increase % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0/0 | 0 | 0 | 0 | 0 | 0 | 0 | — |
| 2 | 1/1 | 1 | 300 | 300 | 150 | 450 | 150 | +50% |
| 3 | 1/1 | 1 | 300 | 300 | 150 | 450 | 150 | +50% |
| 4 | 1/1 | 1 | 300 | 300 | 150 | 450 | 150 | +50% |
| 5 | 2/2 | 1 | 600 | 300 | 600 | 900 | 300 | +50% |
| 6 | 2/2 | 1 | 600 | 300 | 600 | 900 | 300 | +50% |
| 7 | 2/2 | 1 | 600 | 300 | 600 | 900 | 300 | +50% |
| 8 | 2/2 | 1 | 600 | 300 | 600 | 900 | 300 | +50% |
| 9 | 1–2/1–2 | 1 | 300–600 | 300 | 150–600 | 450–900 | 150–300 | +50% |

**Medium**

| Level | Vanilla/current H | Target H | Vanilla budget/min | Target harvest/min | Required net tax/min | Combined goal/min | Increase/min | Increase % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0/0 | 0 | 0 | 0 | 0 | 0 | 0 | — |
| 2 | 2/2 | 2 | 600 | 600 | 300 | 900 | 300 | +50% |
| 3 | 2/2 | 1 | 600 | 300 | 600 | 900 | 300 | +50% |
| 4 | 2/2 | 1 | 600 | 300 | 600 | 900 | 300 | +50% |
| 5 | 4/4 | 2 | 1,200 | 600 | 1,200 | 1,800 | 600 | +50% |
| 6 | 4/4 | 2 | 1,200 | 600 | 1,200 | 1,800 | 600 | +50% |
| 7 | 4/4 | 2 | 1,200 | 600 | 1,200 | 1,800 | 600 | +50% |
| 8 | 4/4 | 2 | 1,200 | 600 | 1,200 | 1,800 | 600 | +50% |
| 9 | 2–4/2–4 | 1–2 | 600–1,200 | 300–600 | 600–1,200 | 900–1,800 | 300–600 | +50% |

**Hard**

| Level | Vanilla/current H | Target H | Vanilla budget/min | Target harvest/min | Required net tax/min | Combined goal/min | Increase/min | Increase % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4/4 (no economy) | 0 | 0 | 0 | 0 | 0 | 0 | — |
| 2 | 4/4 | 3 | 1,200 | 900 | 900 | 1,800 | 600 | +50% |
| 3 | 4/4 | 3 | 1,200 | 900 | 900 | 1,800 | 600 | +50% |
| 4 | 4/4 | 2 | 1,200 | 600 | 1,200 | 1,800 | 600 | +50% |
| 5 | 4/4 | 1 | 1,200 | 300 | 1,500 | 1,800 | 600 | +50% |
| 6 | 4/4 | 1 | 1,200 | 300 | 1,500 | 1,800 | 600 | +50% |
| 7 | 4/4 | 1 | 1,200 | 300 | 1,500 | 1,800 | 600 | +50% |
| 8 | 4/4 | 1 | 1,200 | 300 | 1,500 | 1,800 | 600 | +50% |
| 9 | 4/4 | 1 | 1,200 | 300 | 1,500 | 1,800 | 600 | +50% |

**Brutal**

| Level | Vanilla/current H | Target H | Vanilla budget/min | Target harvest/min | Required net tax/min | Combined goal/min | Increase/min | Increase % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 7/7 (no economy) | 0 | 0 | 0 | 0 | 0 | 0 | — |
| 2 | 7/7 | 6 | 2,100 | 1,800 | 1,350 | 3,150 | 1,050 | +50% |
| 3 | 7/7 | 5 | 2,100 | 1,500 | 1,650 | 3,150 | 1,050 | +50% |
| 4 | 7/7 | 4 | 2,100 | 1,200 | 1,950 | 3,150 | 1,050 | +50% |
| 5 | 7/7 | 3 | 2,100 | 900 | 2,250 | 3,150 | 1,050 | +50% |
| 6 | 7/7 | 3 | 2,100 | 900 | 2,250 | 3,150 | 1,050 | +50% |
| 7 | 7/7 | 3 | 2,100 | 900 | 2,250 | 3,150 | 1,050 | +50% |
| 8 | 7/7 | 3 | 2,100 | 900 | 2,250 | 3,150 | 1,050 | +50% |
| 9 | 7/7 | 3 | 2,100 | 900 | 2,250 | 3,150 | 1,050 | +50% |

## Post-spice phase

Interpret “higher difficulties” as Hard and Brutal. This is a proposal choice, not a newly confirmed selection of difficulties. Easy/Medium retain the normal zone allowance; their remaining tax income still continues and normal growth/replacement is permitted. They receive no depletion-specific zone expansion, so their combined income is expected to fall when harvesting ends.

Hard/Brutal may grow beyond their normal zone allowance to replace lost harvesting income. Keep the SAME mission-start total budget; do not multiply it by1.5 again and do not rebase it against vanilla's now-zero spice income. The desired post-spice split is zero harvesting plus100% city revenue. For sampled economic houses this means Hard1800/min and Brutal3150/min net tax. The amount of extra tax required is exactly the normal harvesting contribution being replaced, shown below. These are income goals, not guaranteed tax yields or an implemented physical-cap table.

| Level | Hard additional net tax/min | Hard post-spice tax goal/min | Brutal additional net tax/min | Brutal post-spice tax goal/min |
|---:|---:|---:|---:|---:|
| 2 | 900 | 1,800 | 1,800 | 3,150 |
| 3 | 900 | 1,800 | 1,500 | 3,150 |
| 4 | 600 | 1,800 | 1,200 | 3,150 |
| 5 | 300 | 1,800 | 900 | 3,150 |
| 6 | 300 | 1,800 | 900 | 3,150 |
| 7 | 300 | 1,800 | 900 | 3,150 |
| 8 | 300 | 1,800 | 900 | 3,150 |
| 9 | 300 | 1,800 | 900 | 3,150 |

Proposed transition rules:

1. Verify map-wide harvestable spice exhaustion with fresh scans. Use a short sustained-zero interval (proposed30 game seconds), rather than one worker reporting no reachable field. Count cargo in all owned harvesters, including contained/unloading/transported workers, so the last loads are accounted for before calling the income fully tax-only. Cargo receipt remains part of combined income during the transition.
2. Expand gradually using earned cash, demand and income forecasts, with a shared finite RCI cap appropriate to the actual map/buildable area and level. A larger map alone does not increase the income budget. Save investment funds while spice is still available; no emergency credit grant if the AI failed to save enough. Small maps or poor city development may prevent reaching the goal.
3. The extra allowance permits rebuilding and normal city growth; it does not force all zones to be built or impose a fixed R/I/C mix. Stop discretionary income expansion near the combined budget, accounting for queued/developing zones and natural densification. Actual income may overshoot/undershoot; do not clip tax receipts. Preserve ordinary services/repairs.
4. Stop investing in new harvesting capacity when none can earn from spice; retain existing assets. If blooms or other mechanics restore harvestable spice, include any resumed harvest income in the same total and stop additional depletion-specific expansion. Do not demolish already-built zones to switch modes.
5. Set and validate actual normal/post-spice zone caps from real candidate AI/fixture measurements. The previous largest tested city was20zones with1628–1755 net credits/min in the sampled large-map layouts, which does not substantiate a3150/min Brutal tax-only economy. Larger fixtures and small-map feasibility need calibration before implementation; do not present untested30/40zone counts as established balance.

## Source audit and validation status

Existing map spice is scanned every500 game cycles in src/players/QuantBot.cpp:711–731 (about8 game seconds at16ms/cycle). Runtime currently retains at least a one-worker resource allowance at735–762, so “harvester target is one” is not a zero-spice detector. The existing waitingCargo logic at3515–3530 covers a subset of blocked returning harvesters; it is not a total cargo meter. Existing city selection uses demand, marginal return and funding (approximately3830–3960) and is not proof of the new income target or post-spice finite allowance. Tile::triggerSpiceBloom atsrc/Tile.cpp:1071 can create new spice, so the transition must allow resource recovery.

One bounded Claude Max read-only worker located depletion and city-selection paths but hit its turn budget without a final report. Codex independently reviewed the relevant source. Generated36setting rows over48paired enemy-house rows; assertions verify target harvest+net tax=1.5×vanilla, correct post-spice replacement amounts, and no target allowance above the verified current ceiling. No gameplay code, version, build, save or deployment changes. This change fixes the design target and records its remaining calibration requirements; it does not claim completed balancing.
