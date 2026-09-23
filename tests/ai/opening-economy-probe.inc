#include <structures/Refinery.h>
#include <units/Harvester.h>

// Drive real build decisions on All against Atreides, with its empty market.
void runOpeningEconomyProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("OPENING_ECONOMY_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_ATREIDES);
    QuantBot* bot=nullptr;BuilderBase* yard=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    for(auto* structure:structureList)
        if(structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard)
            yard=static_cast<BuilderBase*>(structure);
    require(bot && yard,"Missing opening controller/yard");
    // This fixture compares economic tech routes in isolation; proactive
    // rocket technology/coverage is exercised by the defence fixtures.
    if(game.isCitySimEnabled()) {
        game.objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].enabled=false;
        yard->updateBuildList();
    }
    game.gameCycleCount=123;bot->update();
    require(house->getNumItems(Structure_HeavyFactory)==0,"Opening must start without heavy production");
    for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Slab);
    auto clearOrders=[&] {
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
        yard->upgrading=false;yard->setHealth(yard->getMaxHealth());
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
    };
    auto place=[&](Uint32 item) {
        clearOrders();const auto site=bot->findPlaceLocation(item);
        require(site.isValid(),"No fixture placement");
        require(house->placeStructure(NONE_ID,item,site.x,site.y,true,true),"Fixture placement failed");
        yard->updateBuildList();
    };
    auto build=[&](int credits) {
        clearOrders();house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits; // Explicit scenario funding, not an over-capacity spice deposit.
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        for(const auto& order:yard->currentProductionQueue)
            if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                return order.itemID;
        return Uint32(NONE_ID);
    };
    auto storageBuild=[&](int capacity,int stored,int city) {
        clearOrders();
        house->capacity=capacity;house->startingCredits=0;
        house->storedCredits=stored;house->cityCredits=city;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        for(const auto& order:yard->currentProductionQueue)
            if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                return order.itemID;
        return Uint32(NONE_ID);
    };
    place(Structure_WindTrap);place(Structure_Refinery);
    require(build(100000)==Structure_LightFactory,"Rich empty-market opening failed to unlock factory before extra refineries");
    house->getChoam().addItem(Unit_Harvester,0); // Supported, temporarily sold out.
    require(build(1800)==Structure_Refinery,"Poor opening lost the supported Starport economy path");
    require(build(100000)==Structure_LightFactory,"Rich opening waited for refineries despite ample operating funds");
    place(Structure_LightFactory);
    require(build(100000)==Structure_Radar,"Rich opening skipped its heavy-factory prerequisite");
    place(Structure_Radar);
    require(build(100000)==Structure_HeavyFactory,"Rich opening built repair/Starport before first heavy factory");
    place(Structure_HeavyFactory);
    house->getChoam().availableItems.clear();
    // With prerequisites built and spending otherwise exhausted, the fallback
    // must not construct the absent market merely because a heavy line exists.
    for(Uint32 item:{Structure_Radar,Structure_RepairYard,Structure_HighTechFactory,Structure_IX,Structure_Palace})
        if(!house->getNumItems(item))place(item);
    while(house->getNumItems(Structure_Refinery)<4)place(Structure_Refinery);
    bot->militaryValueLimit=5100;
    require(build(1800)!=Structure_StarPort,"Heavy factory bypassed absent-market check");
    house->getChoam().addItem(Unit_Harvester,0);
    require(build(1800)==Structure_StarPort,"Sold-out supported imports incorrectly treated as disabled");
    auto& worker=game.objectData.data[Unit_Harvester][HOUSE_ATREIDES];
    const bool enabled=worker.enabled;worker.enabled=false;
    require(build(1800)!=Structure_StarPort,"All disabled imports still caused Starport construction");
    worker.enabled=enabled;clearOrders();

    // Default must ignore the old cap in saved houses; explicit options win.
    house->maxHarvesters=60;
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=-1;
    bot->lastCalculatedSpice=1300000;bot->update();
    require(house->getMaxHarvesters()==0 && bot->harvesterLimit>100,"Default retained map-size worker ceiling");
    const int saved=house->numItem[Unit_Harvester];house->numItem[Unit_Harvester]=100;
    require(!house->isUnitLimitReached(Unit_Harvester),"Default still blocks the 101st worker");
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=100;bot->update();
    require(house->getMaxHarvesters()==100 && house->isUnitLimitReached(Unit_Harvester)
        && bot->harvesterLimit==100,"Explicit100 cap ignored");
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=0;bot->update();
    require(!house->isUnitLimitReached(Unit_Harvester) && bot->harvesterLimit>100,"Explicit unlimited froze AI production");
    bot->lastCalculatedSpice=1000;bot->update();
    require(bot->harvesterLimit<=1,"Unlimited engine cap removed finite-spice planning");
    house->numItem[Unit_Harvester]=saved;

    // Distant loaded returners cannot request a lift to occupied refineries.
    // They must count as a queue before walking within six tiles of the base.
    house->getChoam().availableItems.clear(); // Restore this map's absent market.
    Refinery* occupied=nullptr;
    for(auto* structure:structureList)if(structure->getOwner()==house && structure->getItemID()==Structure_Refinery) {
        auto* refinery=static_cast<Refinery*>(structure);
        auto* worker=static_cast<Harvester*>(house->createUnit(Unit_Harvester,true));
        require(worker,"Could not create unloading worker");
        worker->setAmountOfSpice(HARVESTERMAXSPICE);worker->setActive(false);
        refinery->book();refinery->assignHarvester(worker);occupied=refinery;
    }
    require(occupied,"Missing unloading bay");
    for(int i=0;i<2;++i) {
        auto* worker=static_cast<Harvester*>(house->createUnit(Unit_Harvester,true));
        require(worker,"Could not create field returner");
        bool deployed=false;
        for(int y=1;y<currentGameMap->getSizeY()-1 && !deployed;++y)
            for(int x=1;x<currentGameMap->getSizeX()-1 && !deployed;++x)
                if(!currentGameMap->getTile(x,y)->hasAGroundObject()
                    && blockDistance(Coord(x,y),occupied->getLocation())>12) {
                    worker->deploy(Coord(x,y));deployed=true;
                }
        require(deployed,"No field returner location");
        worker->setAmountOfSpice(HARVESTERMAXSPICE);worker->setTarget(occupied);
    }
    build(2000);game.gameCycleCount+=MILLI2CYCLES(11000);
    require(build(2000)==Structure_Refinery,"Distant blocked returners failed to fund additional unloading capacity");
    SDL_Log("REFINERY_QUEUE_PASS: full field returners trigger capacity before reaching the base");

    // Mature bases must relieve a full earned-income bank before continuing
    // optional expansion. Exercise the real order path at every difficulty.
    const auto savedDifficulty=bot->difficulty;
    for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
            QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal,QuantBot::Difficulty::Defend}) {
        bot->difficulty=difficulty;
        require(storageBuild(35025,35023,0)==Structure_Silo,"Near-full storage was starved by optional investment");
        require(storageBuild(35025,10000,25023)==Structure_Silo,"City income was ignored by storage expansion");
        require(storageBuild(35025,1000,0)!=Structure_Silo,"Low storage usage triggered a silo");
        require(storageBuild(999999,999999,0)!=Structure_Silo,"Silos grew beyond the absolute credit ceiling");
    }
    bot->difficulty=savedDifficulty;
    // A paid capacity order already in a second yard prevents duplicate silos.
    clearOrders();
    const auto secondSite=bot->findPlaceLocation(Structure_ConstructionYard);
    require(secondSite.isValid(),"No second yard site");
    auto* secondYard=dynamic_cast<BuilderBase*>(house->placeStructure(NONE_ID,
        Structure_ConstructionYard,secondSite.x,secondSite.y,true,true));
    require(secondYard,"Second yard placement failed");
    clearOrders();house->capacity=35025;house->startingCredits=0;
    house->storedCredits=35023;house->cityCredits=0;house->producedPower=10000;
    bot->build(5100);
    int silos=0;
    for(auto* builder:{yard,secondYard})for(const auto& order:builder->currentProductionQueue)
        if(order.itemID==Structure_Silo)++silos;
    require(silos==1,"Parallel yards duplicated or omitted the storage order");
    clearOrders();
    // Losing core infrastructure re-establishes its priority over storage.
    auto destroyCore=[&](Uint32 item) {
        StructureBase* victim=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==item) {victim=structure;break;}
        require(victim,"Missing core fixture building");
        victim->destroy();
    };
    if(game.isCitySimEnabled()) {
        destroyCore(Structure_HighTechFactory);
        require(storageBuild(35025,35023,0)!=Structure_Silo,"Storage displaced missing high-tech core");
    }
    destroyCore(Structure_HeavyFactory);
    require(storageBuild(35025,35023,0)!=Structure_Silo,"Storage displaced missing heavy factory/core infrastructure");
    SDL_Log("STORAGE_PRIORITY_PASS: core first, all difficulties, spice/tax capacity, pending orders and absolute ceiling");
    SDL_Log("OPENING_ECONOMY_PASS: rich factory opening, poor economy opening, absent/sold-out/disabled markets, default/100/unlimited caps");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=0 seed=0 attack_percent=0 partner=brutal enemy=hard cycle=%u outcome=opening_economy_pass",game.getGameCycleCount());
}
