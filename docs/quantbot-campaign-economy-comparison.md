# Corrected QuantBot campaign economy proposal

Replaces the earlier observed-fleet balance matrix. Proposal only; no game changes. Source reviewed at 04dbf22a, 21 September 2026. All counts and income are per enemy house, using the same Atreides first-layout samples as the existing measurements.

## Correct harvester baseline

The campaign begins with multiplier × mission-start refinery allowance. It is not a flat fleet count by level and not the observed number of workers in a short game.

| Difficulty | Active config multiplier | Minimum refinery allowance | Runtime campaign enemy rule before spice/options reductions |
|---|---:|---:|---|
| Easy | 1 | 0 | Initial refineries × 1 |
| Medium | 2 | 0 | Initial refineries × 2 |
| Hard | 2 (config text says 2.5; integer parser truncates it) | 2 | 2 × max(initial refineries, 2); mission 21+ explicitly resets the allowance to 2 |
| Brutal | 999 | 4 | Runtime uses remaining spice, then caps enemy target at 7, overriding the huge initial product |

These values are identical in the actual vanilla and Dune City measurement-profile configs. They are distinct from C++ fallback defaults. Minimum refinery allowance is permission to build/rebuild toward that count, not free placed refineries. The Hard minimum applies before late missions too. Its mission 21+ override is a separate assignment to two refineries; it is not a generic harvester minimum at every higher level. Mission number is not displayed level: sampled levels 1–9 use missions 1, 2, 5, 8, 11, 14, 17, 20, 22. Other branches must use their actual mission number and starting house assets.

Runtime then reduces the target for remaining spice (floor(spice/2000), with a minimum resource allowance of 1), positive Game Options overrides and positive house limits. These are target ceilings, not a guarantee every acquisition path enforces them. Existing workers are not removed when the target falls. Enemy rules must not be confused with support/allied AI rules.

Evidence: src/players/QuantBot.cpp:510–565, 735–762; src/players/QuantBotCampaign.cpp:23–33; src/players/QuantBotConfig.cpp:270–271; src/FileClasses/INIFile.cpp:60–75; config/QuantBot Config.ini.default:81–152; include/misc/CampaignControls.h:9–12. Measured-profile configs are in /tmp/dunecity-results-744/income/runs/L2-hard/profile/{config,mods/dunecity}/QuantBot Config.ini.

The measurement parser's old `configured_harvester_limit` column is actually the maximum emitted `harvester_ai_limit` over the sampled run, not a raw ini setting. The new CSV calls it `runtime_limit_peak`. Each included vanilla and Dune City runtime value was reconciled with the formula above; observed fleets remain separate columns. Initial refinery counts are inferred from Easy's multiplier-1/no-topup runtime values in these spice-rich samples, not from the largest refinery count reached later.

## Revised target design

Use one shared ceiling for all R+I+C zones, including queued zones. QuantBot chooses the mix. Allow more city income on harder difficulties, replacing progressively more harvesters instead of stacking all city income on a full vanilla fleet. No artificial tax cap, free cash or special tax multiplier. Normal city tax rules remain.

Compared with the withdrawn matrix: level 2 Medium has a two-worker vanilla budget, Hard four, Brutal seven. Higher-level Medium and Hard use two/four workers as their original refineries and minimum dictate. The revised targets below use those allowances. Medium's later eight-zone economy replaces two of four harvesters; Hard's twelve zones replace two of four; late Brutal's twenty zones replace four of seven. Easy stays at one zone while its vanilla allowance is one worker, and trades one of two workers for three zones when the original economy grows. This is a candidate for playtesting, not a calibrated guarantee.

Start from the original mission assets and preserve RTS building-type restrictions. A target ceiling does not grant workers, refineries, Heavy Factories or any other building type. Grow the city before lowering replenishment targets. Zero-economy scripted houses receive no new economy. Level 1 nominal Hard/Brutal targets are 4/7 in the current code, but those houses have no sampled working economy; the proposal keeps them at zero workers/zones, and does not assign them fictional full-fleet income.

## Income comparison

**Money is a full-fleet planning estimate, not a measured future AI average:** vanilla = limit × 300 credits/min; target = target harvesters × 300 + sampled net city income. The 300-per-worker reference is applied equally to both sides. City samples use normal 7% tax and organic growth, measured over minutes 50–60, including fixture power costs but no police or additional RTS-base upkeep. Level 2 uses level 2 city samples; levels 3–9 use the level 9 reference layout. Ranges are the sampled residential-heavy mixes, not guaranteed minimum/maximum income for every allowed city. Growth, travel, combat, throughput and starting tech can keep actual income below full-fleet estimates.

