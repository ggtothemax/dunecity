// Anti-air audit fixture: real Game::updateGameState encounters between powered
// rocket turrets and Ornithopters/Carryalls. Diagnostic only; changes no balance.
//
// Scenario names describe what the engine actually does:
//   idle_orbit  - no order is given. AirUnit::navigate() (src/units/AirUnit.cpp:145)
//                 always sets moving=true, so an unordered aircraft orbits; it is
//                 NOT stationary. Path/displacement columns record the motion.
//   attack      - Ornithopter ordered onto the first turret (Carryall cannot attack).
//   cross       - straight flyby: the aircraft is ordered to the far map edge and the
//                 encounter ends when it crosses a fixed exit plane while travelling
//                 left, or when it dies. Never uses !moving as a stop condition.
//   pinned_diag - diagnostic only: aircraft speed is zero for this fixture. This
//                 freezes actual movement, including within a cycle, to verify
//                 damage independently of tracking. No positions are teleported.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <sand.h>
#include <mmath.h>
#include <DynastyProjectile.h>
#include <dunecity/PowerRules.h>
#include <mod/ModManager.h>
#include <units/Ornithopter.h>
#include <units/Carryall.h>
#include <structures/RocketTurret.h>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace {

struct TurretView {
    Uint32 id = NONE_ID;
    int lastWeaponTimer = 0;
};

// src/structures/TurretBase.cpp:31-33 defines these with internal linkage, so the
// probe mirrors them verbatim instead of linking to them.
const FixPoint kProbeRocketFireTolerance = 15.0_fix / 45.0_fix;
const FixPoint kProbeHalfCircle = 4_fix;
const FixPoint kProbeFullCircle = 8_fix;

// Identity of a live bullet: the pointer alone is not enough because the
// allocator can recycle an address in the same cycle a bullet detonates.
using BulletKey = std::array<Sint32, 5>;
BulletKey bulletKey(const Bullet* b) {
    return {static_cast<Sint32>(b->shooterID), static_cast<Sint32>(b->bulletID), b->damage,
            b->source.x, b->source.y};
}

// Mirrors TurretBase::updateStructureSpecificStuff's rocket firing gate
// (src/structures/TurretBase.cpp:116-124) so the probe can report aiming state for
// Carryalls too (combatStats only counts Ornithopters). Reads production state; it
// does not drive the simulation.
bool turretAimedAt(const RocketTurret* turret, const ObjectBase* aircraft) {
    const int wanted = destinationDrawnAngle(turret->getLocation(), aircraft->getClosestPoint(turret->getLocation()));
    FixPoint delta = FixPoint::abs(turret->angle - FixPoint(wanted));
    if(delta > kProbeHalfCircle) delta = kProbeFullCircle - delta;
    return delta <= kProbeRocketFireTolerance;
}

struct EncounterSpec {
    std::string scenario;
    int airItem = Unit_Ornithopter;
    int houseIndex = 0;
    int turretCount = 1;
    int distance = 8;
    int seed = 1;
    bool pinned = false;
    bool diagnostic = false;
    int limit = 1250;
};

} // namespace

