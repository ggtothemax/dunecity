#ifndef MATCHCONTROLSTATE_H
#define MATCHCONTROLSTATE_H

#include <algorithm>
#include <cstdint>

// Wall-clock match controls, separate from deterministic world state. A pause
// command finishes its simulation cycle; resume can arrive without another tick.
struct MatchControlState {
    std::uint32_t revision = 0;
    std::uint32_t pauseCycle = 0;
    std::uint32_t resumedPauseCycle = 0;

    bool pausedAt(std::uint32_t cycle) const {
        return pauseCycle > resumedPauseCycle && cycle >= pauseCycle;
    }
    void pauseAfter(std::uint32_t cycle) { pauseCycle = std::max(pauseCycle, cycle); }
    bool resume(std::uint32_t cycle) {
        if (cycle == 0 || cycle != pauseCycle || resumedPauseCycle >= cycle) return false;
        resumedPauseCycle = cycle;
        return true;
    }
    bool receive(std::uint32_t nextRevision, std::uint32_t nextPause, std::uint32_t nextResume) {
        if (!nextRevision || nextRevision <= revision || nextResume > nextPause) return false;
        revision = nextRevision;
        // The local lockstep pause command can precede an older host heartbeat.
        // Conversely resume can arrive before a lagging peer executes that command.
        pauseCycle = std::max(pauseCycle, nextPause);
        resumedPauseCycle = std::max(resumedPauseCycle, nextResume);
        return true;
    }
};
#endif
