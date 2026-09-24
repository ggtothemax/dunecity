#include <catch2/catch_all.hpp>
#include <dunecity/StructureDegradation.h>

namespace Decay = DuneCity::Degradation;

TEST_CASE("Degradation: house degrading amounts match the Dynasty house table", "[degradation][houses]") {
    REQUIRE(Decay::houseDegradingAmount(HOUSE_HARKONNEN) == 3);
    REQUIRE(Decay::houseDegradingAmount(HOUSE_ORDOS) == 2);
    REQUIRE(Decay::houseDegradingAmount(HOUSE_ATREIDES) == 1);
    REQUIRE(Decay::houseDegradingAmount(HOUSE_FREMEN) == 1);
    REQUIRE(Decay::houseDegradingAmount(HOUSE_SARDAUKAR) == 1);
    REQUIRE(Decay::houseDegradingAmount(HOUSE_MERCENARY) == 1);
    for(int house = HOUSE_HARKONNEN; house < NUM_HOUSES; house++) {
        const int amount = Decay::houseDegradingAmount(house);
        REQUIRE(amount >= 1);
        REQUIRE(amount <= 3);
    }
}

TEST_CASE("Degradation: power cap follows produced/required and never goes below half",
          "[degradation][power]") {
    // A house that needs no power is never capped.
    REQUIRE(Decay::powerHitpointsMax(1000, 0, 0) == 1000);
    REQUIRE(Decay::powerHitpointsMax(1000, 500, 0) == 1000);
    // Surplus and exact coverage leave full health available.
    REQUIRE(Decay::powerHitpointsMax(1000, 400, 200) == 1000);
    REQUIRE(Decay::powerHitpointsMax(1000, 200, 200) == 1000);
    // Fractional coverage scales the cap.
    REQUIRE(Decay::powerHitpointsMax(800, 3, 4) == 600);
    REQUIRE(Decay::powerHitpointsMax(1000, 900, 1000) == 898);   // 900*256/1000 = 230 (truncated)
    // At or below half coverage the floor takes over.
    REQUIRE(Decay::powerHitpointsMax(1000, 500, 1000) == 500);
    REQUIRE(Decay::powerHitpointsMax(1000, 250, 1000) == 500);
    REQUIRE(Decay::powerHitpointsMax(1000, 0, 1000) == 500);
    // Odd maxima and degenerate input stay well defined.
    REQUIRE(Decay::powerHitpointsMax(999, 0, 100) == 499);
    REQUIRE(Decay::powerHitpointsMax(0, 0, 100) == 0);
    REQUIRE(Decay::powerHitpointsMax(1000, -50, 100) == 500);
}

TEST_CASE("Degradation: legacy saves restore a sane power timer", "[degradation][save]") {
    const int interval = Decay::powerDamageIntervalCycles();
    REQUIRE(Decay::legacyPowerDamageTimer(-1) == interval);          // old "never degrades" sentinel
    REQUIRE(Decay::legacyPowerDamageTimer(0) == interval);
    REQUIRE(Decay::legacyPowerDamageTimer(interval) == interval);
    REQUIRE(Decay::legacyPowerDamageTimer(interval / 2) == interval / 2);
    REQUIRE(Decay::legacyPowerDamageTimer(interval + 1) == interval);
    REQUIRE(Decay::legacyPowerDamageTimer(999999) == interval);
}

TEST_CASE("Degradation: legacy saves restore without foundation decay", "[degradation][save]") {
    // The single legacy timer cannot identify the original foundation, so no
    // loaded structure decays from one; only power damage applies to old saves.
    REQUIRE_FALSE(Decay::legacyFoundationDegrades(-1));
    REQUIRE_FALSE(Decay::legacyFoundationDegrades(0));
    REQUIRE_FALSE(Decay::legacyFoundationDegrades(937));
}
