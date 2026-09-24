#ifndef AIR_STRIKE_POLICY_H
#define AIR_STRIKE_POLICY_H

#include <data.h>
#include <mmath.h>
#include <algorithm>
#include <vector>

namespace AirStrikePolicy {
inline bool antiAir(int item) {
    return item == Structure_RocketTurret || item == Unit_Launcher
        || item == Unit_EliteLauncher || item == Unit_Deviator;
}

// Home and harvester defence preempts raids on exposed enemy structures.
// Unrelated ground units are never hunted across the map.
constexpr int RaidRank = 1;
constexpr int DefenseRank = 2;
// An attacker actually hitting something we own. A remote worker rescue and an
// attack on the base itself are both emergencies, but they are not the same
// emergency: the base outranks the field, so a wing already saving a harvester
// is recalled by an attack on the city and not the other way round. Without
// that separation every aircraft re-chose its target on every volley.
constexpr int UnderAttackRank = 3;
constexpr int BaseUnderAttackRank = 4;
inline int targetRank(bool structure, bool defensiveContact) {
    return defensiveContact ? DefenseRank : structure ? RaidRank : 0;
}
inline int underAttackRank(bool attackingBase) {
    return attackingBase ? BaseUnderAttackRank : UnderAttackRank;
}
// Ranks that describe a present loss rather than an opportunity. They may be
// approached through anti-air cover and they outrank every raid.
inline bool emergencyRank(int rank) { return rank >= UnderAttackRank; }
// A forced interception stands until its target dies or becomes unreachable;
// only a strictly more urgent class of emergency may replace it. Equal-rank
// score differences are not a reason to abandon a live attack run.
inline bool holdsInterception(int heldRank, int bestRank) {
    return heldRank > 0 && bestRank <= heldRank;
}
inline int safetyRange(int weaponRange) { return weaponRange + 5; }

// One shared coverage map per tactical pass; no per-target scan of all weapons.
// Use the combat distance metric, including diagonal range, plus manoeuvre room.
class Coverage {
public:
    Coverage(int width, int height) : width_(width), height_(height), blocked_(width*height,false) {}
    void add(Coord centre, int range) {
        for (int y=std::max(0,centre.y-range);y<=std::min(height_-1,centre.y+range);++y)
            for (int x=std::max(0,centre.x-range);x<=std::min(width_-1,centre.x+range);++x)
                if (blockDistance(centre,Coord(x,y))<=range) blocked_[y*width_+x]=true;
    }
    bool safe(Coord point) const {
        return point.x>=0 && point.y>=0 && point.x<width_ && point.y<height_
            && !blocked_[point.y*width_+point.x];
    }
    bool clearFootprint(Coord origin, Coord size) const {
        for (int y=0;y<size.y;++y) for(int x=0;x<size.x;++x)
            if (!safe(Coord(origin.x+x,origin.y+y))) return false;
        return true;
    }
    bool clearApproach(Coord from, Coord to) const {
        const int steps=std::max({1,std::abs(to.x-from.x),std::abs(to.y-from.y)});
        for(int step=0;step<=steps;++step)
            if (!safe(Coord(from.x+(to.x-from.x)*step/steps,from.y+(to.y-from.y)*step/steps))) return false;
        return true;
    }
    // Escape may start inside newly arrived AA coverage, but cannot re-enter it.
    bool clearWithdrawal(Coord from, Coord to) const {
        if (!safe(to)) return false;
        bool reachedSafety = safe(from);
        const int steps=std::max({1,std::abs(to.x-from.x),std::abs(to.y-from.y)});
        for(int step=0;step<=steps;++step) {
            const bool clear=safe(Coord(from.x+(to.x-from.x)*step/steps,
                from.y+(to.y-from.y)*step/steps));
            if(reachedSafety && !clear) return false;
            reachedSafety = reachedSafety || clear;
        }
        return true;
    }
    Coord escape(Coord from) const {
        // Closest safe straight exit; deterministic and used only during danger.
        Coord best; best.invalidate();
        int distance=width_+height_;
        for(int y=0;y<height_;++y) for(int x=0;x<width_;++x) {
            const Coord point(x,y);
            const int d=std::abs(x-from.x)+std::abs(y-from.y);
            if(d<distance && safe(point) && clearWithdrawal(from,point)) {
                best=point; distance=d;
            }
        }
        return best;
    }
private:
    int width_,height_;
    std::vector<bool> blocked_;
};
} // namespace AirStrikePolicy
#endif
