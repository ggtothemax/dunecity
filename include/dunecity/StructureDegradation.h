/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DUNECITY_STRUCTURE_DEGRADATION_H
#define DUNECITY_STRUCTURE_DEGRADATION_H

#include <DataTypes.h>
#include <Definitions.h>
#include <sand.h>

#include <algorithm>

/**
    Dynasty-aligned structure degradation.

    Dune Dynasty runs two independent mechanisms, and this header is the single
    place both of them are defined so the rules are identical in every mode
    (Vanilla, DuneCity, Dune2R, Tornie) and can be exercised by tests:

    1. Power shortage (`Structure_CalculateHitpointsMax`, src/structure.c):
       every house tick (900 ticks = 15 s) each structure has a damage threshold of
       `maxHealth * produced / required`, never below half, and loses exactly
       one absolute hitpoint while it is above that cap. It is driven by the
       owner's raw produced/required power, not by House::hasPower(): vanilla
       switches power requirements off wholesale for unrelated behaviour, and
       that must not silently disable degradation.

    2. Foundation decay (`tickDegrade`, src/structure.c): every 10800 ticks
       (180 s) a structure that was placed on an incomplete foundation loses
       its house's degrading amount (src/table/houseinfo.c), but only while it
       is above half health. Dynasty gates this on `g_campaignID > 1`; campaign
       IDs are 0-based there, so the first two campaign levels are exempt.

    Neither mechanism depends on the other. A fully prepared foundation only
    suppresses (2); power damage still applies.
*/
namespace DuneCity {
namespace Degradation {

/// Dynasty house tick: 900 ticks at 60 ticks/s.
constexpr int kPowerDamageIntervalMs = 15 * 1000;
/// Dynasty g_tickStructureDegrade: 10800 ticks at 60 ticks/s.
constexpr int kFoundationDecayIntervalMs = 180 * 1000;
/// Structure_CalculateHitpointsMax() deals exactly one hitpoint per tick.
constexpr int kPowerDamagePerInterval = 1;
/// Fixed-point scale Dynasty uses for the produced/required power fraction.
constexpr int kPowerFractionScale = 256;
/// Dynasty's `g_campaignID > 1` on 0-based campaign IDs: levels 1 and 2 exempt.
constexpr int kLastExemptCampaignLevel = 2;

constexpr int powerDamageIntervalCycles() { return MILLI2CYCLES(kPowerDamageIntervalMs); }
constexpr int foundationDecayIntervalCycles() { return MILLI2CYCLES(kFoundationDecayIntervalMs); }

/**
    Damage one foundation-decay tick deals, from Dynasty's HouseInfo table
    (src/table/houseinfo.c `degradingAmount`).
    \param  houseID     house owning the structure
    \return 3 for Harkonnen, 2 for Ordos, 1 for every other house
*/
constexpr int houseDegradingAmount(int houseID) {
    switch(houseID) {
        case HOUSE_HARKONNEN:   return 3;
        case HOUSE_ORDOS:       return 2;
        default:                return 1;
    }
}

/**
    Health cap a house's power supply allows, mirroring Dynasty's
    Structure_CalculateHitpointsMax(). A house that needs no power is at full
    health; otherwise the cap scales with produced/required and never drops
    below half the structure's maximum health.
    \param  maxHealth           maximum health of the structure
    \param  producedPower       power the owner currently produces
    \param  powerRequirement    power the owner currently needs
    \return the lowest health power shortage alone may reduce this structure to
*/
constexpr int powerHitpointsMax(int maxHealth, int producedPower, int powerRequirement) {
    if(maxHealth <= 0) return 0;

    int fraction = kPowerFractionScale;
    if(powerRequirement > 0) {
        const long long produced = std::max(0, producedPower);
        fraction = static_cast<int>(std::min<long long>(
                        produced * kPowerFractionScale / powerRequirement, kPowerFractionScale));
    }

    const int scaled = static_cast<int>(static_cast<long long>(maxHealth) * fraction / kPowerFractionScale);
    return std::max(scaled, maxHealth / 2);
}

/// Foundation decay only runs above this threshold; its final hit may cross it.
constexpr int foundationDecayFloor(int maxHealth) { return maxHealth > 0 ? maxHealth / 2 : 0; }

/**
    Is foundation decay suppressed for this game? Only the first two campaign
    levels are exempt (Dynasty `g_campaignID > 1`); custom games, skirmish and
    multiplayer always decay. Power damage is never exempt.
    \param  gameType        type of the running game
    \param  missionNumber   campaign mission number (0 outside the campaign)
*/
inline bool foundationDecayExempt(GameType gameType, int missionNumber) {
    if(!isCampaignGameType(gameType)) return false;
    if(missionNumber <= 0) return false;
    return missionNumberToLevelNumber(missionNumber) <= kLastExemptCampaignLevel;
}

/**
    Power-damage timer to restore from a pre-9847 save.

    Older saves stored one `degradeTimer`, counting the same 15 s the power tick
    now uses, with -1 meaning "never degrades". Out-of-range values (including
    that sentinel) restart a full interval.
*/
constexpr int legacyPowerDamageTimer(int legacyDegradeTimer) {
    return (legacyDegradeTimer <= 0 || legacyDegradeTimer > powerDamageIntervalCycles())
            ? powerDamageIntervalCycles()
            : legacyDegradeTimer;
}

/**
    Does a pre-9847 structure decay from its foundation?

    No: the legacy timer cannot identify one. Its -1 sentinel was only written
    when the removed "Structures Degrade On Concrete" option was off, so with
    the default option every structure — fully concreted or not — stored a
    running timer. Rather than guess, loaded structures start without foundation
    decay; anything built afterwards records its real foundation. Power damage
    is unaffected, so old saves still behave under the new power rules.
*/
constexpr bool legacyFoundationDegrades(int /*legacyDegradeTimer*/) { return false; }

} // namespace Degradation
} // namespace DuneCity

#endif // DUNECITY_STRUCTURE_DEGRADATION_H
