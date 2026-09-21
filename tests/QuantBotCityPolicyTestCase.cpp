#include <players/QuantBotCityPolicy.h>
#include <catch2/catch_test_macros.hpp>
#include <climits>
using namespace QuantBotCityPolicy;
TEST_CASE("Custom city ceilings match approved area and difficulty matrix", "[quantbot][city]") {
    const int areas[]={1024,4096,16384,65536};
    const int expected[][4]={{5000,10000,20000,30000},{10000,20000,40000,60000},{20000,40000,80000,120000}};
    for(int d=0;d<3;++d)for(int band=0;band<4;++band) {
        REQUIRE(populationLimit(d,areas[band])==expected[d][band]);
        if(band<3) REQUIRE(populationLimit(d,areas[band]+1)==expected[d][band+1]);
    }
    REQUIRE(zoneLimit(Easy,16384)==48);
    for(int area:areas) {
        REQUIRE(populationLimit(Brutal,area)==kUnlimited);
        REQUIRE(zoneLimit(Brutal,area)==kUnlimited);
        REQUIRE(admitsZone(limits(Brutal,area),9999,999999,999999,999999));
    }
}
TEST_CASE("City admission reserves queued population and land without overflow", "[quantbot][city]") {
    const auto cap=limits(Easy,16384);
    REQUIRE(admitsZone(cap,47,19600,80,320));
    REQUIRE_FALSE(admitsZone(cap,48,0,0,20));
    REQUIRE_FALSE(admitsZone(cap,47,19600,100,320));
    REQUIRE_FALSE(allowsPopulation(INT_MAX,INT_MAX,INT_MAX,20000));
    REQUIRE_FALSE(admitsZone(cap,1,20001,0,20));
}
