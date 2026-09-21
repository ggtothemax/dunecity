# Revised QuantBot campaign balance proposal — 21 September 2026

**Superseded:** this matrix incorrectly used observed opening fleets as vanilla limits. Use [the corrected refinery-formula comparison](quantbot-campaign-economy-comparison.md), which includes current limits, observed counts, revised targets and combined-income deltas.

Proposal only. This supersedes the chat proposal with fixed per-type R/I/C quotas and an artificial AI tax cap. No gameplay changes are included.

## Design

Use a shared TOTAL R+I+C ceiling, counting queued and placed zones. QuantBot chooses the mix from demand, jobs and city health; residential-heavy is valid and expected. Six zones is not a required Easy starting point. Ordinary tax calculation stays in place: no AI-only income clipping or tax-rate multiplier is proposed here. Higher difficulty permits a larger tax-producing city and gives up more of the vanilla harvester allowance. Physical caps alone cannot guarantee an exact credits/min ceiling, because density, placement and mix change income.

Treat the table as established-economy harvester ceilings and zone permissions per enemy house, not free starting buildings/units. Preserve the mission's existing starting assets. Grow the city before lowering harvester replacement targets, and do not invent a Light Factory or another RTS building type absent at mission start. House/type and existing scripted-asset rules still apply. If the actual city cannot support its intended substitution, delay the transition rather than grant compensating cash. Final control logic and live capped-AI runs remain to be implemented/validated.

## Joint matrix

Each cell is **harvester ceiling / combined zone ceiling**. Initial campaign layouts can have fewer available harvesters than these permissions.

| Level | Easy | Medium | Hard | Brutal |
|---:|:---:|:---:|:---:|:---:|
| 1 | 0 H / 0 zones | 0 H / 0 zones | 0 H / 0 zones | 0 H / 0 zones |
| 2 | 1 H / 1 zones | 1 H / 2 zones | 1 H / 5 zones | 1 H / 6 zones |
| 3 | 1 H / 1 zones | 1 H / 2 zones | 1 H / 5 zones | 4 H / 12 zones |
| 4 | 1 H / 1 zones | 1 H / 3 zones | 3 H / 6 zones | 4 H / 12 zones |
| 5 | 1 H / 3 zones | 3 H / 3 zones | 3 H / 8 zones | 3 H / 16 zones |
| 6 | 1 H / 3 zones | 3 H / 3 zones | 3 H / 10 zones | 3 H / 16 zones |
| 7 | 1 H / 3 zones | 2 H / 8 zones | 2 H / 12 zones | 2 H / 20 zones |
| 8 | 1 H / 3 zones | 2 H / 8 zones | 2 H / 12 zones | 2 H / 20 zones |
| 9 | 1 H / 3 zones* | 2 H / 8 zones* | 2 H / 12 zones | 2 H / 20 zones |

*Level9 has different starting refinery counts across enemy houses. A house with the original one-refinery vanilla budget uses Easy1H/1zone or Medium1H/3zones; a two-refinery-budget house uses the larger table entry. A zero-economy scripted house does not gain a new economy from this table. Generalize the same original-house-budget constraint to other campaign variants rather than granting a larger allowance solely from a level number. Standard sampled maps were32×32 atlevels1–2,62×62 later. Smaller variants may require a smaller physical cap; bigger map area alone is not permission to exceed the original economy budget.

## Why these differ from the rejected proposal

- Easy levels2–4 retain one harvester and only ONE zone. Their sampled vanilla allowance is one harvester; a mature three-zone residential-heavy city can itself approach that income.
- Once the sampled vanilla Easy budget rises to two harvesters atlevel5, trade one harvester for a small three-zone city. This grows the visible city without adding its entire income on top of two harvesters.
- Medium uses1H/2zones atlevels2–3, then1H/3zones atlevel4; its higher four-harvester vanilla allowance atlevels5–6 becomes3H/3zones, then2H/8zones.
- Hard starts at1H/5zones against the two-harvester fleet observed in the early vanilla samples; later it trades the four-harvester reference for3H and ultimately2H/12zones.
- Brutal starts at1H/6zones against the two-harvester fleet observed onlevel2. Fromlevel3, its intended seven-harvester ceiling is the planning reference:4H/12zones,3H/16zones and finally2H/20zones. Tax capacity replaces harvesting in substantial steps. Existing over-limit vanilla harvester behavior is a separate defect, not the balancing anchor.
- Plateauing caps is deliberate: if the underlying vanilla economy and map size do not grow, the city should not gain arbitrary extra income just because the level number increases.

