#include <structures/Refinery.h>
#include <units/Harvester.h>
#include <players/QuantBotBuildPolicy.h>

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
    // Real queue checks while the opening still has an unambiguous productive
    // next building. Isolate policies from the later mature-base fixtures.
    {
        const bool savedConcrete=game.gameInitSettings.gameOptions.concreteRequired;
        const auto savedDifficulty=bot->difficulty;
        const auto savedUpgrade=yard->curUpgradeLev;
        const auto slab4=game.objectData.data[Structure_Slab4][HOUSE_ATREIDES];
        if (house->getNumItems(Unit_Harvester)==0) {
            auto* income=house->createUnit(Unit_Harvester,true);
            require(income!=nullptr,"Could not create opening income worker");
        }
        game.gameInitSettings.gameOptions.concreteRequired=true;
        for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                               QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            yard->curUpgradeLev=0;yard->updateBuildList();
            build(100000);
            require(yard->isUpgrading(),"Funded opening did not upgrade the yard before optional infrastructure");
        }
        // The rest of the opening tests exercise build selection after the
        // new foundation upgrade, so give them the unlocked level explicitly.
        clearOrders();yard->curUpgradeLev=std::max<int>(savedUpgrade,slab4.upgradeLevel);
        auto queueCheck=[&](bool required,bool bulk) {
            game.gameInitSettings.gameOptions.concreteRequired=required;
            game.objectData.data[Structure_Slab4][HOUSE_ATREIDES].enabled=bulk;
            yard->updateBuildList();
            for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
                auto* tile=currentGameMap->getTile(x,y);
                if(!tile->hasAStructure() && tile->isConcrete())tile->setType(Terrain_Rock);
            }
            const auto item=build(100000);
            require(item!=NONE_ID,"Foundation opening did not order a building");
            const auto size=getStructureSize(item);
            const auto& info=game.objectData.data[item][HOUSE_ATREIDES];
            const bool wanted=QuantBotBuildPolicy::foundationPriorityStructure(item,game.isCitySimEnabled())
                || QuantBotBuildPolicy::foundationPaysForItself(info.hitpoints,info.price,
                    size.x*size.y*game.objectData.data[Structure_Slab1][HOUSE_ATREIDES].price);
            int singles=0,large=0;
            for(const auto& order:yard->currentProductionQueue) {
                singles+=order.itemID==Structure_Slab1;
                large+=order.itemID==Structure_Slab4;
            }
            if(required && wanted) {
                require(singles+4*large==size.x*size.y,"Productive opening building lacks its full foundation");
                if(bulk && size.x>=2 && size.y>=2)require(large>0,"Upgraded yard failed to use 2x2 concrete");
                if(!bulk)require(large==0,"Disabled 2x2 concrete was ordered");
            } else require(singles==0 && large==0,"Disabled or uneconomic concrete entered the AI queue");
        };
        for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                               QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            queueCheck(false,true);queueCheck(true,false);queueCheck(true,true);
        }
        // Legacy non-city campaigns use a separate reconstruction branch.
        // It must share both the early upgrade and the foundation queue rules.
        if (!game.isCitySimEnabled()) {
            const auto mode=bot->gameMode;
            const auto type=game.gameType;
            const int originalLight=bot->initialItemCount[Structure_LightFactory];
            bot->gameMode=QuantBot::GameMode::Campaign;game.gameType=GameType::Campaign;
            bot->initialItemCount[Structure_LightFactory]=1;
            for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                                   QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
                bot->difficulty=difficulty;
                game.gameInitSettings.gameOptions.concreteRequired=true;
                yard->curUpgradeLev=0;yard->updateBuildList();
                build(100000);
                require(yard->isUpgrading(),"Campaign rebuild bypassed the early concrete upgrade");
                clearOrders();yard->curUpgradeLev=std::max<int>(savedUpgrade,slab4.upgradeLevel);
                queueCheck(false,true);queueCheck(true,false);queueCheck(true,true);
            }
            clearOrders();bot->gameMode=mode;game.gameType=type;
            bot->initialItemCount[Structure_LightFactory]=originalLight;
        }
        if (game.isCitySimEnabled()) {
            bot->clearPlacementCache();
            const auto oldSite=bot->findPlaceLocation(Structure_ZoneResidential);
            require(oldSite.isValid(),"Missing city-zone foundation site");
            currentGameMap->getTile(oldSite)->setType(Terrain_Sand);
            bot->clearPlacementCache();
            const auto site=bot->findPlaceLocation(Structure_ZoneResidential);
            require(site.isValid(),"No alternative prepared-ground city site");
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                require(currentGameMap->getTile(site.x+dx,site.y+dy)->isRock(),
                    "QuantBot selected sand for a required concrete foundation");
        }
        // Raw power matters even in Vanilla. At 50 credits and exactly half
        // health neither the rich nor emergency-health repair rule applies.
        StructureBase* wind=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==Structure_WindTrap)wind=structure;
        require(wind!=nullptr,"Missing windtrap repair fixture");
        for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                               QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            clearOrders();bot->difficulty=difficulty;
            wind->setHealth(FixPoint(wind->getMaxHealth())/2);wind->repairing=false;
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=50;
            house->producedPower=0;
            bot->build(5100);
            require(wind->isRepairing(),"Damaged windtrap was not repaired during a power shortage");
            wind->setHealth(wind->getMaxHealth());wind->repairing=false;
        }
        clearOrders();bot->difficulty=savedDifficulty;
        game.gameInitSettings.gameOptions.concreteRequired=savedConcrete;
        game.objectData.data[Structure_Slab4][HOUSE_ATREIDES]=slab4;
        yard->updateBuildList();
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
            if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Slab);
        SDL_Log("FOUNDATION_POLICY_PASS: all difficulties, early yard upgrade, singles, bulk and concrete disabled");
    }
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

    // Vanilla and Dune2R progression. An enemy wing elsewhere on the map buys
    // the baseline emplacements and then waits behind the production core and
    // the advanced factories, instead of owning every slot for the whole game.
    if(!game.isCitySimEnabled()) {
        House* airEnemy=nullptr;
        for(int h=0;h<NUM_HOUSES && !airEnemy;++h) {
            House* other=game.getHouse(h);
            if(other && other->getTeamID()!=house->getTeamID())airEnemy=other;
        }
        require(airEnemy,"No enemy house for air pressure");
        yard->curUpgradeLev=std::max<Uint8>(yard->curUpgradeLev,2); // Emplacements already unlocked.
        secondYard->curUpgradeLev=yard->curUpgradeLev;
        secondYard->updateBuildList();
        // IX requires a Starport even when this fixture has no import stock.
        for(Uint32 item:{Structure_HeavyFactory,Structure_RepairYard,Structure_StarPort})
            if(!house->getNumItems(item))place(item);
        // Other non-city mods retain infantry prerequisites in their opening.
        if(house->isPowerRequired())
            for(Uint32 item:{Structure_Barracks,Structure_WOR})
                if(!house->getNumItems(item))place(item);
        if(house->getNumItems(Structure_HighTechFactory))destroyCore(Structure_HighTechFactory);
        if(house->getNumItems(Structure_IX))destroyCore(Structure_IX);
        while(house->getNumItems(Structure_RocketTurret)<2)place(Structure_RocketTurret);
        // The unloading-queue fixture above has been made; empty its cargo so
        // these decisions are about progression rather than trapped spice.
        for(auto* unit:unitList) if(unit->getOwner()==house && unit->getItemID()==Unit_Harvester) {
            auto* worker=static_cast<Harvester*>(unit);
            worker->setAmountOfSpice(0);worker->setTarget(nullptr);
        }
        // Restore the finite-spice planning the harvester-cap fixture above
        // deliberately starved, so refining decisions see a real field again.
        clearOrders();bot->lastCalculatedSpice=1300000;bot->update();
        require(bot->harvesterLimit>10,"Fixture could not restore a funded worker plan");
        // Expire unrelated structure-loss recovery bookkeeping from fixture setup.
        game.gameCycleCount+=MILLI2CYCLES(200000);
        const int fieldWorkers=house->numItem[Unit_Harvester];
        house->numItem[Unit_Harvester]=3; // The four bays already outrun this fleet.
        airEnemy->numItem[Unit_Ornithopter]=6;
        require(build(100000)==Structure_HighTechFactory,
            "Distant enemy aircraft blocked the affordable missing production core");
        place(Structure_HighTechFactory);
        require(build(100000)==Structure_IX,"Distant enemy aircraft blocked affordable advanced tech");
        // Earned credits at capacity are lost income now; that does not wait
        // for tech the base has not finished.
        require(storageBuild(35025,35023,0)==Structure_Silo,"Overflowing storage waited for optional tech");
        place(Structure_IX);
        require(build(100000)==Structure_RocketTurret,
            "A finished core and IX did not release the counter-aircraft response");

        // Tech this tree cannot reach never reserves a slot and never withholds
        // the defence the base can actually build.
        auto& advanced=game.objectData.data[Structure_HighTechFactory][HOUSE_ATREIDES];
        auto& advancedTech=game.objectData.data[Structure_IX][HOUSE_ATREIDES];
        const int savedTechLevel=game.techLevel;
        const int loweredTech=std::max(1,std::min<int>(advanced.techLevel,advancedTech.techLevel)-1);
        if(loweredTech>=game.objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].techLevel) {
            game.techLevel=loweredTech;
            if(house->getNumItems(Structure_IX))destroyCore(Structure_IX);
            if(house->getNumItems(Structure_HighTechFactory))destroyCore(Structure_HighTechFactory);
            game.gameCycleCount+=MILLI2CYCLES(200000);
            yard->updateBuildList();secondYard->updateBuildList();
            const Uint32 lowTech=build(100000);
            require(lowTech!=Structure_HighTechFactory && lowTech!=Structure_IX,
                "Construction ordered advanced tech this tech level does not offer");
            require(lowTech==Structure_RocketTurret,
                "An unreachable advanced tree still withheld the defence the base could build");
            game.techLevel=savedTechLevel;
            yard->updateBuildList();secondYard->updateBuildList();
            for(Uint32 item:{Structure_HighTechFactory,Structure_IX})
                if(!house->getNumItems(item))place(item);
            game.gameCycleCount+=MILLI2CYCLES(200000);
        }
        airEnemy->numItem[Unit_Ornithopter]=6;

        // Processing capacity follows the worker fleet and the field, not a
        // cash surplus: four bays are not a ceiling, and empty ones are not a
        // reason to keep buying more.
        house->numItem[Unit_Harvester]=15;
        require(build(4000)==Structure_Refinery,
            "A funded worker fleet did not grow refining capacity past four bays");
        airEnemy->numItem[Unit_Ornithopter]=0;
        house->numItem[Unit_Harvester]=3;
        require(build(4000)!=Structure_Refinery,"Empty bays kept expanding without workers to fill them");

        // A queued bay brings its own storage. Count that pending capacity
        // rather than cancelling every silo a growing refinery base still needs.
        const int bayCapacity=game.objectData.data[Structure_Refinery][HOUSE_ATREIDES].capacity;
        require(bayCapacity>0,"Fixture needs refinery storage capacity");
        auto storageWithPendingBay=[&](int capacity,int stored) {
            clearOrders();
            house->capacity=capacity;house->startingCredits=0;
            house->storedCredits=stored;house->cityCredits=0;
            house->producedPower=10000;bot->lastCalculatedSpice=1300000;
            secondYard->doProduceItem(Structure_Refinery);
            require(secondYard->getProductionQueueSize()>0,"Fixture could not queue the pending bay");
            bot->build(5100);
            int silos=0;
            for(auto* builder:{yard,secondYard})for(const auto& order:builder->currentProductionQueue)
                if(order.itemID==Structure_Silo)++silos;
            return silos;
        };
        require(storageWithPendingBay(20000,19000)==1,
            "A queued refinery cancelled the storage the bank still needed");
        require(storageWithPendingBay(20000,(20000+bayCapacity)*79/100)==0,
            "Pending refinery capacity was not counted before ordering another silo");

        // Defence never invents a power buffer. With the option off no
        // generator is bought at all; with it on, the emplacement's own draw is
        // the requirement, and a real deficit still builds its generator.
        const int turretPower=game.objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].power;
        auto pressuredBuild=[&](int credits,int power) {
            clearOrders();house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
            house->producedPower=power;bot->lastCalculatedSpice=1300000;
            bot->build(5100);
            for(const auto& order:yard->currentProductionQueue)
                if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                    return order.itemID;
            return Uint32(NONE_ID);
        };
        airEnemy->numItem[Unit_Ornithopter]=6;
        const bool savedTurretPower=game.gameInitSettings.gameOptions.rocketTurretsNeedPower;
        game.gameInitSettings.gameOptions.rocketTurretsNeedPower=false;
        require(pressuredBuild(100000,house->getPowerRequirement())!=Structure_WindTrap,
            "Defence bought a power buffer with the turret power option off");
        game.gameInitSettings.gameOptions.rocketTurretsNeedPower=true;
        require(pressuredBuild(100000,house->getPowerRequirement()+turretPower)!=Structure_WindTrap,
            "A covered turret draw still reserved spare generation");
        if(turretPower>0) {
            const Uint32 deficitBuild=pressuredBuild(100000,house->getPowerRequirement());
            require(deficitBuild==Structure_WindTrap || deficitBuild==Structure_NuclearPlant,
                "A real turret power deficit no longer builds its generator");
        }
        game.gameInitSettings.gameOptions.rocketTurretsNeedPower=savedTurretPower;
        airEnemy->numItem[Unit_Ornithopter]=0;
        house->numItem[Unit_Harvester]=fieldWorkers;
        clearOrders();
        SDL_Log("VANILLA_PROGRESSION_PASS: core and IX before repeated cover, storage before tech, "
            "worker-led refining, pending bay capacity, genuine turret power");
    }
    SDL_Log("OPENING_ECONOMY_PASS: rich factory opening, poor economy opening, absent/sold-out/disabled markets, default/100/unlimited caps");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=0 seed=0 attack_percent=0 partner=brutal enemy=hard cycle=%u outcome=opening_economy_pass",game.getGameCycleCount());
}
