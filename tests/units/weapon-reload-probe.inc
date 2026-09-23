// Normal-speed shot timestamp controls: full engine loop, stationary target,
// healthy or damaged shooter, no retaliation. Diagnostic target HP/speed only.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/Launcher.h>
#include <units/AirUnit.h>
#include <structures/RocketTurret.h>
#include <fstream>
#include <memory>
#include <map>

int runUnitSpeedProbe() {
    const std::string mod = std::getenv("UNIT_SPEED_PROBE_MOD");
    auto require = [](bool ok, const char* why) { if(!ok) throw std::runtime_error(why); };
    require(ModManager::instance().setActiveMod(mod), "Missing reload-probe mod");
    settings.general.playerName = "Reload tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    std::ofstream csv(std::string(std::getenv("UNIT_SPEED_PROBE_OUT")) + "/" + mod + ".csv");
    csv << "shooter,health_percent,target,seed,shot,tick,seconds,projectile,primary_timer,secondary_timer\n";
    for(int kind = 0; kind < 3; ++kind)
    for(int health = 0; health < (kind == 2 ? 3 : 1); ++health)
    for(int air = 0; air < 2; ++air)
    for(int seed = 1; seed <= 3; ++seed) {
        const char* name = kind == 0 ? "rocket_turret" : kind == 1 ? "turret_cannon" : "launcher";
        const int percent = health == 0 ? 100 : health == 1 ? 50 : 33;
        const int distance = kind == 1 ? 2 : 8;
        auto game = std::make_unique<Game>(); currentGame = game.get();
        auto options = settings.gameOptions; options.rocketTurretsNeedPower = true;
        GameInitSettings init("reload.ini", mapData, "Reload control", true, options); init.randomSeed = seed;
        GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
        humans.addPlayerInfo({"Reload tester", "HumanPlayer"}); init.addHouseInfo(humans);
        GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
        enemies.addPlayerInfo({"Target", "HumanPlayer"}); init.addHouseInfo(enemies);
        game->initGame(init); game->initializeGameLoop();
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        const Coord site(20, 15);
        Launcher* launcher = nullptr; RocketTurret* turret = nullptr; ObjectBase* shooter = nullptr;
        if(kind == 2) {
            launcher = static_cast<Launcher*>(owner->createUnit(Unit_Launcher, true));
            launcher->deploy(site); launcher->angle = RIGHT; launcher->drawnAngle = RIGHT;
            launcher->setHealth(launcher->getMaxHealth() * percent / 100); shooter = launcher;
        } else {
            turret = static_cast<RocketTurret*>(owner->placeStructure(NONE_ID, Structure_RocketTurret, site.x, site.y, true, true));
            require(turret != nullptr, "Missing turret"); shooter = turret;
            owner->placeStructure(NONE_ID, Structure_WindTrap, 3, 3, true, true);
            require(owner->getProducedPower() >= owner->getPowerRequirement(), "Unpowered turret");
        }
        const int targetType = air ? Unit_Carryall : Unit_Tank;
        auto& targetData = game->objectData.data[targetType][HOUSE_HARKONNEN];
        targetData.maxspeed = 0; targetData.hitpoints = 10000;
        auto* target = game->getHouse(HOUSE_HARKONNEN)->createUnit(targetType, true);
        target->deploy(site + Coord(distance, 0)); target->doSetAttackMode(STOP);
        target->numWeapons = 0; target->setVisible(VIS_ALL, true);
        if(auto* flying = dynamic_cast<AirUnit*>(target)) flying->currentMaxSpeed = 0;
        const auto targetX = target->realX, targetY = target->realY;
        if(launcher) launcher->doAttackObject(target, true);
        else turret->doAttackObject(target);
        int shots = 0, timerShots = 0, oldPrimary = 0, oldSecondary = -1;
        std::map<const Bullet*, int> seen;
        for(int tick = 0; tick < 7500; ++tick) {
            game->updateGameState();
            require(target->getHealth() > 0 && shooter->getHealth() > 0, "Control actor died");
            require(target->realX == targetX && target->realY == targetY, "Control target moved");
            const int primary = launcher ? launcher->primaryWeaponTimer : turret->weaponTimer;
            const int secondary = launcher ? launcher->secondaryWeaponTimer : -1;
            if(primary > oldPrimary) ++timerShots;
            if(launcher && secondary == -1 && oldSecondary == 0 && !launcher->isBadlyDamaged()) ++timerShots;
            oldPrimary = primary; oldSecondary = secondary;
            std::map<const Bullet*, int> current;
            for(const auto* b : bulletList) {
                if(b->shooterID != shooter->getObjectID()) continue;
                const auto previous = seen.find(b);
                if(previous == seen.end() || b->detonationTimer > previous->second) {
                    ++shots;
                    csv << name << ',' << percent << ',' << (air ? "air" : "ground") << ',' << seed << ','
                        << shots << ',' << tick << ',' << tick * 0.016 << ',' << b->bulletID << ','
                        << primary << ',' << secondary << '\n';
                }
                current[b] = b->detonationTimer;
            }
            seen.swap(current);
        }
        require(shots >= 4 && shots == timerShots, "Shot/timer accounting mismatch");
        game.reset(); currentGame = nullptr;
    }
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s weapon reload timestamps", mod.c_str());
    return -1;
}
