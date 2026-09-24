# QuantBot foundations, repairs and power

Local candidate 1.0.778. This policy is shared across QuantBot difficulties and
its custom-game and campaign planning paths. No gameplay damage rates change.

## Foundations

When Concrete is required is enabled, QuantBot prioritises the construction-yard
upgrade that unlocks 2x2 concrete as soon as the actual technology and build
rules permit it. Low-tech missions retain 1x1 foundations rather than waiting
for an unavailable upgrade. With the option disabled, there are no foundation
orders or upgrades for this purpose.

Ordinary constructed buildings receive a complete prepared foundation before
placement. Existing roads count. Walls, roads and slabs need no foundations;
an MCV's deployed construction yard is not a yard construction order.

Large slabs may extend beyond the building footprint to finish a 1x2 or 2x1
strip. Placement must remain within the map, on legal terrain, avoid existing
structures and preserve roads. Singles remain a fallback for isolated tiles
or sites where a large slab cannot fit safely. Planned coverage must account
for earlier slab orders, and final building placement must recheck actual
coverage so an interrupted slab sequence cannot place a damaged building.

## Repairs

QuantBot starts repairs below full health for buildings whose production,
unloading or generation depends on health. It also repairs structures whose
actual integer repair cost is zero. Other structures qualify when available
funds exceed 5,000 credits, while survival and engaged-defence repair rules
remain relevant.

DuneCity extends proactive repairs to its structures because condition affects
land value even where the direct building function does not scale with health.
The engine still needs at least five credits to advance repairs. Building
repairs can run alongside ordinary production.

## Vanilla power investment

Vanilla keeps the initial windtrap needed for prerequisites. Subsequent
investment in generation is an economic decision after its income and
technology-appropriate core factories exist. It considers raw supply/demand,
restorable generator output, pending generation, operating reserves and
ongoing income. Its capacity target includes the power demand of queued buildings
and a reserve of 20% of anticipated demand, with a minimum of one windtrap's
output. It tops up this reserve while currently powered, instead of waiting for
the next completed building to cause a shortage. It must not mistake a large starting balance for sustained
income or buy duplicate generation while repairs can close the shortage.

The investment comparison covers the capacity required to reach that target,
including foundations when required. Forecast income must replace that capital
within 30 game seconds. Earlier investment requires a 10-minute avoided-repair
payback; a prosperous base with more than 5,000 spare credits after the operating
reserve may invest despite a longer repair-only payback. This avoids permanently
underpowering an otherwise well-funded, established base.
Pending output counts toward the target so multiple yards cannot overbuy it.
If the buffered package fails those checks while an operating shortage remains,
the same checks can approve the smaller shortage-only package. This preserves
affordable recovery while the base saves for its full reserve.
DuneCity retains its operational power priorities because power also controls
city growth, policing and aircraft deployment.

Repair prices follow the existing integer expression
`floor(512 / maximumHP) * buildingPrice / 1280` per HP. Power damage is roughly
four HP per game minute. Free-to-repair buildings therefore add no repair-credit
benefit to new generation. This policy does not alter that repair formula.
