# Building condition and concrete

Local implementation: 1.0.776. These are DuneCity engine rules, shared by the
Vanilla/DuneCity/Dune2R/Tornie modes; they are not a claim that every economic
mechanic matches Dune Dynasty.

## Player setting

Game Rules contains **Concrete is required**. Checked enables slab construction,
missing-foundation placement damage and periodic foundation decay. Unchecked
hides slabs and rejects player slab placement; new buildings incur neither
foundation penalty. Roads and authored scenario foundations remain supported.
DuneCity defaults unchecked; Vanilla defaults checked. Explicit saved player
choices remain overrides. Power-shortage damage is independent of this setting.

## Health effects

Standard and advanced windtraps now produce `floor(nominalPower * HP / maxHP)`.
This follows Dynasty's enhanced rule, without original Dune II's 50% output
floor. Repairs restore output immediately; destroying or removing the generator
removes its remaining contribution. Saved totals are rebuilt on load.

In city mode, damage proportionally reduces the final land value of every
building's footprint, after amenities and hostile-threat effects. Half health
means half the otherwise-calculated value (integer rounding, minimum 1 on
developed land). Shared 2x2 land-value cells use the worst building condition,
not stacked multipliers. Repairs restore value at the next city effects scan.
Damaged walls also devalue their occupied land, although foundations never
damage walls. This affects existing land-value growth gates and the
owner's average land value used for tax revenue. It adds no new saved state.

## Which buildings do not directly slow down from damage?

No ordinary building is completely unaffected by missing foundations when the
setting is enabled: it loses durability, and in city mode it also loses land
value. The following functions do **not** directly scale with building health:

| Building | Function unaffected directly | Concrete decision |
| --- | --- | --- |
| Spice Silo | Storage capacity | Optional outside city mode; value/protection inside city mode |
| Radar/Outpost | Radar and prerequisite unlock | Same; power availability is a separate requirement |
| House IX | Technology unlocks | Same |
| Starport | Purchase/delivery throughput | Same; vulnerable deliveries justify protection |
| Repair Yard | Unit-repair speed | Same |
| Palace | Special-ability timer | Protection and city value; no repair-credit saving under the current high-HP repair formula |
| Police Station | Police coverage (funding, power and connectivity still matter) | City land value and resilience |
| Stadium | Amenity bonus | City land value and resilience |
| Airport | Civic/transport benefits | City land value and resilience |
| Residential, commercial, industrial zones | No separate direct HP multiplier on population | Growth and taxes are affected indirectly through land value; foundation priority in city mode |
| Gun/Rocket Turrets | Firing cadence | Always protect foundations when required: durability is critical to defence |

Mod-only equivalents without direct health scaling include Tech Center's spawn
timer, Love Factory's import function and Scoutpost variants' output/functions.
Worfinery unloading and ordinary factory production are health-sensitive. Chaos
Factory uses the ordinary health-sensitive production path. Advanced windtraps
use the same new health scaling as standard windtraps.

Walls do not suffer placement-health or foundation-decay penalties. Roads and
slabs are tile states rather than damageable buildings. They do not need slabs
beneath them.

Construction yards are a special case: placement itself does not remove half
their health, but missing-foundation decay can subsequently damage them and
slow their production. An MCV deployment is not an ordinary yard-built building
with a preceding slab order.

## Economic interpretation

At the default full-health yard speed, covering a 2x2 footprint costs 20 credits
and takes 3.84 seconds with one large slab, versus 15.36 seconds with singles.
A 3x2 footprint costs 30 credits and takes 11.52 versus 23.04 seconds. A 3x3
footprint costs 45 credits and takes 23.04 versus 34.56 seconds. Placement/AI
handling adds latency; a damaged yard slows production. Tech/mod availability
and Instant Build can change this comparison.

Building bare then repairing immediately can deliver urgent production sooner,
because repair and manufacturing run concurrently in this engine. It costs more
than slabs for most ordinary buildings. This is **not** Dynasty's repair timing:
Dynasty pauses factory production during building repairs.

Leaving a building at exactly half health avoids continuing foundation damage,
but loses durability, factory speed/refinery unloading, windtrap output and city
land value as applicable. Rich discretionary concrete outside city mode is only
credit-saving if the building would otherwise be repaired. The current repair
formula has integer `512 / maxHP`; buildings above 512 HP can repair for zero
credits (while still requiring at least 5 credits to keep repairing). Do not
justify Palace/Nuclear foundations with nonexistent repair-credit savings.

The first construction-yard upgrade costs 200 credits with the default 400-credit
yard and occupies it for about 9.6 seconds when continuously funded. One 2x2 slab
saves 11.52 seconds over four singles, so its manufacturing-time investment is
recovered on the first fully covered 2x2 footprint. The upgrade unlock still
requires the configured tech level (default level 4), and delaying the first
refinery to buy it can be a worse trade than accepting single slabs briefly.
The useful policy is early upgrade **after income exists**, not an unconditional
first action.

For the three 100-credit, 200-HP zones, repairing the initial missing-foundation
health costs about 15.6 credits, less than their 20-credit concrete footprint.
Their reason for foundations is immediate full land value and avoiding repair
latency/maintenance, not a universal claim that concrete is always cheaper.
With DuneCity's default disabled-concrete setting, none of this foundation
expense or latency is incurred.

Relevant implementation: `WindTrap.cpp`, `AdvancedWindTrap.cpp`,
`StructureBase.cpp`, `BuilderBase.cpp`, `House.cpp`, `CityEffectsRuntime.cpp`,
`CityEffects.h`, `QuantBot.cpp`, and `QuantBotBuildPolicy.h`.

## QuantBot decisions

The policy applies at every difficulty, including the separate campaign
reconstruction and power-order paths. With concrete enabled, productive
buildings and defensive emplacements receive foundations; city mode extends
that priority to every building because of land value. City zones select rock
that slabs can actually cover. Urgent recovery and cash-limited zoning may build
bare if the building is affordable but its foundation is not.

Outside that priority set, discretionary foundations require more than 5,000
spendable credits, a lower slab bill than the placement-repair bill, and enough
money for the building, slabs, economy reserve and another building-price buffer.
The threshold matches the existing general rich-building repair trigger.

The slab upgrade runs before optional infrastructure once a refinery and
harvester exist, Slab4 is enabled and technologically reachable, and funds cover
the upgrade plus the economy reserve and a refinery-price buffer. Immediate
power deficits and defensive needs retain priority. Only one yard upgrades for
this purpose at a time; concrete-disabled games do not buy this upgrade for
slabs. Damaged standard/advanced windtraps can trigger low-power repairs;
Vanilla's unrelated power bypass no longer hides a real shortage from that
repair decision.
