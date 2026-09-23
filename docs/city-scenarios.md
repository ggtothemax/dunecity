# Arrakis city scenarios

Three standalone **single-player** missions, with one Atreides human slot and
computer-controlled opposing houses. The `2P`/`3P` filenames count houses, not
required human players. Play with the **DuneCity** mod in **Custom Game → SP Maps**
(or **SP User Maps** for copies installed in the user map directory).

These form a proposed campaign order, with a shared Atreides viewpoint. They
are not yet linked into the campaign menu. The briefings below and in each INI
header provide the narrative material for that conversion; the current custom
map loader does not display those header comments as a briefing screen.

| Order | Map | Objective | Starting problem |
| --- | --- | --- | --- |
| 1 | Sihaya Basin, 128×128 | Defeat the Harkonnen within 60 minutes | Establish a profitable frontier town while defending its spice economy |
| 2 | Ash Quarter, 128×128 | Defeat the Harkonnen and Mercenaries | Repair a damaged refinery town, restore policing and reclaim its northern works |
| 3 | Coriolis Gap, 160×96 | Defeat the Sardaukar and Harkonnen | Secure the mountain passes between your districts and the enemy city |

In every mission, losing your house loses the game. Sihaya also has a losing
timeout. Victory uses the engine's normal enemy-defeat condition; the fixed
500-internal-population economic condition is deliberately disabled because
these prebuilt cities could satisfy it within seconds.

## Mission briefings

### 1. Sihaya Basin

The basin's company town has survived for generations without prospering.
CHOAM will review our claim in one hour, and the Harkonnen intend to seize the
spice fields before then. You have inherited a working town, factories,
refineries and an airfield. Protect its harvesters, use the city's income to
build an army, and destroy the Harkonnen base before the audit.

Atreides starts with 9,000 credits; Harkonnen with 7,000. Two compact works
areas serve nearby housing and commercial streets. Rock outcrops offer room
to expand. Enemy reinforcements begin at minute 9, with armour at 20 and 32;
later waves repeat. Friendly harvesters arrive at 14 and 30 minutes.

Inspiration: Micropolis **Dullsville**—developing a stagnant town—adapted into
a timed military and economic challenge using existing DuneCity mechanics.

### 2. Ash Quarter

Our advance has recovered a ruined refinery town. Its workshops still stand,
but damaged buildings, missing police coverage and an isolated northern annex
threaten the recovery. Harkonnen raiders and Mercenary scavengers hold the
surrounding shelves. Repair the production core, restore the neighbourhoods,
and remove both hostile houses so this town can support the next advance.

Atreides starts with 6,500 credits. Many buildings begin at reduced health;
33 residential lots lie outside starting police/turret coverage. Industry is
oversupplied, making rezoning and service placement useful choices. The annex
has a separate local road network. Hostile reinforcements begin at minutes 7
and 13; friendly relief arrives from minute 6. There is no time limit.

Inspiration: Micropolis **Detroit**'s urban recovery and **Hamburg**'s rebuilding
brief. Damage is a starting condition, not a scripted earthquake or firestorm.

### 3. Coriolis Gap

Our towns are ready to support an advance, but Sardaukar troops hold the
western pass and a Harkonnen city controls the eastern approach. Secure the
passes, connect your forces and harvesting routes, then break the enemy city.
Each district has local housing, commerce and employment: the blockade is a
tactical obstacle, not an impossible long-distance commute.

Atreides starts with 8,000 credits. Western freight depots use Spice Silos'
industrial role without adding pollution. Barracks and a WOR supply nearby
residents to the central works. Mountain walls, pass garrisons, road stubs,
spice fields and an established enemy city create several fronts. Harkonnen
reinforcements begin at minute 8; Sardaukar troops reinforce every 15 minutes.
There is no time limit.

Inspiration: Micropolis **Bern**'s transport and rezoning problem, translated
into compact viable neighbourhoods separated by a contested military corridor.

## SimCity repair

The original 192×192 SimCity map had large separated R/C/I districts. The real
engine found complementary road destinations for only **487 of 1,087 zones**.
The replacement repeats compact mixed blocks and uses Dune infrastructure as
city destinations. All **1,222 zones** now pass the same engine route check.

Each city's roads form one connected network. Terrain, mission flags, CHOAM
and player sections are preserved. The generator also repairs orphaned
Player3 sandworms by assigning them to the neutral Fremen house.

## Layout and reproduction

Zones retain their 2×2 gameplay footprint. Complementary roles must be
reachable within 20 road steps; commerce also needs nearby residential and
industrial supply. Roads are distinct from concrete and power is global.

Industrial roles include construction yards, refineries, silos, factories,
repair yards and starports. Outposts, House IX and airports are commercial;
barracks, WORs and palaces are residential. Silos, starports and construction
yards have industrial roles but do not emit industrial pollution. Windtraps
and nuclear plants supply power and have no R/C/I role.

```sh
python3 scripts/gen_city_scenarios.py
python3 scripts/pack_simcity_user_map.py
python3 scripts/validate_city_maps.py
```

Generators are deterministic. The validator checks format, legal placements,
overlap, road graphs, real-role traffic destinations, supply, pollution,
services, ownership, objectives and reinforcement syntax. Ash Quarter's
under-policed housing is an explicit scenario exception; disconnected local
trade is not an exception in any new map.

Micropolis reference: `MicropolisCore/resources/data/scenarios.xml` and
`strings_en-US.xml`, notices 50 (Dullsville), 52 (Hamburg), 53 (Bern), 55
(Detroit). Existing DuneCity single-player maps, Twin Cities and campaign
reinforcement/team formats informed the bases and mission structure.

## Verification and limits

Local native build and core CTest suite passed. Structural validation passes
for all four maps. Real-engine checks cover loading, rendered screenshots,
zone connectivity and automated opening simulations. All new missions completed
12 simulated minutes with a passive human slot and Medium enemy QuantBots;
SimCity completed five minutes. No map-loader warnings occurred. Initial route
checks passed for 237/237 Sihaya zones, 315/315 Ash zones, 432/432 Coriolis
zones and 1,222/1,222 SimCity zones. These are single-seed
smoke checks, not a full human difficulty or mission-completion playtest.
An unattended human base is vulnerable to the active opponents; repair,
policing, storage expansion and military production remain player decisions.

For a later campaign conversion, retain the shared Atreides viewpoint and this
order, move the briefings into campaign text resources, assign mission slots,
and recheck tech unlocks, campaign AI and win/lose transitions. These standalone
maps currently use tech level 8 and do not carry armies or money between games.
