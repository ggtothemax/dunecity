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
    game->initializeGameLoop();
    const Coord site(25,15);
    std::ofstream csv(std::string(std::getenv("UNIT_SPEED_PROBE_OUT"))+"/"+mod+".csv");
    csv << "unit,tiles,start_heading,phase,seconds,ticks,moving_ticks,stationary_ticks\n";
    const std::pair<int,const char*> units[]={{Unit_Tank,"Tank"},{Unit_Trike,"Trike"},{Unit_RaiderTrike,"Raider Trike"},
        {Unit_Quad,"Quad"},{Unit_Harvester,"Harvester"},{Unit_Soldier,"Soldier"},{Unit_Trooper,"Trooper"},
        {Unit_Devastator,"Devastator"},{Unit_Launcher,"Launcher"},{Unit_SiegeTank,"Siege Tank"},
        {Unit_MCV,"MCV"},{Unit_Deviator,"Deviator"},{Unit_SonicTank,"Sonic Tank"},{Unit_Saboteur,"Saboteur"}};
    for(const auto& entry : units) for(int tiles : {8,16}) for(int heading : {RIGHT,UP}) for(int phase=0;phase<5;++phase) {
        for(int y=site.y-2;y<=site.y+2;++y) for(int x=site.x-2;x<=site.x+17;++x) {
            auto* tile=currentGameMap->getTile(x,y); tile->setType(Terrain_Sand); tile->setRoad(false);
            require(!tile->hasAGroundObject(),"Route corridor is occupied");
        }
        while(game->getGameCycleCount()%5 != static_cast<unsigned>(phase)) game->updateGameState();
        auto* unit=owner->createUnit(entry.first,true);
        unit->deploy(site); unit->angle=heading; unit->drawnAngle=heading;
        const Coord end=site+Coord(tiles,0);
        unit->doMove2Pos(end.x,end.y,true);
        int ticks=0,movingTicks=0;
        while((unit->location!=end || unit->moving) && ticks<20000) {
            const auto x=unit->realX-unit->bumpyOffsetX, y=unit->realY-unit->bumpyOffsetY;
            game->updateGameState();
            if(x!=unit->realX-unit->bumpyOffsetX || y!=unit->realY-unit->bumpyOffsetY) ++movingTicks;
            ++ticks;
        }
        require(ticks<20000,"Route did not complete");
        csv << entry.second << ',' << tiles << ',' << (heading==RIGHT?0:90) << ',' << phase << ','
            << ticks*0.016 << ',' << ticks << ',' << movingTicks << ',' << ticks-movingTicks << '\n';
        unit->cancelDeployment();
    }
    csv.close();
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s routes",mod.c_str());
    game.reset(); currentGame=nullptr;
    return -1;
}
