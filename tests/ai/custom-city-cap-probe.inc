// Real growth-loop regression. Included only by the native diagnostic harness.
void runCustomCityCapProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get())) bot=candidate;
    require(bot,"City cap fixture needs a QuantBot");
    game.citySimEnabled_=true;
    if(!game.citySimulation_) {
        game.citySimulation_=std::make_unique<DuneCity::CitySimulation>();
        game.citySimulation_->init(currentGameMap->getSizeX(),currentGameMap->getSizeY());
    }
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->supportMode=false;
    bot->difficulty=QuantBot::Difficulty::Easy;
    const int area=currentGameMap->getSizeX()*currentGameMap->getSizeY();
    require(bot->getCityPopulationLimit(area)==0,"Shared human house inherited city ceiling");
    std::list<std::unique_ptr<Player>> humanPlayers;
    for(auto it=house->players.begin();it!=house->players.end();) {
        auto current=it++;
        if(dynamic_cast<HumanPlayer*>(current->get())) humanPlayers.splice(humanPlayers.end(),house->players,current);
    }
    const int cap=bot->getCityPopulationLimit(area);
    require(cap>0,"Easy has no city ceiling");
    bot->difficulty=QuantBot::Difficulty::Brutal;
    require(bot->getCityPopulationLimit(area)==0,"Brutal has a city ceiling");
    bot->difficulty=QuantBot::Difficulty::Easy;
    game.gameType=GameType::Campaign;
    require(bot->getCityPopulationLimit(area)==0,"Campaign helper inherited custom city ceiling");
    game.gameType=GameType::CustomGame;
    auto place=[&]() -> ZoneStructure* {
        for(int y=1;y+3<currentGameMap->getSizeY();y+=3)
            for(int x=1;x+3<currentGameMap->getSizeX();x+=3) {
                bool clear=true;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
                if(clear) return dynamic_cast<ZoneStructure*>(house->placeStructure(
                    NONE_ID,Structure_ZoneResidential,x,y,true,true));
            }
        return nullptr;
    };
    // Existing oversize cities are preserved, but cannot continue growing.
    for(int i=0;i<cap/800+4;++i) {
        auto* zone=place();require(zone,"No mature city fixture site");zone->setResidentialPopulation(40);
    }
    auto* vacant=place();require(vacant,"No vacant fixture site");vacant->setResidentialPopulation(0);
    auto* sim=game.getCitySimulation();
    auto ready=[&]() {
        house->producedPower=100000;
        auto& state=sim->houseState_[house->getHouseID()];
        state.resValve=2000;state.comValve=1500;state.indValve=1500;
        state.hasStadium=true;state.hasAirport=true;state.hasStarport=true;
    };
    ready();sim->runZoneGrowth();
    require(vacant->getResidentialPopulation()==0,"Easy city grew above population limit");
    bot->difficulty=QuantBot::Difficulty::Brutal;
    ready();sim->runZoneGrowth();
    require(vacant->getResidentialPopulation()>0,"Brutal growth was capped or fixture cannot grow");
    house->players.splice(house->players.end(),humanPlayers);
    SDL_Log("CUSTOM_CITY_CAP_PROBE_PASS: Easy oversize growth blocked, Brutal grows, campaign excluded");
}
