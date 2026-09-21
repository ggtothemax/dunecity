# QuantBot campaign economy proposal — 21 September 2026

Status: proposal, not implemented. Applies to opposing campaign QuantBots in Dune City, not human players or AI partners.

Use the existing vanilla difficulty budget as the anchor: Easy/Medium +10%, Hard +15%, Brutal +20% total recurring income. Higher difficulty can replace harvesters with city income, rather than keeping its full harvester fleet and adding unrestricted taxes.

## How to read the matrix

- These are steady-income **planning estimates**, not measured Dune City outcomes. Assumption: approximately **300 credits per game minute per working harvester**. The Level 2 Easy diagnostic measured 300.1, and several later Easy samples were near 300–320 per harvester. Travel, refinery queues, spice exhaustion, attacks and replacement delays change actual income.
- Vanilla harvesters are nominal AI limits, not a promise that the mission supports or reaches that fleet. Easy is one per initial refinery, Medium two, Hard normally four from its minimum-refinery allowance; Brutal has a nominal seven ceiling. Existing scripted assets and tech restrictions take precedence. Do not grant a missing factory just to reach this table.
- Numbers are **per enemy house with an existing productive base**. Level 9 ranges reflect different starting refinery counts. The tested Level 1 enemy has no harvesting/construction economy; keep it that way.
- R/I/C is the **combined maximum** of residential, industrial and commercial zones, not a cap of that number for each type. Choose the mix from employment and demand; six zones need not mean two of each. Zones are a maximum, not a compulsory build target.
- Dune City harvester counts are **conditional mature-city targets**. Keep the vanilla allowance while taxes develop; reduce the target by one only after sustained spendable city income can replace approximately 300/min. Do not delete existing harvesters or manufacture missing tax income. If the zone cap cannot produce enough taxes, the lower fleet target is not reached.
- The tax column is the income required to reach the proposed combined budget with that mature fleet. It is **not a claim that these zone counts currently yield those amounts**. City growth/upkeep calibration remains to be measured. Budget spendable city revenue after service costs, so gross tax receipts alone do not incorrectly trigger harvester replacement.

## Easy — total income target +10%

| Level | Vanilla H cap | Vanilla est. cr/min | Dune City H* | Max R+I+C | Tax needed/min* | Dune City target cr/min |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 | 1 | 300 | 1 | 6 | 30 | 330 |
| 3 | 1 | 300 | 1 | 8 | 30 | 330 |
| 4 | 1 | 300 | 1 | 10 | 30 | 330 |
| 5 | 2 | 600 | 2 | 12 | 60 | 660 |
| 6 | 2 | 600 | 2 | 14 | 60 | 660 |
| 7 | 2 | 600 | 2 | 16 | 60 | 660 |
| 8 | 2 | 600 | 2 | 18 | 60 | 660 |
| 9 | 1–2 | 300–600 | 1–2 | 20 | 30–60 | 330–660 |

## Medium — total income target +10%

| Level | Vanilla H cap | Vanilla est. cr/min | Dune City H* | Max R+I+C | Tax needed/min* | Dune City target cr/min |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 | 2 | 600 | 2 | 6 | 60 | 660 |
| 3 | 2 | 600 | 2 | 9 | 60 | 660 |
| 4 | 2 | 600 | 2 | 12 | 60 | 660 |
| 5 | 4 | 1200 | 3 | 15 | 420 | 1320 |
| 6 | 4 | 1200 | 3 | 18 | 420 | 1320 |
| 7 | 4 | 1200 | 3 | 21 | 420 | 1320 |
| 8 | 4 | 1200 | 3 | 24 | 420 | 1320 |
| 9 | 2–4 | 600–1200 | 2–3 | 27 | 60–420 | 660–1320 |

## Hard — total income target +15%

| Level | Vanilla H cap | Vanilla est. cr/min | Dune City H* | Max R+I+C | Tax needed/min* | Dune City target cr/min |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 | 4 | 1200 | 3 | 6 | 480 | 1380 |
| 3 | 4 | 1200 | 3 | 12 | 480 | 1380 |
| 4 | 4 | 1200 | 3 | 15 | 480 | 1380 |
| 5 | 4 | 1200 | 3 | 18 | 480 | 1380 |
| 6 | 4 | 1200 | 3 | 21 | 480 | 1380 |
| 7 | 4 | 1200 | 3 | 24 | 480 | 1380 |
| 8 | 4 | 1200 | 3 | 27 | 480 | 1380 |
| 9 | 4 | 1200 | 3 | 30 | 480 | 1380 |

## Brutal — total income target +20%

