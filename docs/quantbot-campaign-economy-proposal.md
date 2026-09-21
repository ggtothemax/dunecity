# QuantBot campaign income — measured comparison

This replaces the earlier estimated matrix. The old claim pairing six zones with 30 credits/min was unsupported and is withdrawn. No balance changes were made for these measurements.

## Method

Current 1.0.744 runtime, source29a649c3 (repository80ada12f adds documentation only), paired vanilla/Dune City Atreides campaigns at levels1–9, Easy/Medium/Hard/Brutal, first mission layout at each level, seed486409243, automated Easy human-side helper, up to10 simulated minutes. Each mode can end at a different time due to victory/combat; rates use its actual elapsed time, 3750cycles per game minute. These are single-seed opening-game observations, not steady-state forecasts or proof of causal tax uplift by themselves. The last user's MBA game was not available on claw.local.

The table totals **all enemy houses together**. H and zones show **sums of per-house peak observed counts**, not starting allowances or simultaneous guaranteed fleet sizes. Level8 has two principal bases and level9 three, so totals are not per single base. R/I/C is combined zones. The per-house CSV retains the individual breakdown.

All money columns are credits per simulated minute. **DC total = DC harvesting + actual net city income - power costs.** Net city income is the real change applied at city payouts (`city_net_applied`), already after police deductions and city balance clamps; it is not always gross minus nominal police charges because the city account cannot fall below zero. Raw gross tax and nominal police costs are separate CSV columns. Building purchases, refunds, combat losses and spice-storage losses are not recurring income and are excluded; the totals are operating inflows, not the final cash balance. Vanilla income is harvesting minus actual power cost (zero in these vanilla samples). Percentage compares these measured operating totals.

## Easy

| Level | Vanilla H | Vanilla income | DC H | Peak R/I/C | DC harvesting | DC net city | DC power cost | DC combined total | Change vs vanilla |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0.0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | — |
| 2 | 1 | 300.1 | 1 | 3 | 275.0 | 149.4 | 9.7 | 414.7 | +38.2% |
| 3 | 1 | 280.9 | 1 | 33 | 281.2 | 2283.1 | 42.5 | 2521.8 | +797.8% |
| 4 | 1 | 288.5 | 1 | 30 | 305.1 | 1521.4 | 36.6 | 1789.9 | +520.4% |
| 5 | 2 | 573.2 | 2 | 63 | 528.3 | 3869.5 | 90.3 | 4307.5 | +651.5% |
| 6 | 2 | 633.1 | 2 | 36 | 590.5 | 1704.5 | 85.6 | 2209.4 | +249.0% |
| 7 | 2 | 599.6 | 2 | 0 | 575.7 | 0.0 | 74.9 | 500.8 | -16.5% |
| 8 | 4 | 1257.6 | 4 | 0 | 1239.9 | 0.0 | 115.2 | 1124.7 | -10.6% |
| 9 | 4 | 1273.9 | 4 | 39 | 1166.0 | 3612.1 | 221.7 | 4556.4 | +257.7% |

## Medium

| Level | Vanilla H | Vanilla income | DC H | Peak R/I/C | DC harvesting | DC net city | DC power cost | DC combined total | Change vs vanilla |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0.0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | — |
| 2 | 1 | 292.6 | 2 | 9 | 477.7 | 370.3 | 15.8 | 832.2 | +184.4% |
| 3 | 1 | 280.9 | 2 | 29 | 457.2 | 2024.9 | 42.7 | 2439.4 | +768.4% |
| 4 | 2 | 564.2 | 2 | 12 | 458.5 | 471.8 | 23.1 | 907.2 | +60.8% |
| 5 | 4 | 948.9 | 4 | 43 | 1069.2 | 3008.9 | 79.2 | 3998.9 | +321.4% |
| 6 | 4 | 1105.1 | 6 | 31 | 1210.6 | 1501.2 | 83.3 | 2628.5 | +137.9% |
| 7 | 4 | 1059.4 | 4 | 0 | 1062.5 | 0.0 | 74.9 | 987.6 | -6.8% |
| 8 | 8 | 2299.3 | 8 | 3 | 2164.9 | 11.0 | 115.4 | 2060.5 | -10.4% |
| 9 | 8 | 2289.5 | 9 | 50 | 2268.5 | 3426.7 | 215.9 | 5479.3 | +139.3% |

## Hard

