// Full headless Game::updateGameState attack passes, using real weapon selection.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/Ornithopter.h>
#include <units/Launcher.h>
#include <structures/RocketTurret.h>
#include <fstream>
#include <memory>
#include <algorithm>

int runUnitSpeedProbe() {
    const std::string mod=std::getenv("UNIT_SPEED_PROBE_MOD");
    auto require=[](bool ok,const char* why) {if(!ok)throw std::runtime_error(why);};
    require(ModManager::instance().setActiveMod(mod),"Missing projectile combat mod");
    settings.general.playerName="Speed tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)),{});
    std::ofstream csv(std::string(std::getenv("UNIT_SPEED_PROBE_OUT"))+"/"+mod+".csv");
    csv << "shooter,seed,distance,heading,seconds,killed,damaged,shots,air_shots\n";
    int kills[2]={},damageCases[2]={},fired[2]={},airShots=0;
    for(int kind=0;kind<2;++kind) for(int seed=1;seed<=4;++seed)
    for(int distance : {4,8,12,18}) for(int heading : {LEFT,UP,DOWN,RIGHT}) {
        auto game=std::make_unique<Game>();currentGame=game.get();
        auto options=settings.gameOptions;options.rocketTurretsNeedPower=false;
        GameInitSettings init("projectiles.ini",mapData,"Projectile combat",true,options);init.randomSeed=seed;
        GameInitSettings::HouseInfo humans(HOUSE_ATREIDES,1);humans.addPlayerInfo({"Speed tester","HumanPlayer"});init.addHouseInfo(humans);
        GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN,2);enemies.addPlayerInfo({"Enemy","HumanPlayer"});init.addHouseInfo(enemies);
        game->initGame(init);game->initializeGameLoop();
        // Diagnostic sensitivity experiment only; never changes shipped data.
        if(const char* reload = std::getenv("AA_RELOAD_CYCLES")) {
            const int cycles = std::atoi(reload);
            require(cycles > 0 && cycles <= 10000, "Invalid diagnostic rocket reload");
            for(int house = 0; house < NUM_HOUSES; ++house)
                game->objectData.data[Structure_RocketTurret][house].weaponreloadtime = cycles;
        }
        auto* owner=game->getHouse(HOUSE_ATREIDES);
        const Coord site(20,15);
        ObjectBase* shooter=nullptr;
        if(kind==0) {auto* u=owner->createUnit(Unit_Launcher,true);u->deploy(site);shooter=u;}
        else shooter=owner->placeStructure(NONE_ID,Structure_RocketTurret,site.x,site.y,true,true);
        require(shooter!=nullptr,"Could not deploy anti-air shooter");
        shooter->setVisible(VIS_ALL,true);
        auto* aircraft=static_cast<Ornithopter*>(game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true));
        aircraft->deploy(site+Coord(distance,0));aircraft->angle=heading;aircraft->drawnAngle=heading;aircraft->setVisible(VIS_ALL,true);
        aircraft->doAttackObject(shooter,true);
        const auto targetID=aircraft->getObjectID(),shooterID=shooter->getObjectID();
        const auto hp=aircraft->getHealth();
        if(kind==0)static_cast<Launcher*>(shooter)->doAttackObject(aircraft,true);
        bool killed=false,damaged=false;int ticks=0,shots=0,orniShots=0;
        Uint32 previousShot=0;
        for(;ticks<3750;++ticks) {
            game->updateGameState();
            aircraft=dynamic_cast<Ornithopter*>(game->getObjectManager().getObject(targetID));
            auto* firingObject=game->getObjectManager().getObject(shooterID);
            if(!aircraft || aircraft->getHealth()<=0) {killed=true;break;}
            damaged |= aircraft->getHealth()<hp;
            if(aircraft->timeLastShot && aircraft->timeLastShot!=previousShot) {++orniShots;previousShot=aircraft->timeLastShot;}
            if(!firingObject || firingObject->getHealth()<=0)break;
        }
        shots=kind==0?game->combatStats.launcherRocketsSpawned:game->combatStats.turretRocketsSpawned;
        kills[kind]+=killed;damageCases[kind]+=damaged||killed;fired[kind]+=shots;airShots+=orniShots;
        csv<<kind<<','<<seed<<','<<distance<<','<<heading<<','<<ticks*.016<<','<<killed<<','<<(damaged||killed)<<','<<shots<<','<<orniShots<<'\n';
        game.reset();currentGame=nullptr;
    }
    for(int kind=0;kind<2;++kind) {
        SDL_Log("AA_COMBAT kind=%d kills=%d/64 damage=%d shots=%d",kind,kills[kind],damageCases[kind],fired[kind]);
        require(fired[kind]>0,"Anti-air shooter never fired");
        require(kills[kind]>0,"Anti-air shooter could not kill any attacking ornithopter");
    }
    require(airShots>0,"Ornithopters never fired during combat");
    csv.close();SDL_Log("UNIT_SPEED_PROBE_PASS: %s projectile combat",mod.c_str());return -1;
}
