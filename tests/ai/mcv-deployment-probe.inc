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
#include <players/QuantBotColonisationPolicy.h>
#include <players/QuantBotBuildPolicy.h>
#include <units/MCV.h>
#include <structures/ConstructionYard.h>
#include <structures/HeavyFactory.h>

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

    // 7. Space-driven colonisation. A city whose production yards are all
    //    accounted for, whose own rock is built out, and which can reach safe
    //    free rock, must buy an MCV and settle it. This is a separate decision
    //    from the local growth above: none of the sections below may change
    //    how an MCV deploys while the base still has room.
    {
        // One settlement, of its own. The rock from the sections above is
        // retired to sand and its yards are taken out of the AI's view, so the
        // survey measures the cramped base built here and nothing else.
        for(int y=0;y<spanY;++y) for(int x=0;x<spanX;++x)
            if(!tileAt(x,y)->hasAStructure()) tileAt(x,y)->setType(Terrain_Sand);
        // Earlier blocked-site cases leave two live MCVs deliberately. Retire
        // them through the engine so the house's pending-MCV count is correct.
        std::vector<UnitBase*> retiredMcvs;
        for(auto* unit:unitList)
            if(unit->getOwner()==house && unit->getItemID()==Unit_MCV) retiredMcvs.push_back(unit);
        for(auto* unit:retiredMcvs) unit->destroy();
        game.processObjects();
        require(house->getNumItems(Unit_MCV)==0,"Prior fixture MCVs were not retired");
        unitList.clear();structureList.clear();
        // Build range uses tile ownership, not the AI object list. Remove old
        // anchors and free rock outside this isolated settlement as well.
        for(int y=0;y<currentGameMap->getSizeY();++y)
            for(int x=0;x<currentGameMap->getSizeX();++x) {
                auto* tile=currentGameMap->getTile(x,y);
                tile->setOwner(INVALID);
                if(!tile->hasAStructure()) {tile->setType(Terrain_Sand);tile->setRoad(false);}
            }

        constexpr int fieldX=52,fieldY=18;
        Coord ground=Coord::Invalid();
        for(int y=1;y+fieldY+1<currentGameMap->getSizeY() && ground.isInvalid();++y)
            for(int x=1;x+fieldX+1<currentGameMap->getSizeX() && ground.isInvalid();++x) {
                const bool overlapsFixture=x<origin.x+spanX+2 && origin.x<x+fieldX+2
                    && y<origin.y+spanY+2 && origin.y<y+fieldY+2;
                if(overlapsFixture) continue;
                bool clear=true;
                for(int dy=-1;dy<=fieldY && clear;++dy)
                    for(int dx=-1;dx<=fieldX && clear;++dx)
                        if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
                if(clear) ground=Coord(x,y);
            }
        require(ground.isValid(),"Colonisation fixture needs a second clear region");
        auto at=[&](int x,int y){ return Coord(ground.x+x,ground.y+y); };
        auto groundTile=[&](int x,int y){ return currentGameMap->getTile(ground.x+x,ground.y+y); };
        for(int y=0;y<fieldY;++y) for(int x=0;x<fieldX;++x) {
            auto* tile=groundTile(x,y);
            tile->setType(Terrain_Sand);tile->setRoad(false);tile->setOwner(INVALID);
        }
        auto terraform=[&](int x0,int y0,int x1,int y1,int terrain) {
            for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) groundTile(x,y)->setType(terrain);
        };
        // The base's own rock, and a far larger formation across open sand.
        // Thirty tiles of sand is well beyond the two-tile build range, so the
        // second formation is only ever reachable by driving an MCV there.
        terraform(1,4,11,12,Terrain_Rock);
        terraform(36,3,48,15,Terrain_Rock);

        bot->reservedStructures.clear();
        bot->recentStructureLosses.clear();
        bot->mcvExpansionSites.clear();
        bot->mcvSurveyCycles.clear();
        bot->lastMcvTrace.clear();
        bot->clearPlacementCache();
        bot->planningBuilder=NONE_ID;
        bot->availableBaseRock=0;
        bot->availableBaseFootprints=0;
        bot->baseProductionRoomBlocked=false;
        bot->rockSurveyCycle=std::numeric_limits<Uint32>::max();
        bot->lastCalculatedSpice=0;
        // This is an established city's expansion decision, after its opening
        // worker fleet. Real workers also keep that unrelated gate satisfied.
        while(house->getNumItems(Unit_Harvester)<4) {
            const int index=house->getNumItems(Unit_Harvester);
            auto* worker=house->createUnit(Unit_Harvester,true);
            require(worker!=nullptr,"Colonisation fixture could not establish its worker fleet");
            worker->deploy(at(28,index+3));
        }
        // The advanced core the colonisation gate demands. The heavy factory
        // is a real builder because the MCV has to be ordered from one; the
        // other two are counted, exactly as the fixture counts them away above.
        auto* yard=house->placeStructure(NONE_ID,Structure_ConstructionYard,at(1,4).x,at(1,4).y,true,true);
        auto* factory=house->placeStructure(NONE_ID,Structure_HeavyFactory,at(4,4).x,at(4,4).y,true,true);
        require(yard!=nullptr && factory!=nullptr,"Colonisation fixture could not build its base");
        // Cover the roomy base's far edge with real construction anchors, so
        // the old raw-tile exhaustion rule also sees at least 48 free tiles.
        for(Coord post:{Coord(9,4),Coord(9,11),Coord(1,11),Coord(5,11),Coord(9,8)})
            require(house->placeStructure(NONE_ID,Structure_RocketTurret,
                at(post.x,post.y).x,at(post.x,post.y).y,true,true)!=nullptr,
                "Colonisation fixture could not anchor its roomy base");
        house->numItem[Structure_HighTechFactory]=1;
        house->numItem[Structure_RepairYard]=1;
        house->producedPower=10000;
        auto* builder=static_cast<BuilderBase*>(factory);
        builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        require(bot->expansionDefenceReady(),"Colonisation fixture needs the expansion gate open");

        // No demand pressure and yards to spare: the production-yard target is
        // satisfied, which is the state in which the reported city stopped
        // growing altogether.
        auto* sim=game.getCitySimulation();
        auto& state=sim->houseState_[house->getHouseID()];
        state.resValve=0;state.comValve=0;state.indValve=0;
        auto fund=[&](int credits) {
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
        };
        auto queuedMcvs=[&]() {
            int queued=0;
            for(const auto& order:builder->currentProductionQueue) if(order.itemID==Unit_MCV) ++queued;
            return queued;
        };
        auto clearQueues=[&]() {
            for(auto* structure:structureList) if(structure->getOwner()==house && structure->isABuilder()) {
                auto* producer=static_cast<BuilderBase*>(structure);
                while(producer->getProductionQueueSize())producer->doCancelItem(producer->getCurrentProducedItem());
            }
        };
        auto survey=[&]() {
            clearQueues();
            fund(30000);
            bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
            bot->rockSurveyCycle=std::numeric_limits<Uint32>::max();
            bot->build(0);
        };
        require(house->getNumItems(Structure_ConstructionYard)
                >= QuantBotBuildPolicy::cityConstructionYardTarget(house->getCredits(),0,0,0),
            "Colonisation fixture must run with the production-yard target already satisfied");
        bool mcvBuildable=false;
        for(const auto& offer:builder->getBuildList()) if(offer.itemID==Unit_MCV) mcvBuildable=true;

        // 7a. Room at home is not a reason to colonise. The survey sees the
        //     distant formation, and still nothing is ordered or driven away.
        survey();
        SDL_Log("MCV_DEPLOYMENT_NOTE: roomy base footprints=%d production_blocked=%d site=%d,%d",
            bot->availableBaseFootprints,int(bot->baseProductionRoomBlocked),
            bot->rockExpansionSite.x,bot->rockExpansionSite.y);
        require(bot->availableBaseFootprints>=QuantBotColonisationPolicy::kCrampedFootprints,
            "Colonisation fixture must start with building room at home");
        require(!bot->baseProductionRoomBlocked,
            "The placement search found no room for a factory on eighty free tiles of rock:"
            " the room measurement, not the fixture, is what failed here");
        require(!bot->baseBuiltOut(),"A base with open rock reported itself built out");
        require(!bot->colonisationMcvDue(0,0),"A base with room ordered a colonist");
        require(!bot->colonyMissionDue(),"A base with room sent its MCVs away");
        require(queuedMcvs()==0,"A base with room bought an MCV to colonise with");
        {
            // The fast local path is untouched while the base has room: an MCV
            // standing on it is still given a footprint at home.
            auto* local=static_cast<MCV*>(house->createUnit(Unit_MCV,true));
            require(local!=nullptr,"Colonisation fixture could not create an MCV");
            local->deploy(at(7,9));
            require(bot->findLocalDeploySite(local).isValid(),
                "Local deployment stopped offering sites on a roomy base");
            require(!bot->colonyMissionDue(),"A roomy base turned a local MCV into a colonist");
            local->setVisible(VIS_ALL,false);local->destroy();
            game.processObjects();
        }

        // 7b. The base builds out. Loose rock is deliberately left behind —
        //     more of it than the old free-tile rule ever called cramped — but
        //     one tile wide, so not one more building fits on any of it. This
        //     is the reported city: hundreds of free tiles, nowhere to build.
        for(int y=4;y<13;++y) for(int x=1;x<26;++x)
            if(!groundTile(x,y)->hasAStructure())
                groundTile(x,y)->setType(y%2 ? Terrain_Rock : Terrain_Sand);
        // Spread the base's own build range across that rock, so the survey
        // counts it as the city's own ground rather than distant desert.
        for(Coord post:{Coord(12,5),Coord(18,5),Coord(24,5),Coord(12,9),Coord(18,9),Coord(24,9)}) {
            auto* turret=house->placeStructure(NONE_ID,Structure_RocketTurret,
                at(post.x,post.y).x,at(post.x,post.y).y,true,true);
            require(turret!=nullptr,"Colonisation fixture could not spread its build range");
        }

        // 7c. An unreachable destination is not a destination. The formation
        //     is walled off, and a built-out base still orders nothing.
        for(int x=35;x<49;++x) { groundTile(x,2)->setType(Terrain_Mountain);groundTile(x,15)->setType(Terrain_Mountain); }
        for(int y=2;y<16;++y) { groundTile(35,y)->setType(Terrain_Mountain);groundTile(48,y)->setType(Terrain_Mountain); }
        survey();
        SDL_Log("MCV_DEPLOYMENT_NOTE: built-out base free_rock=%d footprints=%d production_blocked=%d",
            bot->availableBaseRock,bot->availableBaseFootprints,int(bot->baseProductionRoomBlocked));
        require(bot->availableBaseRock>=48,
            "The fixture must keep more free rock than the old exhaustion rule reacts to,"
            " or this section would not be testing the space-driven decision at all");
        require(bot->baseBuiltOut(),"A base with nowhere to build did not report itself built out");
        require(!bot->rockExpansionSite.isValid(),"A walled-off formation was accepted as a destination");
        require(!bot->colonisationMcvDue(0,0),"A built-out base ordered a colonist with nowhere to go");
        require(queuedMcvs()==0,"An MCV was bought for an unreachable destination");

        // 7d. Open the route again, but put an enemy on the far rock. Threat
        //     keeps it out of the survey while that enemy is there.
        for(int x=35;x<49;++x) { groundTile(x,2)->setType(Terrain_Sand);groundTile(x,15)->setType(Terrain_Sand); }
        for(int y=2;y<16;++y) { groundTile(35,y)->setType(Terrain_Sand);groundTile(48,y)->setType(Terrain_Sand); }
        House* enemy=nullptr;
        for(int id=0;id<NUM_HOUSES && enemy==nullptr;++id) {
            auto* other=game.getHouse(id);
            if(other!=nullptr && other!=house && other->getTeamID()!=house->getTeamID()) enemy=other;
        }
        require(enemy!=nullptr,"Colonisation fixture needs an enemy house");
        auto* raider=enemy->createUnit(Unit_Quad,true);
        require(raider!=nullptr,"Colonisation fixture could not create an enemy unit");
        raider->deploy(at(42,9));
        raider->setVisible(VIS_ALL,true);
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        survey();
        require(!bot->rockExpansionSite.isValid(),"Rock an enemy is standing on was accepted");
        require(!bot->colonisationMcvDue(0,0),"A colonist was ordered towards a held formation");
        require(queuedMcvs()==0,"An MCV was bought for rock the enemy still holds");

        // 7e. The enemy is destroyed. The same rock becomes eligible again on
        //     the next survey: nothing about it was remembered as refused.
        const Uint32 raiderID=raider->getObjectID();
        raider->setVisible(VIS_ALL,false);raider->destroy();
        game.processObjects();
        require(game.getObjectManager().getObject(raiderID)==nullptr,"The enemy unit survived the fixture");
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        survey();
        require(bot->rockExpansionSite.isValid(),
            "Rock cleared of its enemy stayed refused: an availability decision outlived the threat");
        require(bot->colonisationMcvDue(0,0),
            "A built-out city with safe reachable rock still refused to colonise it");
        require(bot->colonyMissionDue(),"A built-out city did not treat its MCVs as colonists");
        require(bot->availableBaseRock>=48 && house->getNumItems(Structure_ConstructionYard)
                >= QuantBotBuildPolicy::cityConstructionYardTarget(house->getCredits(),0,0,0),
            "The order below must be attributable to colonisation alone: free rock and the"
            " production-yard target both have to leave the older MCV rules quiet");
        require(mcvBuildable,"Colonisation fixture did not unlock MCV production");
        require(queuedMcvs()==1,
            "A built-out city with somewhere to settle bought no MCV");

        // 7f. One colonist, not a stream of them: an MCV already bought is the
        //     answer to the same survey.
        auto* colonist=static_cast<MCV*>(house->createUnit(Unit_MCV,true));
        require(colonist!=nullptr,"Colonisation fixture could not create its colonist");
        colonist->deploy(at(7,9));
        const Uint32 colonistID=colonist->getObjectID();
        require(!bot->colonisationMcvDue(1,0),"A second MCV was ordered while one was already bought");
        survey();
        require(queuedMcvs()==0,"An MCV was queued while a colonist already existed");

        // 7g. The colonist drives to the surveyed formation and founds the
        //     city there, on real engine frames.
        const Coord destination=bot->rockExpansionSite;
        require(destination.isValid(),"The colonisation survey lost its site");
        require(destination.x>=ground.x+36 && destination.y>=ground.y+3,
            "The colonisation site is not on the far formation");
        for(int pass=0;pass<3 && bot->mcvExpansionSites.find(colonistID)==bot->mcvExpansionSites.end();++pass) {
            colonist->setForced(false);
            bot->manageMcv(colonist);
            game.gameCycleCount+=MILLI2CYCLES(6000);
        }
        const auto assigned=bot->mcvExpansionSites.find(colonistID);
        require(assigned!=bot->mcvExpansionSites.end(),"The colonist was given no site at all");
        const Coord target=assigned->second;
        require(target.x>=ground.x+36 && target.x<ground.x+48
            && target.y>=ground.y+3 && target.y<ground.y+15,
            "The colonist was kept on the built-out base instead of the free formation");
        const Uint32 deadline=game.getGameCycleCount()+MILLI2CYCLES(240000);
        Coord arrived=colonist->getLocation();
        while(game.getObjectManager().getObject(colonistID)!=nullptr && game.getGameCycleCount()<deadline) {
            game.processObjects();++game.gameCycleCount;
            if(game.getObjectManager().getObject(colonistID)==nullptr) break;
            arrived=colonist->getLocation();
            if(game.getGameCycleCount()%50<1) bot->manageMcv(colonist);
        }
        require(game.getObjectManager().getObject(colonistID)==nullptr,
            "The colonist never arrived and founded its city");
        require(currentGameMap->getTile(target)->hasAStructure(),
            "No construction yard stands on the settled formation");
        require(arrived.x>=ground.x+36,"The colonist deployed before reaching the new formation");
        // Settling is not a licence to keep buying: the survey now sees a yard
        // on that formation, so the same pass must not order another colonist.
        bot->rockSurveyCycle=std::numeric_limits<Uint32>::max();
        survey();
        require(queuedMcvs()==0,"A colonised formation kept ordering further MCVs");
    }

    SDL_Log("MCV_DEPLOYMENT_PASS: prompt same-rock deployment at every difficulty, one driven arrival,"
        " distinct reservations, blocked-site handling, the colony gate,"
        " and space-driven colonisation of safe reclaimed rock");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=mcv_deployment outcome=passed");
}
