// Real-engine policy integration probe; never included in the shipped executable.
void runCampaignCityEconomyProbe(Game& game) {
    auto require=[](bool ok,const char* reason) {
        if(!ok) {SDL_Log("CITY_CAMPAIGN_FAIL: %s",reason);throw std::runtime_error(reason);}
    };
    require(game.isCitySimEnabled(),"Fixture requires Dune City");
    QuantBot* enemy=nullptr; QuantBot* ally=nullptr;
    std::set<Uint32> original;
    for(int h=0;h<NUM_HOUSES;++h) if(auto* owner=game.getHouse(h)) {
        for(const auto& player:owner->getPlayerList()) if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {
            if(bot->isAlliedWithHuman()) ally=bot;
            else if(!enemy && owner->getNumItems(Structure_Refinery)>0) enemy=bot;
        }
    }
    require(enemy && ally,"Fixture needs economic enemy and human-side helper");
    auto* owner=const_cast<House*>(enemy->getHouse());
    for(Uint32 item=ItemID_FirstID;item<=ItemID_LastID;++item)
        if(isStructure(item) && owner->getNumItems(item)>0) original.insert(item);
    const int originalRefineries=owner->getNumItems(Structure_Refinery);
    game.gameCycleCount=123;enemy->update();ally->update();
    require(enemy->campaignCityEconomy() && !ally->campaignCityEconomy(),"Policy leaked to allied helper");
    require(enemy->campaignBaseline.refineries==originalRefineries,"Baseline lost original refinery count");
    for(auto item:{Structure_LightFactory,Structure_HeavyFactory,Structure_HighTechFactory,
                   Structure_Radar,Structure_RepairYard,Structure_IX,Structure_Palace})
        require(enemy->campaignPermitsStructure(item)==bool(original.count(item)),"RTS building permission differs from original mission");
    require(enemy->campaignPermitsStructure(Structure_ZoneResidential)
        && enemy->campaignPermitsStructure(Structure_ZoneCommercial)
        && enemy->campaignPermitsStructure(Structure_ZoneIndustrial),"RCI mix was restricted");
    const auto before=enemy->campaignCityLimits();
    require(before.sharedZoneCap>0 && before.harvesters<=enemy->campaignBaseline.allowance,"Invalid campaign economy limits");

    const auto originalMod=game.gameInitSettings.modName;
    game.gameInitSettings.modName="vanilla";
    require(!enemy->campaignCityEconomy() && enemy->campaignCanAddHarvester(),"Policy leaked into vanilla");
    game.gameInitSettings.modName="custom-city";
    require(!enemy->campaignCityEconomy(),"Policy leaked into another city mod");
    game.gameInitSettings.modName=originalMod;
    const auto originalGameType=game.gameType;
    game.gameType=GameType::CustomGame;
    require(!enemy->campaignCityEconomy(),"Policy leaked into custom games");
    game.gameType=originalGameType;

    auto place=[&](Uint32 item) {
        for(int y=1;y+4<currentGameMap->getSizeY();y+=4)
            for(int x=1;x+4<currentGameMap->getSizeX();x+=4) {
                bool clear=true;
                for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
                if(clear) return owner->placeStructure(NONE_ID,item,x,y,true,true);
            }
        throw std::runtime_error("No policy fixture site");
    };
    auto* first=dynamic_cast<BuilderBase*>(place(Structure_ConstructionYard));
    auto* second=dynamic_cast<BuilderBase*>(place(Structure_ConstructionYard));
    require(first && second,"Two-yard fixture failed");
    owner->addCredits(100000);
    for(auto* yard:{first,second})
        for(auto kind:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial,Structure_LightFactory})
            if(!yard->isAvailableToBuild(kind)) yard->buildList.emplace_back(kind,100);
    const auto baseline=enemy->campaignBaseline;
    enemy->campaignBaseline.allowance=100; // Avoid income cutoff while testing the physical queue ceiling.
    const int cap=enemy->campaignCityLimits().sharedZoneCap;
    const Uint32 kinds[]={Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial};
    for(int i=0;i<cap+6;++i) enemy->doProduceItem(i%2 ? second:first,kinds[i%3]);
    int committed=0;
    for(auto kind:kinds) committed+=enemy->campaignCommittedCount(kind);
    require(committed==cap,"Shared RCI cap did not include all yards and zone types");
    for(auto* yard:{first,second}) for(auto kind:kinds)
        while(yard->getBuildItem(kind)->num>0) enemy->doCancelItem(yard,kind);
    enemy->campaignBaseline=baseline;
    if(!original.count(Structure_LightFactory)) {
        enemy->doProduceItem(first,Structure_LightFactory);
        require(first->getBuildItem(Structure_LightFactory)->num==0,"Actual order introduced forbidden light factory");
    }

    // A paid factory order must reserve the slot before a refinery can deliver.
    const auto difficulty=enemy->difficulty;
    enemy->difficulty=QuantBot::Difficulty::Brutal;
    enemy->campaignBaseline.allowance=7;
    enemy->harvesterLimit=enemy->campaignHarvesterTarget();
    first->buildList.emplace_back(Unit_Harvester,100);
    for(int i=0;i<10;++i) enemy->doProduceItem(first,Unit_Harvester);
    require(!enemy->campaignCanAddHarvester(),"Paid worker orders did not consume worker slots");
    const auto workersBefore=owner->getNumItems(Unit_Harvester);
    for(auto* structure:structureList) if(structure->getOwner()==owner && structure->getItemID()==Structure_Refinery) {
        owner->freeHarvester(structure->getLocation());break;
    }
    require(owner->getNumItems(Unit_Harvester)==workersBefore,"Refinery delivery overtook paid worker orders");
    while(first->getBuildItem(Unit_Harvester)->num>0) enemy->doCancelItem(first,Unit_Harvester);
    enemy->difficulty=difficulty;enemy->campaignBaseline=baseline;
    enemy->harvesterLimit=enemy->campaignHarvesterTarget();

    // The House-level delivery path must not evade the difficulty/worker target.
    auto* worker=static_cast<Harvester*>(nullptr);
    for(auto* unit:unitList) if(unit->getOwner()==owner && unit->getItemID()==Unit_Harvester) {
        worker=dynamic_cast<Harvester*>(unit);break;
    }
    require(worker,"Fixture needs a real worker for fractional-cargo checks");

    // Advance the actual update path with fractional spice present first.
    for(int y=0;y<currentGameMap->getSizeY();++y) for(int x=0;x<currentGameMap->getSizeX();++x)
        currentGameMap->getTile(x,y)->setSpice(0);
    for(auto* unit:unitList) if(unit->getOwner()==owner)
        if(auto* h=dynamic_cast<Harvester*>(unit)) h->setAmountOfSpice(0);
    auto* spiceTile=currentGameMap->getTile(0,0);
    spiceTile->setSpice(FixPoint(1)/4);
    const auto tick=[&](Uint32 cycle) {
        game.gameCycleCount=cycle;enemy->buildTimer=INT32_MAX;enemy->update();
    };
    for(Uint32 cycle=1000;cycle<3500;++cycle) tick(cycle);
    require(!enemy->campaignPostSpice(),"Fractional map spice was rounded away");
    spiceTile->setSpice(0);
    for(Uint32 cycle=3500;cycle<4000;++cycle) tick(cycle);
    const auto zeroSince=enemy->campaignSpiceZeroSince;
    require(zeroSince!=UINT32_MAX,"Fresh zero-spice scan was not recorded");
    for(Uint32 cycle=4000;cycle<zeroSince+MILLI2CYCLES(30000);++cycle) {
        tick(cycle);require(!enemy->campaignPostSpice(),"Post-spice allowance activated before30 game seconds");
    }
    for(Uint32 cycle=zeroSince+MILLI2CYCLES(30000);cycle<=zeroSince+MILLI2CYCLES(31000);++cycle) tick(cycle);
    require(enemy->campaignPostSpice(),"Confirmed map exhaustion did not activate");
    const auto exhausted=enemy->campaignCityLimits();
    const int expected=enemy->difficulty==QuantBot::Difficulty::Hard ? 24
        : enemy->difficulty==QuantBot::Difficulty::Brutal ? 40 : before.sharedZoneCap;
    require(exhausted.sharedZoneCap==expected,"Wrong difficulty post-spice zone cap");
    worker->setAmountOfSpice(FixPoint(1)/4);
    require(!enemy->campaignPostSpice(),"Fractional/inactive worker cargo ignored");
    worker->setAmountOfSpice(0);

    // New saves keep the original permissions, frozen budget and depletion clock.
    const auto previousVersion=game.loadedSavegameVersion;game.loadedSavegameVersion=SAVEGAMEVERSION;
    OMemoryStream saved;saved.open();enemy->save(saved);
    IMemoryStream input(saved.getData(),saved.getDataLength());
    { QuantBot loaded(input,owner);
        require(loaded.campaignBaselineCaptured==enemy->campaignBaselineCaptured
            && loaded.campaignBaseline.refineries==enemy->campaignBaseline.refineries
            && loaded.campaignBaseline.allowance==enemy->campaignBaseline.allowance,
            "Save/load changed original income budget");
        require(loaded.campaignOriginalStructures==enemy->campaignOriginalStructures,"Save/load changed original building permissions");
        require(loaded.campaignSpiceZeroSince==enemy->campaignSpiceZeroSince,"Save/load reset exhaustion timer");
        require(loaded.campaignCityLimits().sharedZoneCap==exhausted.sharedZoneCap,"Save/load changed active zone cap");
    }
    // The old prefix remains readable; reconstruct authored permissions before top-ups.
    game.loadedSavegameVersion=9841;
    const auto trailerSize=18+4*enemy->campaignOriginalStructures.size();
    IMemoryStream legacyInput(saved.getData(),saved.getDataLength()-trailerSize);
    { QuantBot legacy(legacyInput,owner);
        require(!legacy.campaignBaselineCaptured,"Legacy stream consumed new-format state");
        legacy.noteCampaignOriginalState(true);
        for(auto item:legacy.campaignOriginalStructures) if(!original.count(item)) SDL_Log("LEGACY_EXTRA: %u",item);
        for(auto item:original) if(!legacy.campaignOriginalStructures.count(item)) SDL_Log("LEGACY_MISSING: %u",item);
        require(legacy.campaignOriginalStructures==original,"Legacy save did not recover authored building types");
        require(legacy.campaignBaseline.refineries==originalRefineries,"Legacy save did not recover original refineries");
    }
    game.loadedSavegameVersion=previousVersion;
    spiceTile->setSpice(FixPoint(1)/4);
    const auto resume=game.gameCycleCount+1;
    for(Uint32 cycle=resume;cycle<resume+501;++cycle) tick(cycle);
    require(!enemy->campaignPostSpice() && enemy->campaignCityLimits().sharedZoneCap==before.sharedZoneCap,
        "Restored map spice left extra expansion enabled");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=city_campaign outcome=passed");
    SDL_Log("CITY_CAMPAIGN_PROBE_PASS: original permissions, ally exclusion, fractional spice/cargo, exact depletion delay, restore and save/load");
}
