#ifndef CARRYALLFLIGHT_H
#define CARRYALLFLIGHT_H

#include <Definitions.h>
#include <fixmath/FixPoint.h>
#include <algorithm>

// Dune Dynasty's normal-speed carryall handling, expressed at our fixed
// simulation rate. Movement stays continuous; no script/timer state is added.
namespace CarryallFlight {
inline FixPoint distance(FixPoint dx, FixPoint dy) {
    dx = FixPoint::abs(dx); dy = FixPoint::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy)/2;
}

inline FixPoint approachSpeed(FixPoint distance, FixPoint headingError, FixPoint cruiseSpeed) {
    // Script_Unit_MoveToTarget uses 256 position and heading units per tile
    // and revolution respectively, then Unit_SetSpeed quantizes the result.
    const int range = std::min(255, (distance * 256 / TILESIZE / 8).floor());
    const int diff = std::clamp((headingError * 32).lround(), 0, 128);
    const int throttle = (range * (255-diff) + 128) / 256;
    int speed = 200 * throttle / 256;
    if(speed >= 16) speed = (speed/16)*16;
    // Below 16, Dynasty moves intermittently. Preserve its average rate;
    // 192 is the reference full-throttle movement step.
    return cruiseSpeed * speed / 192;
}

inline FixPoint finalStep(FixPoint cruiseSpeed) {
    // Dynasty: 16/256 tile per axis every (delay 2 + execution 1)*5/60 s
    // = 0.25 tile/s, one sixtieth of the 15 tile/s cruise cap.
    return cruiseSpeed / 60;
}
}
#endif
