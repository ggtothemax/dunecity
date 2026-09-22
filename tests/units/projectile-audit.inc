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
    csv << "type,target,case,speed_tiles_s,initial_fuse,turn_deg_s,lifetime_s,target_damage,end_distance_tiles\n";
    for(int type : {Bullet_Rocket,Bullet_DRocket,Bullet_TurretRocket,Bullet_SmallRocket,Bullet_LargeRocket,Bullet_Flame})
    for(int targetType : {Unit_Tank,Unit_Ornithopter,Unit_Carryall})
    for(int scenario=0;scenario<3;++scenario) {
        auto* shooter=owner->createUnit(type==Bullet_Flame?Unit_Trooper:Unit_Launcher,true);shooter->deploy(site);
        auto* target=game->getHouse(HOUSE_HARKONNEN)->createUnit(targetType,true);target->deploy(site+Coord(8,0));
        target->setVisible(VIS_ALL,true);
        Coord start=shooter->getCenterPoint(),end=target->getCenterPoint();
        auto* b=new Bullet(shooter->getObjectID(),&start,&end,type,40,targetType!=Unit_Tank,target);
        bulletList.push_back(b);
        b->destination=end; // Isolate steering/fuses from random scatter.
        const auto hp=target->getHealth();
        const int fuse=b->detonationTimer;const double speed=b->speed.toDouble()/64/0.016;
        if(scenario==1) {b->angle+=128;if(b->angle>=256)b->angle-=256;b->xSpeed= -b->xSpeed;b->ySpeed= -b->ySpeed;}
        const auto firstAngle=b->angle;double turn=0,distance=8;
        int ticks=0;
        for(;ticks<1000;++ticks) {
            if(scenario==2 && targetType!=Unit_Tank) {
                // Controlled crossing target at the corrected ornithopter cruise speed.
                auto y=target->realY+11.52_fix;
                if(y>(site.y+6)*64) y=site.y*64;
                target->unassignFromMap(target->location);
                target->setLocation(site.x+8,(y/64).floor());
                target->realY=y;
            }
            distance=distanceFrom(b->realX,b->realY,target->getCenterPoint().x,target->getCenterPoint().y).toDouble()/64;
            b->update();
            const bool alive=std::find(bulletList.begin(),bulletList.end(),b)!=bulletList.end();
            if(!alive) {++ticks;break;}
            if(ticks==0) {
                double delta=std::abs((b->angle-firstAngle).toDouble());if(delta>128)delta=256-delta;
                turn=delta*360/256/0.016;
            }
        }
        csv<<type<<','<<targetType<<','<<scenario<<','<<speed<<','<<fuse<<','<<turn<<','<<ticks*0.016<<','<<(hp-target->getHealth()).toDouble()<<','<<distance<<'\n';
        if(std::find(bulletList.begin(),bulletList.end(),b)!=bulletList.end()) {bulletList.remove(b);delete b;}
        target->cancelDeployment();shooter->cancelDeployment();
    }
    for(int offset : {0,24,40}) {
        auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);
        auto* target=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true);target->deploy(site+Coord(8,0));
        const auto hp=target->getHealth();const Coord point=target->getCenterPoint()+Coord(offset,0);
        currentGameMap->damage(shooter->getObjectID(),owner,point,Bullet_Rocket,8,TILESIZE/2,true);
        csv<<"blast,35,"<<offset<<",0,0,0,0,"<<(hp-target->getHealth()).toDouble()<<','<<offset/64.0<<'\n';
        target->cancelDeployment();shooter->cancelDeployment();
    }
    csv.close();SDL_Log("UNIT_SPEED_PROBE_PASS: %s projectile audit",mod.c_str());
    game.reset();currentGame=nullptr;return -1;
}
