// Controlled projectile audit using production Bullet::update and Map::damage.
// This measures mechanics; it is not a battle win-rate benchmark.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>
#include <units/InfantryBase.h>
#include <units/Harvester.h>
#include <units/Ornithopter.h>
#include <units/TankBase.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <Bullet.h>
#include <algorithm>
#include <DynastyProjectile.h>

int runUnitSpeedProbe() {
    const std::string mod = std::getenv("UNIT_SPEED_PROBE_MOD");
    const bool baseline = std::string(std::getenv("UNIT_SPEED_PROBE_BASELINE")) == "1";
    auto require = [](bool ok, const char* why) {
        if(!ok) throw std::runtime_error(why);
    };
    require(ModManager::instance().setActiveMod(mod), "Missing speed fixture mod");
    settings.general.playerName = "Speed tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("speed.ini", mapData, "Unit speeds", true, settings.gameOptions);
    init.randomSeed = 67;
    require(init.getModName() == mod, "Speed fixture selected the wrong mod");
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Speed tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && owner->isAlive(), "Speed fixture house missing");
    game->initializeGameLoop();
    const Coord site(25,15);
    std::ofstream csv(std::string(std::getenv("UNIT_SPEED_PROBE_OUT"))+"/"+mod+".csv");
    csv << "type,air,range,heading,frame,x,y,orientation,fuse\n";
    const int types[]={Bullet_Rocket,Bullet_DRocket,Bullet_TurretRocket,Bullet_SmallRocket,Bullet_LargeRocket};
    for(int k=0;k<5;++k)for(int air=0;air<2;++air)for(int range : {1,3,8,12,20})for(int heading : {0,64,128,192}) {
        auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);
        auto* target=game->getHouse(HOUSE_HARKONNEN)->createUnit(air?Unit_Ornithopter:Unit_Tank,true);target->deploy(site+Coord(range,0));
        Coord start=shooter->getCenterPoint(),end=target->getCenterPoint();
        auto* b=new Bullet(shooter->getObjectID(),&start,&end,types[k],0,air,target);bulletList.push_back(b);
        b->destination=end;b->projectileHeading=heading;b->projectileAim=heading;b->projectileClock=0;
        for(int frame=0;frame<300;++frame) {
            b->update();
            csv<<k<<','<<air<<','<<range<<','<<heading<<','<<frame<<',';
            if(std::find(bulletList.begin(),bulletList.end(),b)==bulletList.end()) {csv<<"-1,-1,-1,-1\n";break;}
            csv<<lround(b->realX*4)<<','<<lround(b->realY*4)<<','<<int(b->projectileHeading)<<','<<b->detonationTimer<<'\n';
        }
        if(std::find(bulletList.begin(),bulletList.end(),b)!=bulletList.end()) {bulletList.remove(b);delete b;}
        target->cancelDeployment();shooter->cancelDeployment();
    }
    csv.close();SDL_Log("UNIT_SPEED_PROBE_PASS: %s projectile trace",mod.c_str());
    game.reset();currentGame=nullptr;return -1;
}
