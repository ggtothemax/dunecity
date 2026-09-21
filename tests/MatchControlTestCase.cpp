#include <Network/MatchControlState.h>
#include <CommandValidation.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Shared pause freezes after its command cycle and resumes without a tick", "[network][pause]") {
    MatchControlState state;
    state.pauseAfter(101);
    REQUIRE_FALSE(state.pausedAt(100));
    REQUIRE(state.pausedAt(101));
    REQUIRE_FALSE(state.resume(100));
    REQUIRE(state.resume(101));
    REQUIRE_FALSE(state.pausedAt(101));
    REQUIRE_FALSE(state.resume(101));
}
TEST_CASE("Resume before a lagging pause command does not strand that peer", "[network][pause]") {
    MatchControlState state;
    REQUIRE(state.receive(3,101,101));
    state.pauseAfter(101);
    REQUIRE_FALSE(state.pausedAt(101));
    state.pauseAfter(102);
    REQUIRE(state.pausedAt(102));
    REQUIRE_FALSE(state.receive(2,101,0));
    REQUIRE(state.pausedAt(102));
}
TEST_CASE("Old host heartbeat cannot undo a newer lockstep pause", "[network][pause]") {
    MatchControlState state;
    REQUIRE(state.receive(1,0,0));
    state.pauseAfter(101);
    REQUIRE(state.receive(2,0,0));
    REQUIRE(state.pausedAt(101));
    REQUIRE_FALSE(state.receive(3,100,101));
    REQUIRE(state.receive(3,101,101));
    REQUIRE_FALSE(state.pausedAt(101));
}
TEST_CASE("Shared pause command validates exactly one request-cycle parameter", "[network][pause]") {
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_MATCH_PAUSE,1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MATCH_PAUSE,0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MATCH_PAUSE,2));
}