| Level | Vanilla H cap | Vanilla est. cr/min | Dune City H* | Max R+I+C | Tax needed/min* | Dune City target cr/min |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 | 7 | 2100 | 5 | 6 | 1020 | 2520 |
| 3 | 7 | 2100 | 5 | 12 | 1020 | 2520 |
| 4 | 7 | 2100 | 5 | 18 | 1020 | 2520 |
| 5 | 7 | 2100 | 5 | 24 | 1020 | 2520 |
| 6 | 7 | 2100 | 5 | 27 | 1020 | 2520 |
| 7 | 7 | 2100 | 5 | 30 | 1020 | 2520 |
| 8 | 7 | 2100 | 5 | 33 | 1020 | 2520 |
| 9 | 7 | 2100 | 5 | 36 | 1020 | 2520 |

## Map size and construction rules

Reference layouts measured here are 32×32 for levels 1–2, 62×62 for levels 3–9. For another layout at the same level, multiply the zone cap by `clamp(sqrt(actual map area / reference map area), 0.5, 1.25)`, round down, and keep the level 1 cap at zero. This is a starting space allowance, not an increase to the income budget. Larger maps may support more zones without increasing credited income beyond the difficulty budget. Road and service-building footprints need a separate bounded allowance; an RCI cap alone does not stop factory/service sprawl.

Campaign RTS building permissions must come from the actual mission-start building types. Permit replacement of destroyed starting types and city buildings under these limits. Do not synthesize Light Factory, Radar or Repair Yard permissions when absent at mission start; preserve intended human/allied helper behavior separately.

Count placed AND queued zones against the cap. Apply one combined spice-plus-spendable-tax allowance, with a short smoothing window, for AI growth decisions; do not average over the entire mission and then allow a giant late burst. Never compensate automatically for player-destroyed harvesters or raided infrastructure. The mature fleet column describes a planned economy mix, not an invulnerable guaranteed income.

## Measured vanilla results (separate from planning estimates)

36/36 diagnostic runs completed using current unchanged QuantBot code, source 29a649c3, seed 486409243, Atreides campaign, level 1–9 first layout, Easy automated human-side helper, 25% helper attack setting, up to 10 simulated minutes. Level 2 ended after about 8.25–8.45 minutes. No Dune City income simulation was run for this proposal.

Below, ranges are across productive enemy houses within that run. Excluded zero-economy auxiliary houses from ranges; retained all houses in the raw CSV/JSON. Counts are maximum observed fleet sizes; income is actual cumulative spice credits divided by actual simulated elapsed time. These are single-seed opening-match samples, not averages across maps or many matches. The user's last MBA game logs were not available on claw.local and were not reviewed.

| Level | Difficulty | Peak observed harvesters | Actual average spice credits/min |
|---:|---|---:|---:|
| 1 | Easy | 0 | 0 |
| 1 | Medium | 0 | 0 |
| 1 | Hard | 0 | 0 |
| 1 | Brutal | 0 | 0 |
| 2 | Easy | 1 | 300 |
| 2 | Medium | 1 | 293 |
| 2 | Hard | 2 | 198 |
| 2 | Brutal | 2 | 133 |
| 3 | Easy | 1 | 281 |
| 3 | Medium | 1 | 281 |
| 3 | Hard | 2 | 489 |
| 3 | Brutal | 16 | 1636 |
| 4 | Easy | 1 | 288 |
| 4 | Medium | 2 | 564 |
| 4 | Hard | 4 | 1005 |
| 4 | Brutal | 8 | 1323 |
| 5 | Easy | 2 | 573 |
| 5 | Medium | 4 | 949 |
| 5 | Hard | 4 | 984 |
| 5 | Brutal | 7 | 1475 |
| 6 | Easy | 2 | 633 |
| 6 | Medium | 4 | 1105 |
| 6 | Hard | 4 | 1131 |
| 6 | Brutal | 8 | 1831 |
| 7 | Easy | 2 | 600 |
| 7 | Medium | 4 | 1059 |
| 7 | Hard | 4 | 1097 |
| 7 | Brutal | 9 | 1722 |
| 8 | Easy | 2 | 622–636 |
| 8 | Medium | 4 | 1138–1161 |
| 8 | Hard | 4 | 1131–1169 |
| 8 | Brutal | 9 | 1637–1978 |
| 9 | Easy | 1–2 | 318–637 |
| 9 | Medium | 2–4 | 565–1088 |
| 9 | Hard | 4 | 915–1131 |
| 9 | Brutal | 7–8 | 1435–1815 |

The samples exposed an additional defect: Brutal sometimes exceeded its nominal seven-harvester ceiling (up to 16 observed). The planning table anchors to the intended seven limit, not that excess. This must be investigated and corrected before treating the nominal vanilla budget as enforced. At Level 2, Hard/Brutal earned less than Easy during this particular short combat sample, demonstrating why that outcome should not itself define the intended difficulty income progression.

## Verification still needed before applying

Measure actual net tax income for small city mixes at each development stage, then tune zone growth and the combined AI income limit. Run multiple seeds and house/layout variants with attrition and spice exhaustion, and verify starting-building restrictions, queued-zone counts, factory-granted/delivered harvesters, save/reload behavior, and multiplayer deterministic state. No economy implementation or cross-version behavior is claimed by this proposal.
