#ifndef QUANTBOT_FOUNDATION_POLICY_H
#define QUANTBOT_FOUNDATION_POLICY_H

#include <data.h>
#include <SDL_stdinc.h>
#include <Definitions.h>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Concrete foundations
//
// A building placed on unprepared ground loses half its health on placement and
// then decays back to that floor whenever it is repaired. QuantBot therefore
// prepares the whole footprint of every ordinary building before it orders it:
// there is no rich/urgent/selective exception left, because a half-health
// building is a permanent loss of production, power, land value or armour.
// Walls, roads and the slabs themselves are tile states with no such penalty
// and are never founded. A construction yard that an MCV deploys is never
// queued, so it cannot be prepared in advance either.
//
// This planner turns one footprint into an ordered list of slab orders. It
// prefers the 2x2 slab, including placements that hang outside the footprint to
// cover a residual 1x2 or 2x1 strip, and falls back to single slabs for an
// isolated tile, a one-tile emplacement, or wherever no legal bulk slab fits.
// It tracks planned coverage to avoid duplicate paving where space permits
// and never leaves a footprint tile bare.
// ---------------------------------------------------------------------------

namespace QuantBotFoundationPolicy {

struct SlabOrder {
    Uint32 item = NONE_ID;
    int x = 0, y = 0;
};

struct Plan {
    std::vector<SlabOrder> orders;
    // False when some footprint tile cannot legally be prepared. The caller
    // must then defer the building rather than place it on bare ground.
    bool complete = false;
};

// Everything the planner needs to know about one tile. `paveable` is the
// engine's own placement rule for a slab: inside the map, rock (never
// mountain), not blocked by a unit or a structure and not a city zone.
// A road is prepared ground already, but paving it would destroy it, so it is
// reported separately and never enters a slab order.
struct TileState {
    bool exists = false;
    bool prepared = false;
    bool road = false;
    bool paveable = false;
    bool inBuildRange = false;
};

// The largest distance a slab may hang outside the footprint it prepares.
constexpr int bulkOverhang = 1;

namespace detail {
struct Candidate {
    Uint32 item = NONE_ID;
    int x = 0, y = 0;
    int covered = 0;   // footprint tiles this order newly prepares
    int duplicate = 0; // tiles it pays for that are prepared or planned already
    int extra = 0;     // new ground beside the footprint that it also prepares
    bool valid = false;
    int continuation = 0; // useful bulk coverage left without re-paving this order
};

// Deterministic on every peer and after loading a save. Useful coverage first;
// then the least concrete paid for twice, because that buys nothing at all;
// then the best remaining bulk coverage and least spill. A residual strip is therefore
// prepared by a 2x2 slab hanging outside the building — new ground that also
// extends build reach — rather than by one that re-paves the slab beside it.
inline bool better(const Candidate& a, const Candidate& b) {
    if (!b.valid) return a.valid;
    if (!a.valid) return false;
    if (a.covered != b.covered) return a.covered > b.covered;
    if (a.duplicate != b.duplicate) return a.duplicate < b.duplicate;
    if (a.continuation != b.continuation) return a.continuation > b.continuation;
    if (a.extra != b.extra) return a.extra < b.extra;
    if (a.y != b.y) return a.y < b.y;
    if (a.x != b.x) return a.x < b.x;
    return a.item < b.item;
}
} // namespace detail

// `query(x, y)` returns the TileState of one map tile. `buildRange` is the
// engine's BUILDRANGE: a slab we place becomes our ground, so it extends the
// reach available to the slabs planned after it.
template<class Query>
Plan planFoundation(int x0, int y0, int width, int height,
                    bool bulkAvailable, bool singleAvailable,
                    Query query, int buildRange = BUILDRANGE) {
    Plan plan;
    if (width <= 0 || height <= 0) return plan;

    // Cache the window the plan can touch: the footprint plus the overhang a
    // 2x2 slab may use on each side.
    const int windowX = x0 - bulkOverhang, windowY = y0 - bulkOverhang;
    const int windowW = width + 2 * bulkOverhang, windowH = height + 2 * bulkOverhang;
    std::vector<TileState> tiles(size_t(windowW) * windowH);
    std::vector<char> covered(tiles.size(), 0);
    auto index = [&](int x, int y) {
        return size_t(y - windowY) * windowW + size_t(x - windowX);
    };
    auto inWindow = [&](int x, int y) {
        return x >= windowX && x < windowX + windowW && y >= windowY && y < windowY + windowH;
    };
    for (int y = windowY; y < windowY + windowH; ++y)
        for (int x = windowX; x < windowX + windowW; ++x) {
            const auto state = query(x, y);
            tiles[index(x, y)] = state;
            covered[index(x, y)] = state.prepared ? 1 : 0;
        }

    auto footprint = [&](int x, int y) {
        return x >= x0 && x < x0 + width && y >= y0 && y < y0 + height;
    };
    int remaining = 0;
    for (int y = y0; y < y0 + height; ++y)
        for (int x = x0; x < x0 + width; ++x) {
            const auto& state = tiles[index(x, y)];
            if (!state.exists) return plan; // Not a legal footprint at all.
            if (!covered[index(x, y)]) ++remaining;
        }
    if (remaining == 0) { plan.complete = true; return plan; }

    // Our own planned slabs anchor construction exactly like placed ones.
    std::vector<SlabOrder> placed;
    auto reachable = [&](int x, int y) {
        if (inWindow(x, y) && tiles[index(x, y)].inBuildRange) return true;
        for (const auto& order : placed) {
            const int span = order.item == Structure_Slab4 ? 1 : 0;
            for (int dy = 0; dy <= span; ++dy)
                for (int dx = 0; dx <= span; ++dx)
                    if (std::abs(x - (order.x + dx)) <= buildRange
                        && std::abs(y - (order.y + dy)) <= buildRange) return true;
        }
        return false;
    };
    // A slab may only be laid on ground the engine accepts, and never on a
    // road: House::placeStructure clears the road flag of every tile it paves.
    auto slabGround = [&](int x, int y) {
        if (!inWindow(x, y)) return false;
        const auto& state = tiles[index(x, y)];
        return state.exists && state.paveable && !state.road;
    };

    while (remaining > 0) {
        detail::Candidate best;
        if (bulkAvailable) {
            for (int py = y0 - bulkOverhang; py < y0 + height; ++py)
                for (int px = x0 - bulkOverhang; px < x0 + width; ++px) {
                    detail::Candidate candidate{Structure_Slab4, px, py, 0, 0, 0, true};
                    bool anchored = false;
                    for (int dy = 0; dy < 2 && candidate.valid; ++dy)
                        for (int dx = 0; dx < 2 && candidate.valid; ++dx) {
                            const int x = px + dx, y = py + dy;
                            if (!slabGround(x, y)) { candidate.valid = false; break; }
                            anchored |= reachable(x, y);
                            if (covered[index(x, y)]) ++candidate.duplicate;
                            else if (footprint(x, y)) ++candidate.covered;
                            else ++candidate.extra;
                        }
                    if (!candidate.valid || !anchored || candidate.covered == 0) continue;
                    // Choose the first slab so a residual strip can use the free
                    // side. Otherwise a map-order tie can force overlap even
                    // when starting from the other end permits an overhang.
                    for (int ny = y0 - bulkOverhang; ny < y0 + height; ++ny)
                        for (int nx = x0 - bulkOverhang; nx < x0 + width; ++nx) {
                            bool legal = true;
                            int useful = 0;
                            for (int dy = 0; dy < 2 && legal; ++dy)
                                for (int dx = 0; dx < 2 && legal; ++dx) {
                                    const int x = nx + dx, y = ny + dy;
                                    if (!slabGround(x,y) || covered[index(x,y)]
                                        || (x >= px && x < px+2 && y >= py && y < py+2)) {
                                        legal = false;
                                        break;
                                    }
                                    useful += footprint(x,y);
                                }
                            if (legal) candidate.continuation = std::max(candidate.continuation,useful);
                        }
                    if (detail::better(candidate, best)) best = candidate;
                }
        }
        if (singleAvailable) {
            for (int y = y0; y < y0 + height; ++y)
                for (int x = x0; x < x0 + width; ++x) {
                    if (covered[index(x, y)] || !slabGround(x, y) || !reachable(x, y)) continue;
                    const detail::Candidate candidate{Structure_Slab1, x, y, 1, 0, 0, true};
                    if (detail::better(candidate, best)) best = candidate;
                }
        }
        if (!best.valid) break;
        plan.orders.push_back({best.item, best.x, best.y});
        placed.push_back({best.item, best.x, best.y});
        const int span = best.item == Structure_Slab4 ? 2 : 1;
        for (int dy = 0; dy < span; ++dy)
            for (int dx = 0; dx < span; ++dx) {
                const int x = best.x + dx, y = best.y + dy;
                if (!inWindow(x, y) || covered[index(x, y)]) continue;
                covered[index(x, y)] = 1;
                if (footprint(x, y)) --remaining;
            }
    }
    plan.complete = remaining == 0;
    if (!plan.complete) plan.orders.clear();
    return plan;
}

} // namespace QuantBotFoundationPolicy
#endif
