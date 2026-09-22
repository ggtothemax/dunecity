// Headless integration fixture; linked with the production game objects.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <structures/RepairYard.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cmath>

int runCarryallFlightProbe() {
    const std::string mod = std::getenv("CARRYALL_PROBE_MOD");
    const bool baseline = std::string(std::getenv("CARRYALL_PROBE_BASELINE")) == "1";
    auto require = [](bool ok, const char* why) {
        if(!ok) throw std::runtime_error(why);
    };
    require(ModManager::instance().setActiveMod(mod), "Missing flight fixture mod");
    settings.general.playerName = "Flight tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("flight.ini", mapData, "Carryall flight", true, settings.gameOptions);
    init.randomSeed = 67;
    require(init.getModName() == mod, "Flight fixture selected the wrong mod");
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Flight tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && owner->isAlive(), "Flight fixture house missing");
    Coord site = Coord::Invalid();
    for(int y=5; y<currentGameMap->getSizeY()-7 && site.isInvalid(); ++y)
        for(int x=22; x<currentGameMap->getSizeX()-7; ++x)
            if(!currentGameMap->getTile(x,y)->hasAGroundObject()
               && !currentGameMap->getTile(x,y+4)->hasAGroundObject()) { site = Coord(x,y); break; }
    require(site.isValid(), "No flight fixture space");
    std::ofstream csv(std::string(std::getenv("CARRYALL_PROBE_OUT"))+"/"+mod+".csv");
    csv << std::setprecision(9) << "kind,distance_tiles,heading_error_degrees,seconds,step_tiles,heading_degrees\n";
    auto aircraft = [&] {
        auto* carry = static_cast<Carryall*>(owner->createUnit(Unit_Carryall, true));
        require(carry != nullptr, "Cannot create carryall");
        carry->deploy(site - Coord(2,0));
        carry->angle = 0; carry->drawnAngle = 0;
        return carry;
    };
    // Calling the real turn method in place isolates angular rate from translation.
    for(int start : {2,4}) {
        auto* carry = aircraft();
        carry->setDestination(site); carry->angle = start; carry->drawnAngle = start;
        int ticks=0;
        while(FixPoint::abs(carry->angle - carry->getDestinationAngle()) > 0.0001_fix
              && ticks<1000) { carry->turn(); ++ticks; }
        require(ticks<1000, "Carryall failed to turn toward target");
        if(!baseline) require(ticks == (start == 2 ? 23 : 45),
                              "Carryall turn rate is not Dynasty's 253.125 degrees/s");
        csv << "rotation,0," << start*45 << ',' << ticks*0.016 << ",0,0\n";
        carry->cancelDeployment();
    }
    for(double distance : {20.0,10.0,8.0,4.0,2.0,1.5,1.0,0.5,0.25,0.125}) {
        for(int heading : {0,1,2,4}) {
            auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank, true));
            require(tank != nullptr, "Cannot create pickup unit");
            tank->deploy(site); tank->setDestination(site+Coord(0,4));
            auto* carry = aircraft();
            carry->unassignFromMap(carry->location);
            const Coord old = carry->location;
            carry->realX = tank->getRealX() - FixPoint(static_cast<int>(distance*TILESIZE));
            carry->realY = tank->getRealY();
            carry->location = Coord(carry->realX.lround()/TILESIZE, carry->realY.lround()/TILESIZE);
            carry->assignToMap(carry->location);
            if(auto* grid = game->getSpatialGrid()) grid->move(*carry,carry->getGridHandle(),old,carry->location);
            carry->angle = heading; carry->drawnAngle = heading;
            carry->setDestination(site); carry->setTarget(tank);
            tank->bookCarrier(carry);
            const double x=carry->getRealX().toDouble(), y=carry->getRealY().toDouble();
            require(carry->update(), "Carryall vanished during approach");
            const double step=std::hypot(carry->getRealX().toDouble()-x,carry->getRealY().toDouble()-y)/TILESIZE;
            if(!baseline) {
                require(step <= 0.24001, "Pickup movement exceeded the cruise cap");
                if(distance == 20.0) {
                    const double expected = heading == 0 ? 0.24 : heading == 1 ? 0.20 : heading == 2 ? 0.18 : 0.12;
                    require(std::abs(step-expected) < 0.0001, "Missing Dynasty heading-dependent throttle");
                }
                if(distance == 1.5 && heading == 0)
                    require(std::abs(step-0.04) < 0.0001, "Pickup approach still jumps at short range");
                if(distance == 0.25)
                    require(std::abs(step-0.004) < 0.0001, "Final creep adds forward flight or ignores script pacing");
            }
            csv << "step," << distance << ',' << heading*45 << ",0.016," << step << ',' << carry->angle.toDouble()*45 << '\n';
            int ticks=1;
            while(!carry->hasCargo() && ticks<2500) { require(carry->update(), "Carryall vanished before pickup"); ++ticks; }
            require(carry->hasCargo(), "Carryall circled or stalled without picking up");
            if(!baseline && distance == 0.25)
                require(ticks*0.016 > 0.45 && ticks*0.016 < 0.8, "Final pickup rushes or stalls");
            csv << "pickup," << distance << ',' << heading*45 << ',' << ticks*0.016 << ",0,0\n";
            ticks=0;
            while(carry->hasCargo() && ticks<2500) { require(carry->update(), "Carryall vanished during delivery"); ++ticks; }
            require(!carry->hasCargo() && tank->isActive(), "Carryall failed to deliver cargo");
            csv << "delivery," << distance << ',' << heading*45 << ',' << ticks*0.016 << ",0,0\n";
            carry->cancelDeployment(); tank->cancelDeployment();
        }
    }
    // Follow an actual off-centre unit, then keep tracking it while it moves.
    {
        auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
        tank->deploy(site); tank->setDestination(site+Coord(0,4));
        auto* carry = aircraft();
        carry->setTarget(tank); tank->bookCarrier(carry);
        tank->realY += 12;
        int ticks=0;
        while(!carry->hasCargo() && ticks<2500) {
            if(ticks<100) tank->realY += 0.1_fix;
            require(carry->update(), "Moving-target carryall vanished"); ++ticks;
        }
        require(carry->hasCargo(), "Carryall could not pick up an off-centre moving unit");
        while(carry->hasCargo() && ticks<5000) { carry->update(); ++ticks; }
        require(!carry->hasCargo() && tank->isActive(), "Moving-target cargo not delivered");
        csv << "moving_target,2,0," << ticks*0.016 << ",0,0\n";
        carry->cancelDeployment(); tank->cancelDeployment();
    }
    for(bool disappeared : {false,true}) {
        auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
        tank->deploy(site); tank->setDestination(site+Coord(0,4));
        auto* carry = aircraft();
        carry->setTarget(tank); tank->bookCarrier(carry);
        if(disappeared) tank->cancelDeployment();
        else tank->setAwaitingPickup(false);
        require(carry->update() && !carry->hasCargo() && !carry->target,
                "Cancelled or disappeared pickup left a stale target");
        carry->cancelDeployment();
        if(!disappeared) tank->cancelDeployment();
    }
    // Exercise both directions of a real repair-yard transfer, not just drops
    // on empty sand. Its multi-tile closest-point destination can change side.
    {
        RepairYard* yard=nullptr;
        const Coord size=getStructureSize(Structure_RepairYard);
        for(int y=14;y+size.y<currentGameMap->getSizeY()-1 && !yard;++y)
            for(int x=32;x+size.x<currentGameMap->getSizeX()-1 && !yard;++x) {
                bool clear=true;
                for(int dy=0;dy<size.y;++dy) for(int dx=0;dx<size.x;++dx)
                    clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject();
                if(clear) yard=static_cast<RepairYard*>(owner->placeStructure(NONE_ID,Structure_RepairYard,x,y,true,true));
            }
        require(yard != nullptr, "Cannot place repair-yard flight fixture");
        auto* tank=static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
        tank->deploy(site); tank->setHealth(tank->getMaxHealth()/3);
        auto* carry=aircraft();
        tank->doRepair();
        require(tank->getTarget()==yard, "Damaged tank did not request repair");
        carry->setTarget(tank); tank->bookCarrier(carry);
        int ticks=0;
        while(yard->getRepairUnit()!=tank && ticks<3000) { carry->update(); ++ticks; }
        require(yard->getRepairUnit()==tank && !carry->hasCargo(), "Carryall failed to dock at repair yard");
        csv << "repair_delivery,0,0," << ticks*0.016 << ",0,0\n";
        tank->setHealth(tank->getMaxHealth());
        carry->setTarget(yard);
        ticks=0;
        while(!carry->hasCargo() && ticks<1000) { carry->update(); ++ticks; }
        require(carry->hasCargo() && yard->isFree(), "Repaired cargo not collected from yard");
        while(carry->hasCargo() && ticks<4000) { carry->update(); ++ticks; }
        require(!carry->hasCargo() && tank->isActive(), "Repaired cargo not returned to ground");
        csv << "repair_return,0,0," << ticks*0.016 << ",0,0\n";
        carry->cancelDeployment(); tank->cancelDeployment();
    }
    csv.close();
    SDL_Log("CARRYALL_FLIGHT_PROBE_PASS: %s (%s)",mod.c_str(),baseline ? "baseline" : "regression");
    game.reset(); currentGame=nullptr;
    return -1;
}