int runUnitSpeedProbe() {
    const std::string mod = std::getenv("UNIT_SPEED_PROBE_MOD");
    const std::string outDir = std::getenv("UNIT_SPEED_PROBE_OUT");
    auto require = [](bool ok, const char* why) { if(!ok) throw std::runtime_error(why); };
    require(ModManager::instance().setActiveMod(mod), "Missing aircraft AA mod");
    settings.general.playerName = "AA tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});

    std::ofstream csv(outDir + "/" + mod + ".csv");
    csv << "mod,scenario,diagnostic,aircraft,house,turrets,distance,seed,rockets_need_power,"
           "produced_power,required_power,start_hp,end_hp,health_loss_capped,killed,"
           "acquire_tick,target_ticks,inrange_ticks,aimed_ticks,"
           "shots_total,shots_rocket,shots_cannon,shots_weapontimer,"
           "damage_events,rocket_damage_events,cannon_damage_events,max_drop,"
           "min_distance_tiles,first_shot_tick,path_tiles,net_disp_tiles,max_disp_tiles,"
           "max_intracycle_excursion_tiles,"
           "crossed_turret_line,exit_crossed,cs_targets_orni,cs_fires_on_orni,cs_fire_blocked,"
           "cs_right_angle,cs_wrong_angle,cs_lost_target,turrets_alive,turret_hp,ticks,seconds,outcome\n";
    std::ofstream tbl(outDir + "/" + mod + "-tables.csv");
    tbl << "mod,item,house,hitpoints,weapondamage,weaponrange,weaponreloadtime,maxspeed,turnspeed,"
           "viewrange,power,maxspeed_tiles_per_s,weaponreloadtime_s\n";
    std::ofstream flags(outDir + "/" + mod + "-flags.csv");
    flags << "mod,key,value\n";

    bool tablesWritten = false;
    int crossEscapes = 0, crossTimeouts = 0, encounters = 0;
    // Measured once from the first real projectile of each kind that a turret fires.
    double rocketSpeed = -1, cannonSpeed = -1;
    int rocketFuse = -1, cannonFuse = -1, rocketDamage = -1, cannonDamage = -1;

    std::vector<EncounterSpec> specs;
    for(const char* scenario : {"idle_orbit", "attack", "cross"})
    for(int air = 0; air < 2; ++air)
    for(int houseIndex = 0; houseIndex < 2; ++houseIndex)
    for(int turretCount : {1, 3})
    for(int distance : {4, 8, 12})
    for(int seed = 1; seed <= 3; ++seed) {
        // A Carryall never attacks, so the attacking scenario is Ornithopter-only.
        if(air == 1 && std::string(scenario) == "attack") continue;
        EncounterSpec spec;
        spec.scenario = scenario;
        spec.airItem = (air == 0) ? Unit_Ornithopter : Unit_Carryall;
        spec.houseIndex = houseIndex;
        spec.turretCount = turretCount;
        spec.distance = distance;
        spec.seed = seed;
        spec.limit = 1250;  // 20 s of normal game time (16 ms per cycle)
        specs.push_back(spec);
    }
    // Diagnostic direct impacts: aircraft held in place, close (cannon) and mid (rocket).
    for(int air = 0; air < 2; ++air)
    for(int distance : {2, 5}) {
        EncounterSpec spec;
        spec.scenario = "pinned_diag";
        spec.airItem = (air == 0) ? Unit_Ornithopter : Unit_Carryall;
        spec.turretCount = 1;
        spec.distance = distance;
        spec.seed = 1;
        spec.pinned = true;
        spec.diagnostic = true;
        spec.limit = 1875;  // 30 s
        specs.push_back(spec);
    }

    for(const auto& spec : specs) {
        auto game = std::make_unique<Game>();
        currentGame = game.get();
        auto options = settings.gameOptions;
        // Powered-turret configuration: the requirement is ENABLED and satisfied with
        // real WindTraps placed away from the encounter (see below).
        options.rocketTurretsNeedPower = true;
        GameInitSettings init("aircraft-aa.ini", mapData, "Aircraft AA", true, options);
        init.randomSeed = spec.seed;
        const int defenderHouse = (spec.houseIndex == 0) ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
        const int attackerHouse = (spec.houseIndex == 0) ? HOUSE_HARKONNEN : HOUSE_ATREIDES;
        GameInitSettings::HouseInfo defender(static_cast<HOUSETYPE>(defenderHouse), 1);
        defender.addPlayerInfo({"AA tester", "HumanPlayer"});
        init.addHouseInfo(defender);
        GameInitSettings::HouseInfo attacker(static_cast<HOUSETYPE>(attackerHouse), 2);
        attacker.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(attacker);
        game->initGame(init);
        game->initializeGameLoop();

        if(!tablesWritten) {
            for(int item : {Structure_RocketTurret, Structure_GunTurret, Structure_WindTrap,
                            Unit_Ornithopter, Unit_Carryall, Unit_Launcher})
            for(int house = 0; house < NUM_HOUSES; ++house) {
                const auto& d = game->objectData.data[item][house];
                // maxspeed/weaponreloadtime are per game cycle; GAMESPEED_DEFAULT is the
                // millisecond length of one cycle (include/Definitions.h:97).
                tbl << mod << ',' << item << ',' << house << ',' << d.hitpoints << ',' << d.weapondamage << ','
                    << d.weaponrange << ',' << d.weaponreloadtime << ',' << d.maxspeed.toDouble() << ','
                    << d.turnspeed.toDouble() << ',' << d.viewrange << ',' << d.power << ','
                    << d.maxspeed.toDouble() / TILESIZE / (GAMESPEED_DEFAULT / 1000.0) << ','
                    << d.weaponreloadtime * (GAMESPEED_DEFAULT / 1000.0) << '\n';
            }
            // Dynasty reference geometry for the same projectiles, plus the exact
            // conversions this audit relies on.
            flags << mod << ",gamespeed_ms_per_cycle," << GAMESPEED_DEFAULT << '\n';
            flags << mod << ",rocketTurretsNeedPower," << options.rocketTurretsNeedPower << '\n';
            flags << mod << ",gameType," << static_cast<int>(game->gameType) << '\n';
            for(int type : {Bullet_TurretRocket, Bullet_Rocket}) {
                const auto p = DynastyProjectile::parameters(type);
                flags << mod << ",dynasty_step_" << type << ',' << p.step << '\n';
                flags << mod << ",dynasty_turn_" << type << ',' << p.turn << '\n';
                flags << mod << ",dynasty_delay_" << type << ',' << p.delay << '\n';
                // 256/tile per Dynasty movement update; DynastyProjectile runs 20 of
                // those per second (conversion asserted in tests/units/projectile-audit.inc:66).
                flags << mod << ",dynasty_speed_tiles_per_s_" << type << ',' << p.step * 20.0 / 256 << '\n';
            }
            tablesWritten = true;
        }

        auto* owner = game->getHouse(defenderHouse);
        auto* enemy = game->getHouse(attackerHouse);
        require(owner && enemy, "AA fixture houses missing");

        const Coord site(20, 15);
        std::vector<TurretView> turrets;
        std::vector<Uint32> turretIDs;
        for(int i = 0; i < spec.turretCount; ++i) {
            auto* s = owner->placeStructure(NONE_ID, Structure_RocketTurret, site.x, site.y + 2 * i, true, true);
            require(s != nullptr, "Could not place rocket turret");
            s->setVisible(VIS_ALL, true);
            turrets.push_back({s->getObjectID(), 0});
            turretIDs.push_back(s->getObjectID());
        }

        // Real power supply, far away from the encounter (>= 15 tiles from the turrets),
        // so the powered path in RocketTurret::updateStructureSpecificStuff is taken.
        for(int i = 0; i < 6 && owner->getProducedPower() < owner->getPowerRequirement() + 50; ++i) {
            owner->placeStructure(NONE_ID, Structure_WindTrap, 3, 3 + 3 * i, true, true);
        }
        const int produced = owner->getProducedPower();
        const int required = owner->getPowerRequirement();
        require(produced >= required, "Powered AA fixture is under-powered");
        require(DuneCity::rocketTurretPowered(true, produced, required), "Turret power gate not satisfied");

        if(spec.pinned)
            game->objectData.data[spec.airItem][attackerHouse].maxspeed = 0;
        auto* aircraft = static_cast<AirUnit*>(enemy->createUnit(spec.airItem, true));
        require(aircraft != nullptr, "Could not create aircraft");
        const Coord start = site + Coord(spec.distance, 0);
        aircraft->deploy(start);
        if(spec.pinned) aircraft->currentMaxSpeed = 0;
        aircraft->angle = LEFT;
        aircraft->drawnAngle = LEFT;
        aircraft->setVisible(VIS_ALL, true);
        const Uint32 airID = aircraft->getObjectID();
        const int startHP = lround(aircraft->getHealth());
        const FixPoint pinX = aircraft->realX, pinY = aircraft->realY;
        const double startX = pinX.toDouble(), startY = pinY.toDouble();

        if(spec.scenario == "attack") {
            aircraft->doAttackObject(game->getObjectManager().getObject(turrets.front().id), true);
        } else if(spec.scenario == "cross") {
            // Fly straight through the turret line and on to the far map edge, so the
            // aircraft is still under a real move order when it reaches the exit plane.
            aircraft->doMove2Pos(1, site.y, true);
        }

        // World-x of the fixed exit plane, mirrored about the turret column.
        const FixPoint turretLineX = FixPoint(site.x * TILESIZE + TILESIZE / 2);
        const FixPoint exitX = FixPoint((site.x - spec.distance) * TILESIZE + TILESIZE / 2);

        int acquireTick = -1, targetTicks = 0, inRangeTicks = 0, aimedTicks = 0;
        int shotsRocket = 0, shotsCannon = 0, shotsTimer = 0;
        int damageEvents = 0, rocketDamageEvents = 0, cannonDamageEvents = 0, maxDrop = 0;
        int ticks = 0, firstShotTick = -1;
        int lastHP = startHP;
        double minDistance = 1e9, pathTiles = 0.0, maxDisp = 0.0, maxExcursion = 0.0;
        double prevX = startX, prevY = startY;
        bool crossedLine = false, exitCrossed = false;
        const char* outcome = "timeout";
        std::map<const Bullet*, BulletKey> liveBullets;

        for(; ticks < spec.limit; ++ticks) {
            game->updateGameState();
            auto* live = dynamic_cast<AirUnit*>(game->getObjectManager().getObject(airID));
            const bool dead = (live == nullptr) || (live->getHealth() <= 0);

            // Firing state is read BEFORE the loop can exit, so shots fired on the
            // killing cycle are counted.
            std::map<const Bullet*, BulletKey> current;
            int vanishedRocket = 0, vanishedCannon = 0;
            for(const auto* b : bulletList) {
                if(std::find(turretIDs.begin(), turretIDs.end(), b->shooterID) == turretIDs.end()) continue;
                const BulletKey key = bulletKey(b);
                current[b] = key;
                const auto it = liveBullets.find(b);
                if(it == liveBullets.end() || it->second != key) {
                    // speed is world units (64 per tile) travelled per game cycle.
                    const double tilesPerSecond = b->speed.toDouble() / TILESIZE / (GAMESPEED_DEFAULT / 1000.0);
                    if(b->bulletID == Bullet_ShellTurret) {
                        ++shotsCannon;
                        if(cannonSpeed < 0) { cannonSpeed = tilesPerSecond; cannonFuse = b->detonationTimer; cannonDamage = b->damage; }
                    } else {
                        ++shotsRocket;
                        if(rocketSpeed < 0) { rocketSpeed = tilesPerSecond; rocketFuse = b->detonationTimer; rocketDamage = b->damage; }
                    }
                    if(firstShotTick < 0) firstShotTick = ticks;
                }
            }
            for(const auto& [ptr, key] : liveBullets) {
                const auto it = current.find(ptr);
                if(it != current.end() && it->second == key) continue;
                if(key[1] == Bullet_ShellTurret) ++vanishedCannon; else ++vanishedRocket;
            }
            liveBullets.swap(current);

            for(auto& view : turrets) {
                auto* turret = dynamic_cast<RocketTurret*>(game->getObjectManager().getObject(view.id));
                if(turret == nullptr) { view.lastWeaponTimer = 0; continue; }
                // Independent cross-check of the projectile count: weaponTimer rises
                // from 0 exactly when TurretBase::attack/RocketTurret::attack fires.
                if(turret->weaponTimer > view.lastWeaponTimer) ++shotsTimer;
                view.lastWeaponTimer = turret->weaponTimer;
            }

            // Health accounting, including the fatal decrement.
            const int hp = dead ? 0 : lround(live->getHealth());
            if(hp < lastHP) {
                ++damageEvents;
                maxDrop = std::max(maxDrop, lastHP - hp);
                // Only attribute a weapon when exactly one of our projectiles ended
                // on this cycle; otherwise the event stays unattributed.
                if(vanishedRocket + vanishedCannon == 1) {
                    if(vanishedCannon == 1) ++cannonDamageEvents; else ++rocketDamageEvents;
                }
            }
            lastHP = hp;

            if(dead) { outcome = "killed"; ++ticks; break; }

            if(spec.pinned) {
                maxExcursion = std::max(maxExcursion,
                    std::hypot((live->realX - pinX).toDouble(),
                               (live->realY - pinY).toDouble()) / TILESIZE);
                require(live->realX == pinX && live->realY == pinY,
                        "Stationary diagnostic aircraft moved");
            }

            // Motion evidence is sampled from the actual engine position.
            const double x = live->realX.toDouble(), y = live->realY.toDouble();
            pathTiles += std::hypot(x - prevX, y - prevY) / TILESIZE;
            maxDisp = std::max(maxDisp, std::hypot(x - startX, y - startY) / TILESIZE);
            const bool movingLeft = x < prevX;
            prevX = x; prevY = y;

            bool anyTargeting = false, anyInRange = false, anyAimed = false;
            for(auto& view : turrets) {
                auto* turret = dynamic_cast<RocketTurret*>(game->getObjectManager().getObject(view.id));
                if(turret == nullptr) continue;
                const double d = std::hypot(double(turret->getCenterPoint().x - live->getCenterPoint().x),
                                            double(turret->getCenterPoint().y - live->getCenterPoint().y)) / TILESIZE;
                minDistance = std::min(minDistance, d);
                if(turret->target.getObjPointer() == live) {
                    anyTargeting = true;
                    if(turret->targetInWeaponRange()) {
                        anyInRange = true;
                        if(turretAimedAt(turret, live)) anyAimed = true;
                    }
                }
            }
            if(anyTargeting) {
                ++targetTicks;
                if(acquireTick < 0) acquireTick = ticks;
            }
            inRangeTicks += anyInRange;
            aimedTicks += anyAimed;

            if(spec.scenario == "cross") {
                if(live->realX <= turretLineX) crossedLine = true;
                if(live->realX <= exitX && movingLeft) {
                    exitCrossed = true;
                    outcome = "escaped";
                    ++ticks;
                    break;
                }
            }
        }

        const bool killed = std::string(outcome) == "killed";
        const int endHP = killed ? 0 : lastHP;
        if(spec.scenario == "cross") {
            if(std::string(outcome) == "escaped") ++crossEscapes;
            else if(!killed) ++crossTimeouts;
        }
        ++encounters;

        int alive = 0, turretHP = 0;
        for(const auto& view : turrets) {
            auto* turret = game->getObjectManager().getObject(view.id);
            if(turret && turret->getHealth() > 0) { ++alive; turretHP += lround(turret->getHealth()); }
        }
        const auto& cs = game->combatStats;
        csv << mod << ',' << spec.scenario << ',' << (spec.diagnostic ? 1 : 0) << ','
            << (spec.airItem == Unit_Ornithopter ? "Ornithopter" : "Carryall") << ','
            << defenderHouse << ',' << spec.turretCount << ',' << spec.distance << ',' << spec.seed << ','
            << 1 << ',' << produced << ',' << required << ','
            << startHP << ',' << endHP << ',' << (startHP - endHP) << ',' << (killed ? 1 : 0) << ','
            << acquireTick << ',' << targetTicks << ',' << inRangeTicks << ',' << aimedTicks << ','
            << (shotsRocket + shotsCannon) << ',' << shotsRocket << ',' << shotsCannon << ',' << shotsTimer << ','
            << damageEvents << ',' << rocketDamageEvents << ',' << cannonDamageEvents << ',' << maxDrop << ','
            << minDistance << ',' << firstShotTick << ',' << pathTiles << ',' << std::hypot(prevX - startX, prevY - startY) / TILESIZE << ','
            << maxDisp << ',' << maxExcursion << ',' << (crossedLine ? 1 : 0) << ',' << (exitCrossed ? 1 : 0) << ','
            << cs.rocketTurretTargetsOrni << ',' << cs.rocketTurretFiresOnOrni << ',' << cs.rocketTurretFireBlocked << ','
            << cs.orniInRangeCorrectAngle << ',' << cs.orniInRangeButWrongAngle << ',' << cs.rocketTurretLosesOrniTarget << ','
            << alive << ',' << turretHP << ',' << ticks << ',' << ticks * (GAMESPEED_DEFAULT / 1000.0) << ',' << outcome << '\n';

        game.reset();
        currentGame = nullptr;
    }

    flags << mod << ",measured_turret_rocket_tiles_per_s," << rocketSpeed << '\n';
    flags << mod << ",measured_turret_rocket_detonation_timer," << rocketFuse << '\n';
    flags << mod << ",measured_turret_rocket_damage," << rocketDamage << '\n';
    flags << mod << ",measured_turret_cannon_tiles_per_s," << cannonSpeed << '\n';
    flags << mod << ",measured_turret_cannon_detonation_timer," << cannonFuse << '\n';
    flags << mod << ",measured_turret_cannon_damage," << cannonDamage << '\n';
    flags << mod << ",cross_escapes," << crossEscapes << '\n';
    flags << mod << ",cross_timeouts," << crossTimeouts << '\n';
    flags << mod << ",encounters," << encounters << '\n';

    csv.close();
    tbl.close();
    flags.close();
    SDL_Log("UNIT_SPEED_PROBE_ENCOUNTERS: %d cross_escapes=%d cross_timeouts=%d", encounters, crossEscapes, crossTimeouts);
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s aircraft anti-air", mod.c_str());
    return -1;
}
