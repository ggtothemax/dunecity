// Real-engine MCV deployment fixture. Every decision here comes from
// QuantBot::manageMcv and every yard from the production MCV::doDeploy path;
// nothing reimplements the rules. The fixture isolates its own region: the
// global unit/structure lists are replaced by the fixture objects and the
// bot's tactical data is recomputed from them, so no other house interferes.
//
// It pins the reported failure: a mature city base whose advanced core is not
// finished (expansionDefenceReady() == false) left several MCVs driving around
// the base instead of deploying on the rock the base already stands on.
#include <dunecity/CitySimulation.h>
#include <units/MCV.h>
#include <structures/ConstructionYard.h>

void runMcvDeploymentProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("MCV_DEPLOYMENT_FAIL: %s",why);throw std::runtime_error(why);}};
    auto* house=pLocalHouse;
    require(house!=nullptr,"MCV fixture needs a local house");
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get())) bot=candidate;
    require(bot!=nullptr,"MCV fixture needs a QuantBot on the local house");

    // City growth rules, in a custom game, exactly as the reported match.
    game.citySimEnabled_=true;
    if(!game.citySimulation_) {
        game.citySimulation_=std::make_unique<DuneCity::CitySimulation>();
        game.citySimulation_->init(currentGameMap->getSizeX(),currentGameMap->getSizeY());
    }
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;
    require(game.isCitySimEnabled(),"MCV fixture needs the city simulation");

    // One clear rectangle, far from everything else on the map.
    constexpr int spanX=40,spanY=30;
    Coord origin=Coord::Invalid();
    for(int y=1;y+spanY+1<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=1;x+spanX+1<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=-1;dy<=spanY && clear;++dy)
                for(int dx=-1;dx<=spanX && clear;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
            if(clear) origin=Coord(x,y);
        }
    require(origin.isValid(),"MCV fixture needs a clear region");
    auto site=[&](int x,int y){ return Coord(origin.x+x,origin.y+y); };
    auto tileAt=[&](int x,int y){ return currentGameMap->getTile(origin.x+x,origin.y+y); };
    for(int y=0;y<spanY;++y) for(int x=0;x<spanX;++x) {
        auto* tile=tileAt(x,y);
        tile->setType(Terrain_Sand);tile->setRoad(false);tile->setOwner(INVALID);
    }
    // The formation the base stands on, a one tile sand gap, and a separate
    // patch that still falls inside the base's build range.
    auto rock=[&](int x0,int y0,int x1,int y1) {
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) tileAt(x,y)->setType(Terrain_Rock);
    };
    rock(2,2,26,22);
    rock(27,4,33,10);

    // Isolation: only fixture objects exist for the AI and the game loop.
    auto savedUnits=unitList;auto savedStructures=structureList;
    const int savedHeavy=house->numItem[Structure_HeavyFactory];
    const int savedHighTech=house->numItem[Structure_HighTechFactory];
    const int savedRepair=house->numItem[Structure_RepairYard];
    struct Restore {
        RobustList<UnitBase*> savedUnits; RobustList<StructureBase*> savedStructures;
        House* house; int heavy,highTech,repair;
        ~Restore() {
            unitList=savedUnits;structureList=savedStructures;
            house->numItem[Structure_HeavyFactory]=heavy;
            house->numItem[Structure_HighTechFactory]=highTech;
            house->numItem[Structure_RepairYard]=repair;
        }
    } restore{savedUnits,savedStructures,house,savedHeavy,savedHighTech,savedRepair};
    unitList.clear();structureList.clear();
    // The base has no advanced core, which is the reported condition.
    house->numItem[Structure_HeavyFactory]=0;
    house->numItem[Structure_HighTechFactory]=0;
    house->numItem[Structure_RepairYard]=0;
    bot->reservedStructures.clear();
    bot->recentStructureLosses.clear();
    bot->mcvExpansionSites.clear();
    bot->mcvSurveyCycles.clear();
    bot->planningBuilder=NONE_ID;
    bot->availableBaseRock=0;              // Never surveyed: the reported state.
    bot->rockSurveyCycle=std::numeric_limits<Uint32>::max();

    auto place=[&](Uint32 item,int x,int y) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+x,origin.y+y,true,true);
        require(structure!=nullptr,"MCV fixture structure placement failed");
        return structure;
    };
    place(Structure_ConstructionYard,4,4);
    // Owned tiles beside the sand gap, so the separate patch is inside the
    // base's build range while remaining a different rock formation.
    place(Structure_WindTrap,24,4);
    house->producedPower=10000;

    // Enemies are gone from the lists, so the danger fields recompute to zero.
    bot->dangerUpdated=std::numeric_limits<Uint32>::max();
    bot->refreshTacticalDanger();

    auto alive=[&](Uint32 id){ return game.getObjectManager().getObject(id)!=nullptr; };
    auto spawn=[&](int x,int y) {
        auto* unit=house->createUnit(Unit_MCV,true);
        require(unit!=nullptr,"MCV fixture could not create an MCV");
        unit->deploy(site(x,y));
        require(unit->getLocation()==site(x,y),"MCV fixture could not place an MCV");
        return static_cast<MCV*>(unit);
    };
    // The object update phase of the real game loop. Houses and AI players are
    // driven separately below, so no other controller touches the fixture.
    auto tick=[&](int cycles){ for(int i=0;i<cycles;++i){game.processObjects();++game.gameCycleCount;} };
    auto overlaps=[](Coord a,Coord b) {
        return a.x<b.x+2 && b.x<a.x+2 && a.y<b.y+2 && b.y<a.y+2;
    };
    // The fixtures below judge decisions without running the route, so the
    // MCV is released from its last forced order first: an MCV that is still
    // executing one is deliberately left alone by the AI.
    auto replan=[&](MCV* unit){ unit->setForced(false);bot->manageMcv(unit); };
    auto yardNear=[&](Coord at,int radius) {
        for(const auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard
                && std::abs(structure->getLocation().x-at.x)<=radius
                && std::abs(structure->getLocation().y-at.y)<=radius) return true;
        return false;
    };
    auto onHomeRock=[&](Coord at) {
        return at.isValid() && at.x>=origin.x+2 && at.x+1<origin.x+26
            && at.y>=origin.y+2 && at.y+1<origin.y+22;
    };

    require(house->getNumItems(Structure_ConstructionYard)>0,"MCV fixture needs an existing yard");
    require(!bot->expansionDefenceReady(),
        "Fixture must reproduce the unfinished advanced core: run it where a heavy factory,"
        " high tech factory or repair yard is buildable at this tech level");

    // 1. Several MCVs standing on the base's own rock deploy promptly, at
    //    every difficulty, although the colonisation gate is shut and the rock
    //    survey has never run.
    const QuantBot::Difficulty difficulties[]={QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
        QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal};
    int row=0;
    for(auto level:difficulties) {
        bot->difficulty=level;
        const int before=house->getNumItems(Structure_ConstructionYard);
        const Coord spots[3]={Coord(9,9+row),Coord(15,9+row),Coord(21,9+row)};
        MCV* crew[3]={spawn(spots[0].x,spots[0].y),spawn(spots[1].x,spots[1].y),spawn(spots[2].x,spots[2].y)};
        Uint32 ids[3]={crew[0]->getObjectID(),crew[1]->getObjectID(),crew[2]->getObjectID()};
        Coord started[3]={crew[0]->getLocation(),crew[1]->getLocation(),crew[2]->getLocation()};
        int deployed=0;
        // Four AI passes at the real cadence: prompt means a handful of passes,
        // not the minutes of driving around the base that were reported.
        for(int pass=0;pass<4 && deployed<3;++pass) {
            deployed=0;
            for(int i=0;i<3;++i) {
                if(!alive(ids[i])) {++deployed;continue;}
                bot->manageMcv(crew[i]);
                if(!alive(ids[i])) ++deployed;   // Deploying destroys the MCV.
            }
            tick(50);
        }
        require(deployed==3,"MCVs on the base's own rock did not deploy promptly");
        require(house->getNumItems(Structure_ConstructionYard)==before+3,
            "Deploying MCVs did not produce construction yards");
        for(int i=0;i<3;++i) {
            require(!alive(ids[i]),"A deployed MCV still exists");
            // Prompt also means here: the yard belongs to the rock the MCV was
            // already standing on, not to a trip across the base.
            require(yardNear(started[i],3),"A deployed MCV left no yard where it stood");
        }
        row+=3;
    }
    bot->difficulty=QuantBot::Difficulty::Hard;

    // 2. An MCV that has to travel reaches its site and deploys there, driven
    //    by real engine frames, within half a minute of game time.
    {
        const int before=house->getNumItems(Structure_ConstructionYard);
        // Three tiles of open sand below the base's rock: a short route that
        // never reaches the carryall lift distance, so the trip is driven.
        auto* traveller=spawn(12,24);
        const Uint32 id=traveller->getObjectID();
        const Coord start=traveller->getLocation();
        require(!traveller->canDeploy(),"Sand must not be a legal yard footprint");
        bot->manageMcv(traveller);
        require(house->getNumItems(Structure_ConstructionYard)==before,
            "An MCV deployed on ground the engine forbids");
        require(alive(id),"The travelling MCV was consumed on sand");
        for(int pass=0;pass<3 && !onHomeRock(traveller->getDestination());++pass) {
            game.gameCycleCount+=MILLI2CYCLES(6000);
            replan(traveller);
        }
        const Coord destination=traveller->getDestination();
        require(onHomeRock(destination),"A travelling MCV was not sent to the base's own rock");
        const Uint32 deadline=game.getGameCycleCount()+MILLI2CYCLES(30000);
        bool moved=false;
        Coord lastSeen=start;
        while(alive(id) && game.getGameCycleCount()<deadline) {
            tick(10);
            if(!alive(id)) break;
            lastSeen=traveller->getLocation();
            if(lastSeen!=start) moved=true;
            if(game.getGameCycleCount()%50<10) bot->manageMcv(traveller);
        }
        require(!alive(id),"A travelling MCV never arrived and deployed within 30 seconds");
        require(moved,"The MCV deployed without ever moving: the route was not exercised");
        require(house->getNumItems(Structure_ConstructionYard)==before+1,
            "Arriving at the chosen site produced no construction yard");
        require(onHomeRock(lastSeen) && yardNear(lastSeen,2),
            "The travelling MCV did not build on the base's own rock where it arrived");
        require(yardNear(destination,2),"The yard is nowhere near the site the MCV was sent to");
    }

    // 3. Two MCVs that both have to travel never reserve the same footprint.
    Coord firstSite=Coord::Invalid();
    MCV* first=nullptr;Uint32 firstID=NONE_ID;
    {
        // Neighbours on sand: without a reservation both would score the same
        // nearest footprint, so the two sites must be separated deliberately.
        auto* left=spawn(5,26);
        auto* right=spawn(6,26);
        const Uint32 leftID=left->getObjectID(),rightID=right->getObjectID();
        for(int pass=0;pass<3;++pass) {
            if(alive(leftID)) replan(left);
            if(alive(rightID)) replan(right);
            game.gameCycleCount+=MILLI2CYCLES(6000);
        }
        require(alive(leftID) && alive(rightID),"Fixture MCVs deployed before reaching their sites");
        const Coord a=left->getDestination(),b=right->getDestination();
        require(onHomeRock(a) && onHomeRock(b),"A reserved site left the base's own rock");
        require(!overlaps(a,b),"Two MCVs reserved overlapping yard footprints");
        first=left;firstID=leftID;firstSite=a;
        // 4. A site is kept while traffic briefly stands on it, and replaced
        //    only once something permanent occupies it.
        auto* blocker=house->createUnit(Unit_Quad,true);
        require(blocker!=nullptr,"Fixture could not create a blocking unit");
        blocker->deploy(firstSite);
        bot->mcvSurveyCycles[firstID]=game.getGameCycleCount();
        game.gameCycleCount+=MILLI2CYCLES(1000);
        replan(first);
        require(alive(firstID),"The blocked MCV disappeared");
        require(first->getDestination()==firstSite,
            "A destination a unit was briefly standing on was rerolled");
        game.gameCycleCount+=MILLI2CYCLES(6000);
        replan(first);
        require(first->getDestination()!=firstSite,
            "An MCV stalled by parked traffic never tried a clear site");
        firstSite=first->getDestination();
        blocker->setVisible(VIS_ALL,false);
        blocker->destroy();
        auto* permanent=house->placeStructure(NONE_ID,Structure_WindTrap,firstSite.x,firstSite.y,true,true);
        require(permanent!=nullptr,"Fixture could not build over the reserved site");
        game.gameCycleCount+=MILLI2CYCLES(1000);
        replan(first);
        require(alive(firstID),"The replanning MCV disappeared");
        const Coord replanned=first->getDestination();
        require(replanned!=firstSite && onHomeRock(replanned),
            "A permanently occupied site was not replaced by another site on the same rock");
        require(!overlaps(replanned,firstSite),"The replanned site still overlaps the occupied one");
        require(!overlaps(replanned,right->getDestination()),
            "Replanning took the site the other MCV is driving at");
    }

    // 5. A separate rock patch is a colony, not base growth, even where it
    //    falls inside the base's own build range.
    {
        const Coord patch=site(27,4);
        require(currentGameMap->isWithinBuildRange(patch.x,patch.y,house),
            "Fixture patch must sit inside the base build range");
        require(!bot->onOwnRockFormation(patch),
            "A rock patch the base does not stand on was treated as its own formation");
        auto* colonist=spawn(27,4);
        const Uint32 id=colonist->getObjectID();
        const int before=house->getNumItems(Structure_ConstructionYard);
        require(colonist->canDeploy(),"Fixture patch must be a legal footprint for the engine");
        require(!bot->mcvMayDeployHere(colonist,true),
            "A colony site bypassed the expansion defence gate");
        require(!bot->findLocalDeploySite(colonist).isValid()
            || onHomeRock(bot->findLocalDeploySite(colonist)),
            "Local deployment offered a site off the base's own formation");
        for(int pass=0;pass<3;++pass) {
            replan(colonist);
            game.gameCycleCount+=MILLI2CYCLES(6000);
        }
        require(alive(id),"The colonist MCV was consumed");
        require(house->getNumItems(Structure_ConstructionYard)==before,
            "An MCV colonised a separate formation while the defence gate was shut");
        require(!currentGameMap->getTile(patch)->hasAStructure(),
            "A yard appeared on the separate formation");

        // 6. Without a yard there is nothing to expand from: the opening MCV
        //    still deploys wherever the engine allows it.
        const int savedYards=house->numItem[Structure_ConstructionYard];
        house->numItem[Structure_ConstructionYard]=0;
        bot->mcvExpansionSites.clear();bot->mcvSurveyCycles.clear();
        replan(colonist);
        require(!alive(id),"An opening MCV with no yard refused a legal footprint");
        require(currentGameMap->getTile(patch)->hasAStructure(),"The opening yard was not built");
        house->numItem[Structure_ConstructionYard]=savedYards+1;
    }

    SDL_Log("MCV_DEPLOYMENT_PASS: prompt same-rock deployment at every difficulty, one driven arrival,"
        " distinct reservations, blocked-site handling and the colony gate");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=mcv_deployment outcome=passed");
}