| Level | Vanilla H | Vanilla income | DC H | Peak R/I/C | DC harvesting | DC net city | DC power cost | DC combined total | Change vs vanilla |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0.0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | — |
| 2 | 2 | 197.9 | 2 | 6 | 231.6 | 88.0 | 9.9 | 309.7 | +56.4% |
| 3 | 2 | 489.4 | 4 | 29 | 808.1 | 2121.6 | 43.6 | 2886.1 | +489.7% |
| 4 | 4 | 1005.0 | 4 | 15 | 805.2 | 492.0 | 23.2 | 1274.0 | +26.8% |
| 5 | 4 | 984.0 | 4 | 47 | 1077.0 | 3219.0 | 78.0 | 4218.0 | +328.7% |
| 6 | 4 | 1131.3 | 5 | 42 | 1123.4 | 1981.9 | 96.0 | 3009.3 | +166.0% |
| 7 | 4 | 1096.7 | 4 | 0 | 1061.5 | 0.0 | 74.9 | 986.6 | -10.0% |
| 8 | 8 | 2299.8 | 8 | 19 | 2168.6 | 441.7 | 117.1 | 2493.2 | +8.4% |
| 9 | 12 | 3109.0 | 15 | 50 | 3226.1 | 3354.8 | 218.3 | 6362.6 | +104.7% |

## Brutal

| Level | Vanilla H | Vanilla income | DC H | Peak R/I/C | DC harvesting | DC net city | DC power cost | DC combined total | Change vs vanilla |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0.0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | — |
| 2 | 2 | 132.6 | 2 | 8 | 223.3 | 98.8 | 10.7 | 311.3 | +134.7% |
| 3 | 16 | 1635.6 | 7 | 24 | 1124.2 | 1712.0 | 42.9 | 2793.3 | +70.8% |
| 4 | 8 | 1323.4 | 7 | 15 | 894.7 | 536.1 | 23.9 | 1406.9 | +6.3% |
| 5 | 7 | 1474.8 | 7 | 38 | 1524.7 | 3343.2 | 77.7 | 4790.2 | +224.8% |
| 6 | 8 | 1831.0 | 8 | 43 | 1630.7 | 2100.1 | 96.9 | 3633.9 | +98.5% |
| 7 | 9 | 1722.4 | 9 | 0 | 1568.4 | 0.0 | 75.2 | 1493.2 | -13.3% |
| 8 | 18 | 3614.7 | 17 | 18 | 2902.2 | 427.0 | 118.5 | 3210.7 | -11.2% |
| 9 | 22 | 4688.8 | 21 | 53 | 3603.4 | 3406.2 | 216.1 | 6793.5 | +44.9% |

## Findings

- Level2 Easy: 275.0 harvesting +149.4 actual net city -9.7 power =414.7/min, versus300.1 vanilla (+38.2%). The bot reached three zones during this run; this is not the controlled six-zone case.
- Level3 Easy: 281.2 harvesting +2283.1 net city -42.5 power =2521.8/min, versus280.9 vanilla (+797.8%), with33 peak zones.
- Level5 Easy reached63 zones, 4307.5/min versus573.2 vanilla (+651.5%).
- Some late layouts developed little or no city income during the opening window, so their operating total could be lower than vanilla. Fixed income from a nominal zone count would hide this variation.
- Brutal exceeded its nominal seven-harvester target in some runs. The measured table includes the actual overrun rather than substituting the intended ceiling.

## Controlled zone measurements

The controlled diagnostic places newly zoned 2×2 lots and connected roads using the real placement implementation (forced fixture placement, excluding construction cost), provides four Windtraps, and uses the real city simulation plus actual House power upkeep. It suppresses player callbacks and combat/production while keeping the registered player objects alive. The measured house has zero units/harvesters. Its initial10,000-credit fixture fund is excluded from all income. Surrounding campaign structures remain frozen, so this is a controlled layout within a real campaign map, not a map-independent universal zone yield.

27 one-hour simulations completed: eight fixture types on the Harkonnen level9 layout at three seeds, plus three Atreides level2 cases. The three level9 seed repetitions produced identical city outcomes on the same fixed layout; they are not independent layout samples. Densities evolve organically; none are forced. Rates below use the actual final10 minutes (minute50 through60), not a mislabeled nine-minute interval. Every run checks unchanged structure/unit counts and reconciles receipts minus actual police/power payments with the cash change.

### Direct Level2 answer (Atreides, default7% tax)

