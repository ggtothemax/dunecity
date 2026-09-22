// Headless integration fixture; linked with the production game objects.
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
    const Coord site(25,15);
    std::ofstream csv(std::string(std::getenv("UNIT_SPEED_PROBE_OUT"))+"/"+mod+".csv");
    csv << std::setprecision(9) << "unit,terrain,damaged,loaded,diagonal,offset,tiles_per_second,turn_degrees_per_second\n";
    const int units[] = {Unit_Ornithopter,Unit_Frigate,Unit_Devastator,Unit_Deviator,Unit_Harvester,
        Unit_Launcher,Unit_MCV,Unit_Quad,Unit_RaiderTrike,Unit_Saboteur,Unit_Sandworm,
        Unit_SiegeTank,Unit_Soldier,Unit_SonicTank,Unit_Tank,Unit_Trike,Unit_Trooper};
    for(int id : units) for(int terrain : {Terrain_Slab,Terrain_Sand,Terrain_Rock,Terrain_Dunes,Terrain_Spice,Terrain_ThickSpice,Terrain_Mountain})
    for(int damaged : {0,1,2}) for(bool loaded : {false,true}) for(bool diagonal : {false,true})
    for(bool offset : {false,true}) {
        if(loaded && id!=Unit_Harvester) continue;
        if(id==Unit_Sandworm && (terrain==Terrain_Slab || terrain==Terrain_Rock)) continue;
        if(terrain==Terrain_Mountain && id!=Unit_Soldier && id!=Unit_Trooper && id!=Unit_Saboteur
            && id!=Unit_Ornithopter && id!=Unit_Frigate) continue;
        if(offset && id!=Unit_Soldier && id!=Unit_Trooper && id!=Unit_Saboteur) continue;
        auto* unit=owner->createUnit(id,true);
        require(unit!=nullptr,"Could not create speed fixture unit");
        for(int y=site.y-2;y<=site.y+2;++y) for(int x=site.x-2;x<=site.x+2;++x) {
            auto* tile=currentGameMap->getTile(x,y); tile->setType(terrain); tile->setRoad(false);
        }
        unit->deploy(site);
        if(damaged) unit->setHealth(damaged==1 ? unit->getMaxHealth()*3/8 : unit->getMaxHealth()/8);
        if(loaded) static_cast<Harvester*>(unit)->spice=HARVESTERMAXSPICE;
        unit->angle=diagonal ? RIGHTDOWN : RIGHT; unit->drawnAngle=diagonal ? RIGHTDOWN : RIGHT;
        unit->nextSpot=site+Coord(1,diagonal ? 1:0); unit->destination=unit->nextSpot;
        unit->moving=true; unit->justStoppedMoving=false;
        if(auto* infantry=dynamic_cast<InfantryBase*>(unit)) {
            infantry->oldTilePosition=infantry->tilePosition;
            if(offset) infantry->tilePosition=(infantry->tilePosition+1)%5;
        }
        if(auto* air=dynamic_cast<AirUnit*>(unit)) air->currentMaxSpeed=game->objectData.data[id][HOUSE_ATREIDES].maxspeed;
        unit->setSpeeds();
        const double x=(unit->realX-unit->bumpyOffsetX).toDouble(), y=(unit->realY-unit->bumpyOffsetY).toDouble();
        unit->move();
        const double speed=std::hypot((unit->realX-unit->bumpyOffsetX).toDouble()-x,(unit->realY-unit->bumpyOffsetY).toDouble()-y)/TILESIZE/0.016;
        unit->angle=UP; unit->drawnAngle=UP; unit->nextSpotAngle=RIGHT;
        unit->moving=false; unit->justStoppedMoving=false; unit->setDestination(site+Coord(12,0));
        unit->nextSpotAngle=RIGHT;
        if(auto* tank=dynamic_cast<TankBase*>(unit)) { tank->turretAngle=UP; tank->targetAngle=RIGHT; }
        unit->turn();
        const double turnSpeed=std::abs(2-unit->angle.toDouble())*45/0.016;
        if(auto* tank=dynamic_cast<TankBase*>(unit))
            if(!baseline) require(std::abs(std::abs(2-tank->turretAngle.toDouble())*45/0.016-84.375)<0.0001,
                                  "Tank turret does not match Dynasty turn rate");
        for(int house=0;house<NUM_HOUSES;++house) {
            require(game->objectData.data[id][house].maxspeed==game->objectData.data[id][HOUSE_ATREIDES].maxspeed,
                    "Unit speed differs by house");
            require(game->objectData.data[id][house].turnspeed==game->objectData.data[id][HOUSE_ATREIDES].turnspeed,
                    "Unit turn speed differs by house");
        }
        csv << id << ',' << terrain << ',' << damaged << ',' << loaded << ',' << diagonal << ',' << offset << ','
            << speed << ',' << turnSpeed << '\n';
        require(std::isfinite(speed) && speed>0,"Unit did not move");
        unit->cancelDeployment();
    }
    // A terrain transition uses the entered tile; custom caps and roads scale it.
    for(int id : {Unit_Tank,Unit_Harvester,Unit_Soldier}) {
        auto* unit=owner->createUnit(id,true); unit->deploy(site);
        unit->nextSpot=site+Coord(1,0);
        currentGameMap->getTile(site)->setType(Terrain_Sand);
        auto* entered=currentGameMap->getTile(unit->nextSpot);
        entered->setType(Terrain_Rock); entered->setRoad(false);
        const FixPoint plain=unit->getTerrainAdjustedSpeed();
        const double expected=id==Unit_Tank ? 1.171875 : id==Unit_Harvester ? 0.9375 : 0.234375;
        if(!baseline) require(std::abs(plain.toDouble()/TILESIZE/0.016-expected)<0.0001,
                              "Movement did not use destination terrain");
        entered->setRoad(true);
        require(FixPoint::abs(unit->getTerrainAdjustedSpeed()-plain*4)<0.00001_fix,"Road multiplier changed");
        entered->setRoad(false);
        auto& cap=game->objectData.data[id][HOUSE_ATREIDES].maxspeed;
        const FixPoint saved=cap; cap*=2;
        require(FixPoint::abs(unit->getTerrainAdjustedSpeed()-plain*2)<0.00001_fix,"Custom speed cap ignored");
        cap=saved; unit->cancelDeployment();
    }
    game->initializeGameLoop();
    for(int id : units) {
        if(id==Unit_Ornithopter || id==Unit_Frigate || id==Unit_Sandworm) continue;
        for(int y=site.y-2;y<=site.y+2;++y) for(int x=site.x-2;x<=site.x+9;++x)
            currentGameMap->getTile(x,y)->setType(Terrain_Sand);
        auto* unit=owner->createUnit(id,true);
        unit->deploy(site); unit->angle=RIGHT; unit->drawnAngle=RIGHT;
        const Coord end=site+Coord(8,0);
        unit->doMove2Pos(end.x,end.y,true);
        int ticks=0;
        while((unit->location!=end || unit->moving) && ticks<10000) {
            game->updateGameState();
            ++ticks;
        }
        if(ticks>=10000) throw std::runtime_error("Route did not complete for unit "+std::to_string(id));
        csv << "route_" << id << ",1,0,0,0,0," << 8/(ticks*0.016) << ",0\n";
        unit->cancelDeployment();
    }
    // Real game loop: the aircraft must circle back and fire again at its target.
    auto* enemy=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Devastator,true);
    enemy->deploy(site+Coord(8,0)); enemy->setVisible(VIS_ALL,true);
    auto* orni=static_cast<Ornithopter*>(owner->createUnit(Unit_Ornithopter,true));
    orni->deploy(site); orni->angle=RIGHT; orni->drawnAngle=RIGHT;
    orni->doAttackObject(enemy,true);
    unsigned firstShot=0; int ticks=0;
    for(;ticks<3000;++ticks) {
        game->updateGameState();
        if(orni->timeLastShot && !firstShot) firstShot=orni->timeLastShot;
        else if(firstShot && orni->timeLastShot!=firstShot) break;
    }
    require(ticks<3000,"Ornithopter failed to make a second attack pass");
    require(enemy->getHealth()<enemy->getMaxHealth(),"Ornithopter attack did not damage its target");
    csv << "route_ornithopter_attack,0,0,0,0,0," << ticks*0.016 << ",0\n";
    orni->cancelDeployment(); enemy->cancelDeployment();
    csv.close();
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s (%s)",mod.c_str(),baseline ? "baseline" : "regression");
    game.reset(); currentGame=nullptr;
    return -1;
}
