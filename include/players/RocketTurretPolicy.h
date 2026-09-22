#ifndef ROCKET_TURRET_POLICY_H
#define ROCKET_TURRET_POLICY_H
#include <dunecity/CityEffects.h>
#include <tuple>
#include <mmath.h>

namespace RocketTurretPolicy {
inline int defenseWeight(int item) {
    if (item == Structure_NuclearPlant) return 2;
    // Every real building needs protection, including outlying R/C/I districts.
    // Surface tiles and defensive emplacements do not recursively demand turrets.
    if (!isStructure(item) || item == Structure_Slab1 || item == Structure_Slab4
        || item == Structure_Road || item == Structure_PowerLine || item == Structure_Wall
        || item == Structure_GunTurret || item == Structure_RocketTurret) return 0;
    return 1;
}
inline bool coversBuilding(Coord turret, Coord origin, Coord size, int range) {
    // All corners must fit the actual octile weapon range. A square radius
    // around the centre incorrectly counts exposed diagonal edges as covered.
    for(int y : {0,size.y-1}) for(int x : {0,size.x-1})
        if(blockDistance(turret,Coord(origin.x+x,origin.y+y))>range) return false;
    return true;
}
// Losing one of these ends the city's ability to rebuild, to refine or to
// produce, so they are worth overlapping cover; everything else is replaceable
// while the core stands.
inline bool coreAsset(int item) {
    return item == Structure_ConstructionYard || item == Structure_Refinery
        || item == Structure_HeavyFactory || item == Structure_LightFactory
        || item == Structure_HighTechFactory || item == Structure_StarPort
        || item == Structure_RepairYard || item == Structure_NuclearPlant;
}
// How many turrets should have a core asset in range. Tier is 0 Easy,
// 1 Medium, 2 Hard, 3 Brutal: a single emplacement dies to the first raid that
// focuses it, so from Medium up the core keeps a second one, and Brutal a
// third. Ordinary buildings stay at one; the reactor keeps its own two-turret
// minimum at every difficulty because its loss damages the whole district.
inline int desiredCoverage(int item, int tier) {
    if (defenseWeight(item) <= 0) return 0;
    const int core = tier <= 0 ? 1 : tier >= 3 ? 3 : 2;
    int coverage = coreAsset(item) ? core : 1;
    if (item == Structure_NuclearPlant) coverage = std::max(coverage, 2);
    return coverage;
}
// Weight each core asset as three ordinary buildings when choosing coverage.
// This favours rebuild and income capacity while still valuing dense districts.
inline int assetPriority(int item) {
    if (defenseWeight(item) <= 0) return 0;
    return coreAsset(item) ? 3 : 1;
}
// A goal counted from the enemy's wing size stops while whole districts are
// still uncovered, and the interim cap of two emplacements left large bases
// exposed until optional tech finished. Scale the interim ceiling with the
// coverage the base actually demands: a two-turret baseline plus one per four
// demanded covers. It is a ceiling, never a build target on its own.
inline int coverageTurretCap(int coverageDemand) {
    return 2 + std::max(0, coverageDemand) / 4;
}
inline int amenityBenefit(int landValue, bool alreadyCovered, int terrainGain) {
    if (alreadyCovered) return 0;
    return std::min(std::max(0, DuneCity::kMaxLandValue - landValue), std::max(0, terrainGain));
}
struct Score {
    int defense = 0, junction = 0, amenity = 0, proximity = 0;
    bool useful() const { return defense > 0 || (junction > 0 && amenity > 0); }
    bool betterThan(const Score& other) const {
        return std::tie(defense, junction, amenity, proximity)
             > std::tie(other.defense, other.junction, other.amenity, other.proximity);
    }
};
}
#endif