Only the city-income columns below are measured by the controlled fixture. The combined column **calculates** one300/min harvester plus measured net city income; it is not a claim that a capped six-zone QuantBot campaign has been implemented and tested. Power costs here cover the city fixture; additional RTS-base costs are not included.

| R / I / C | Police | Measured gross taxes/min | Police paid/min | Power paid/min | Measured net city/min | Harvester assumption/min | Calculated combined/min | Calculated increase vs300 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 / 2 / 2 | 0 | 203.0 | 0.0 | 3.9 | 199.0 | 300.0 | 499.0 | +66.3% |
| 3 / 1 / 2 | 0 | 475.5 | 0.0 | 7.8 | 467.7 | 300.0 | 767.7 | +155.9% |
| 2 / 2 / 2 | 1 | 200.9 | 100.0 | 6.4 | 94.5 | 300.0 | 394.5 | +31.5% |

### Other controlled zone mixes (Harkonnen level9 layout)

Each row repeated identically at seeds486409243–245. These rows show why linear credits-per-zone assumptions are unsafe: 9 zones out-earned12 in this layout because mix, development and land value changed.

| R / I / C | Total zones | Police | Gross taxes/min | Police paid/min | Power paid/min | Net city/min |
|---|---:|---:|---:|---:|---:|---:|
| 1 / 0 / 0 | 1 | 0 | 40.0 | 0.0 | 0.2 | 39.7 |
| 1 / 1 / 1 | 3 | 0 | 147.2 | 0.0 | 2.2 | 145.1 |
| 2 / 2 / 2 | 6 | 0 | 214.6 | 0.0 | 3.9 | 210.7 |
| 1 / 2 / 3 | 6 | 0 | 141.0 | 0.0 | 2.1 | 138.9 |
| 3 / 1 / 2 | 6 | 0 | 513.3 | 0.0 | 7.8 | 505.6 |
| 2 / 2 / 2 | 6 | 1 | 206.7 | 100.0 | 6.4 | 100.2 |
| 3 / 3 / 3 | 9 | 0 | 682.0 | 0.0 | 14.8 | 667.2 |
| 4 / 4 / 4 | 12 | 0 | 630.0 | 0.0 | 11.2 | 618.7 |

## Consequences for balancing

- Six level2 zones produced approximately199–468 net credits/min in the two unpoliced mixes measured, not30. With a300/min harvester that projects approximately499–768/min total (+66–156%). These are observations from the specified fixtures, not minimum/maximum bounds for all possible six-zone cities.
- A police station brought the2R/2I/2C example to94.5 net/min, but forcing unnecessary services just to consume credits would be poor balancing.
- Withdraw the previous claimed zone-to-income mapping and do not apply its harvester reductions. A total zone cap remains useful for physical base size, but does not enforce an income budget.
- To retain six-zone cities on Easy while keeping close to the old one-harvester budget, an explicit enemy-AI city-income adjustment/budget is required. Otherwise accept a substantially higher income or restrict city development much more severely. Do not change the human player's taxes implicitly.
- Higher difficulties can replace harvesting with city income, but substitution must use actual sustained net tax receipts. For example six zones earning199/min cannot replace two300/min harvesters; a developed six-zone mix earning468/min can approximately replace one, with the remainder counted as uplift.
- Final caps by level/difficulty must be validated in actual capped QuantBot runs after the income policy is chosen. No validated future36-cell cap matrix is claimed here. The complete36-cell table above reports the current game's measured behavior.

## Reproduction and source checks

`tests/ai/run-city-income.py --output-dir <new-folder> --level 2 --house 1 --mix RICRIC --minutes 60` compiles only the diagnostic main translation unit and links the unchanged native engine objects. Use `--binary <compiled-probe>` to reuse it. `--police 1` adds one fully funded station; `--tax` explicitly overrides the default. The regular campaign diagnostic is `tests/ai/run-campaign-balance.py`; the archived comparison JSON records exact paired-run settings and ledgers.

Cadence: include/dunecity/CityEffects.h declares3750cycles per city year/game minute and one budget tick per cycle. CitySimulation::advancePhase calls the actual payout. CityEffectsRuntime.cpp records gross receipts and nominal police charges; House::addCityCredits records the actual clamped net change; House::update charges power every15seconds. House::takeCredits spends city cash first, so power expense must be measured directly rather than attributed from the non-city cash balance.

Only diagnostic files and documentation changed. No gameplay balance, save format, app version or public service was modified for this measurement task.