The CSV separately includes measured current vanilla/Dune City harvest, tax, power and combined net income; those opening-run observations are not substituted for the configured budget. Target is unimplemented. `Δ` below includes BOTH target harvesting and city income, against the vanilla full-fleet reference. Counts/ranges on level 9 refer to different original enemy-house budgets; deltas are calculated per house before aggregating.

**Easy**

| Level | Limit: vanilla / current DC | Target H | Total RCI | Vanilla estimate/min | Target harvest/min | Target net city/min | Target combined/min | Δ credits/min | Δ % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | +0 | — |
| 2 | 1 / 1 | 1 | 1 | 300 | 300 | 32 | 332 | +32 | +11% |
| 3 | 1 / 1 | 1 | 1 | 300 | 300 | 38 | 338 | +38 | +13% |
| 4 | 1 / 1 | 1 | 1 | 300 | 300 | 38 | 338 | +38 | +13% |
| 5 | 2 / 2 | 1 | 3 | 600 | 300 | 336 | 636 | +36 | +6% |
| 6 | 2 / 2 | 1 | 3 | 600 | 300 | 336 | 636 | +36 | +6% |
| 7 | 2 / 2 | 1 | 3 | 600 | 300 | 336 | 636 | +36 | +6% |
| 8 | 2 / 2 | 1 | 3 | 600 | 300 | 336 | 636 | +36 | +6% |
| 9 | 1 to 2 / 1 to 2 | 1 | 1 to 3 | 300 to 600 | 300 | 38 to 336 | 338 to 636 | +36 to +38 | +6 to +13% |

**Medium**

| Level | Limit: vanilla / current DC | Target H | Total RCI | Vanilla estimate/min | Target harvest/min | Target net city/min | Target combined/min | Δ credits/min | Δ % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | +0 | — |
| 2 | 2 / 2 | 2 | 2 | 600 | 600 | 67 | 667 | +67 | +11% |
| 3 | 2 / 2 | 1 | 3 | 600 | 300 | 336 | 636 | +36 | +6% |
| 4 | 2 / 2 | 1 | 3 | 600 | 300 | 336 | 636 | +36 | +6% |
| 5 | 4 / 4 | 2 | 8 | 1,200 | 600 | 533 to 633 | 1,133 to 1,233 | -67 to +33 | -6 to +3% |
| 6 | 4 / 4 | 2 | 8 | 1,200 | 600 | 533 to 633 | 1,133 to 1,233 | -67 to +33 | -6 to +3% |
| 7 | 4 / 4 | 2 | 8 | 1,200 | 600 | 533 to 633 | 1,133 to 1,233 | -67 to +33 | -6 to +3% |
| 8 | 4 / 4 | 2 | 8 | 1,200 | 600 | 533 to 633 | 1,133 to 1,233 | -67 to +33 | -6 to +3% |
| 9 | 2 to 4 / 2 to 4 | 1 to 2 | 3 to 8 | 600 to 1,200 | 300 to 600 | 336 to 633 | 636 to 1,233 | -67 to +36 | -6 to +6% |

**Hard**

| Level | Limit: vanilla / current DC | Target H | Total RCI | Vanilla estimate/min | Target harvest/min | Target net city/min | Target combined/min | Δ credits/min | Δ % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4 / 4 (no economy) | 0 | 0 | 0 | 0 | 0 | 0 | +0 | — |
| 2 | 4 / 4 | 3 | 5 | 1,200 | 900 | 315 to 389 | 1,215 to 1,289 | +15 to +89 | +1 to +7% |
| 3 | 4 / 4 | 3 | 5 | 1,200 | 900 | 436 to 488 | 1,336 to 1,388 | +136 to +188 | +11 to +16% |
| 4 | 4 / 4 | 3 | 8 | 1,200 | 900 | 533 to 633 | 1,433 to 1,533 | +233 to +333 | +19 to +28% |
| 5 | 4 / 4 | 2 | 12 | 1,200 | 600 | 829 to 862 | 1,429 to 1,462 | +229 to +262 | +19 to +22% |
| 6 | 4 / 4 | 2 | 12 | 1,200 | 600 | 829 to 862 | 1,429 to 1,462 | +229 to +262 | +19 to +22% |
| 7 | 4 / 4 | 2 | 12 | 1,200 | 600 | 829 to 862 | 1,429 to 1,462 | +229 to +262 | +19 to +22% |
| 8 | 4 / 4 | 2 | 12 | 1,200 | 600 | 829 to 862 | 1,429 to 1,462 | +229 to +262 | +19 to +22% |
| 9 | 4 / 4 | 2 | 12 | 1,200 | 600 | 829 to 862 | 1,429 to 1,462 | +229 to +262 | +19 to +22% |