## Measurement basis

These are real organic city growth observations, not assigned R/I/C quotas. Default7% tax; no police in these fixtures; power costs paid; no AI, combat or harvesters in the controlled measurement; final10minutes of60 simulated minutes. Atreides level2 and9 layouts, seed486409243. Two residential-heavy mixes per eligible count were sampled: roughly half and roughly two-thirds residential, with remaining zones split into C/I. For1–2 zones use residential only. This expands the earlier27-fixture evidence with 28 additional successful cases. Larger fixtures supply enough WindTraps for fully grown zones; all traces stay powered with zero units and constant structure counts. The ranges are just these sampled mixes on specified layouts, not bounds across every legal mix/map/density.

| Map reference | Total zones | Measured net city credits/min across sampled mixes |
|---|---:|---:|
| Level2 | 1 | 31.8 |
| Level2 | 2 | 66.5 |
| Level2 | 3 | 101.8 |
| Level2 | 4 | 99.3–175.1 |
| Level2 | 5 | 315.3–389.2 |
| Level2 | 6 | 361.0–467.7 |
| Level9 | 1 | 37.7 |
| Level9 | 2 | 76.5 |
| Level9 | 3 | 335.7 |
| Level9 | 4 | 352.8–396.6 |
| Level9 | 5 | 435.8–487.7 |
| Level9 | 6 | 271.8–407.5 |
| Level9 | 8 | 533.1–633.0 |
| Level9 | 10 | 481.3–666.3 |
| Level9 | 12 | 828.7–861.7 |
| Level9 | 16 | 1033.0–1322.9 |
| Level9 | 20 | 1627.9–1754.8 |

## Illustrative income checks (not capped-AI forecasts)

Use approximately300/min per functioning harvester solely as a planning reference. Measured city income stays separate:

- Level2 Easy:300 harvesting +31.8 measured city ≈332/min versus300 vanilla (+11%).
- Level2 Medium:300 harvesting +66.5 city ≈367/min versus the300/min one-harvester reference (+22%).
- Level2 Hard:300 harvesting +315–389 city ≈615–689/min versus the600/min two-harvester reference (+3–15%).
- Level2 Brutal:300 harvesting +361–468 city ≈661–768/min versus the600/min two-harvester reference (+10–28%).
- Later two-refinery-budget Easy:300 harvesting +336 sampled three-zone city ≈636/min versus600 vanilla (+6%).
- Later Medium:600 harvesting +533–633 sampled eight-zone city ≈1133–1233/min versus1200 vanilla (-6% to+3%).
- Later Hard:600 harvesting +829–862 sampled twelve-zone city ≈1429–1462/min versus1200 vanilla (+19–22%).
- Later Brutal:600 harvesting +1628–1755 sampled twenty-zone city ≈2228–2355/min versus2100 vanilla (+6–12%).

Early references are observed fleet sizes in the existing single-seed opening samples, not a claim that those are universal hard limits. Preserve scenario tech and assets; verify other layouts and steady fleets before implementation. This avoids budgeting early opponents as though they already run their larger nominal late-game fleets.

The later tax contribution therefore rises substantially by difficulty: about336,533–633,829–862,1628–1755 per minute in the sampled examples. This does not promise identical actual tax in every game, or exactly increasing percentages over different vanilla baselines. City growth and harvester travel/attrition still vary. The normal base's non-city operating costs, mission starting assets, ramp-up and combats must be included in the next live AI verification. No universal per-zone income or guaranteed match-income percentage is claimed.

## Remaining implementation work

Shared queued+placed zone counting; original mission-start RTS building permissions; city-funded reduction of harvester replenishment; bounded power/service expansion; then real QuantBot runs across campaign levels, houses, layouts and difficulties. All remain proposal work, not part of app744. The player retains ordinary tax and city behavior.