**Brutal**

| Level | Limit: vanilla / current DC | Target H | Total RCI | Vanilla estimate/min | Target harvest/min | Target net city/min | Target combined/min | Δ credits/min | Δ % |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 7 / 7 (no economy) | 0 | 0 | 0 | 0 | 0 | 0 | +0 | — |
| 2 | 7 / 7 | 6 | 6 | 2,100 | 1,800 | 361 to 468 | 2,161 to 2,268 | +61 to +168 | +3 to +8% |
| 3 | 7 / 7 | 5 | 12 | 2,100 | 1,500 | 829 to 862 | 2,329 to 2,362 | +229 to +262 | +11 to +12% |
| 4 | 7 / 7 | 5 | 12 | 2,100 | 1,500 | 829 to 862 | 2,329 to 2,362 | +229 to +262 | +11 to +12% |
| 5 | 7 / 7 | 4 | 16 | 2,100 | 1,200 | 1,033 to 1,323 | 2,233 to 2,523 | +133 to +423 | +6 to +20% |
| 6 | 7 / 7 | 4 | 16 | 2,100 | 1,200 | 1,033 to 1,323 | 2,233 to 2,523 | +133 to +423 | +6 to +20% |
| 7 | 7 / 7 | 3 | 20 | 2,100 | 900 | 1,628 to 1,755 | 2,528 to 2,655 | +428 to +555 | +20 to +26% |
| 8 | 7 / 7 | 3 | 20 | 2,100 | 900 | 1,628 to 1,755 | 2,528 to 2,655 | +428 to +555 | +20 to +26% |
| 9 | 7 / 7 | 3 | 20 | 2,100 | 900 | 1,628 to 1,755 | 2,528 to 2,655 | +428 to +555 | +20 to +26% |

## Current observed fleets (not the limit)

This retains the original observation separately. A peak above the runtime AI target is evidence that a target is not universally enforced; it is not permission to silently raise the budget. A built refinery calls House::freeHarvester, which checks the engine house ceiling rather than QuantBot's internal difficulty target (src/House.cpp:890–905, 1135–1137). This is one possible route around the AI target. The exact event chain for the 16-worker sample has not been replayed here; do not claim it was proven to be ally behavior or a larger nominal limit.

| Level | Easy V/DC | Medium V/DC | Hard V/DC | Brutal V/DC |
|---:|:---:|:---:|:---:|:---:|
| 1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 2 | 1 / 1 | 1 / 2 | 2 / 2 | 2 / 2 |
| 3 | 1 / 1 | 1 / 2 | 2 / 4 | 16 / 7 |
| 4 | 1 / 1 | 2 / 2 | 4 / 4 | 8 / 7 |
| 5 | 2 / 2 | 4 / 4 | 4 / 4 | 7 / 7 |
| 6 | 2 / 2 | 4 / 6 | 4 / 5 | 8 / 8 |
| 7 | 2 / 2 | 4 / 4 | 4 / 4 | 9 / 9 |
| 8 | 2 / 2 | 4 / 4 | 4 / 4 | 9 / 8 to 9 |
| 9 | 1 to 2 / 1 to 2 | 2 to 4 / 2 to 4 | 4 / 4 to 6 | 7 to 8 / 7 |

## Limits of the proposal

Easy estimates are about +6–13% over vanilla. Later Medium is roughly -6% to +3% because two harvesters are exchanged for city revenue. Later Hard is about +19–22%; late Brutal about +20–26%. Some sampled mixes underperform a replaced harvester, so no uniform uplift is claimed. Higher difficulty earns more tax in the sampled comparisons, rather than being assigned a higher tax rate. Actual capped-AI runs must validate ramp-up, mix, upkeep, surviving starting workers, all acquisition paths and other campaign houses/maps before implementation or release. The user's request to forbid new RTS building types remains a requirement, not a claim the current code already enforces it.
