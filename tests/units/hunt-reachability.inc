// Real Hunt selection and path-budget checks across a map-wide mountain ridge.
void runHuntReachabilityProbe(const std::string& mod) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::ostringstream map;
    map<<"[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=64\nSizeY=32\n";
    for(int y=0;y<32;++y) {
        std::string row(64,'%');
        for(int x=24;x<=37;++x)row[x]='@';
        map<<std::setw(3)<<std::setfill('0')<<y<<'='<<row<<'\n';
    }
    map<<"[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
    auto game=std::make_unique<Game>();currentGame=game.get();
    GameInitSettings init("hunt.ini",map.str(),false,settings.gameOptions);
    init.randomSeed=67;
    for(auto house:{HOUSE_ATREIDES,HOUSE_HARKONNEN}) {
        GameInitSettings::HouseInfo slot(house,house==HOUSE_ATREIDES?1:2);
        slot.addPlayerInfo({house==HOUSE_ATREIDES?"Speed tester":"Enemy","HumanPlayer"});
        init.addHouseInfo(slot);
    }
    game->initGame(init);game->initializeGameLoop();
    auto* owner=game->getHouse(HOUSE_ATREIDES);auto* enemy=game->getHouse(HOUSE_HARKONNEN);
    for(int y=0;y<32;++y)for(int x=0;x<64;++x)
        currentGameMap->getTile(x,y)->setExplored(owner->getHouseID(),game->getGameCycleCount());
    auto spawn=[&](House* house,int item,Coord point) {
        auto* unit=house->createUnit(item,true);require(unit,"Hunt fixture cannot create unit");
        unit->deploy(point);unit->setVisible(owner->getTeamID(),true);unit->doSetAttackMode(STOP);return unit;
    };
    auto* hunter=spawn(owner,Unit_Tank,Coord(21,16));
    auto* unreachable=spawn(enemy,Unit_Tank,Coord(39,16));
    auto* reachable=spawn(enemy,Unit_Tank,Coord(2,16));
    hunter->doSetAttackMode(HUNT);
    require(!currentGameMap->terrainAttackReachable(*hunter,*unreachable),"Mountain ridge accepted a disconnected target");
    require(hunter->findTarget()==reachable,"Hunt preferred an unreachable target over a reachable one");
    const unsigned builds=currentGameMap->terrainConnectivityBuilds;
    for(int i=0;i<1000;++i) {
        currentGameMap->incrementPathingRevision(); // Traffic must not invalidate terrain labels.
        require(!currentGameMap->terrainAttackReachable(*hunter,*unreachable),"Cached reachability changed with traffic");
    }
    require(currentGameMap->terrainConnectivityBuilds==builds,"Movement rebuilt the terrain connectivity cache");
    auto grid=std::move(game->spatialGrid);
    require(hunter->findTarget()==reachable,"Legacy Hunt ignored terrain reachability");
    game->spatialGrid=std::move(grid);
    // An already acquired target crossing the ridge is released without an A* search.
    hunter->doAttackObject(unreachable,false);
    hunter->setDestination(unreachable->getLocation());
    const auto path= hunter->resolvePendingPathRequest();
    require(!hunter->getTarget() && path.nodesExpanded==0,"Unreachable hunt retained its target or consumed A* nodes");
    // No reachable enemies: stop acquiring the same impossible destination.
    reachable->setVisible(owner->getTeamID(),false);
    require(!hunter->findTarget(),"Hunt reacquired an unreachable enemy");
    hunter->pendingTargetRequest=UnitBase::TargetRequestKind::Acquire;
    hunter->resolvePendingTargetRequest();
    require(hunter->getAttackMode()==GUARD,"Unreachable Hunt kept repeating a whole-map target search");
    // Infantry climb the same ridge; nearby enemies across a narrow ridge can be shot.
    auto* infantry=spawn(owner,Unit_Soldier,Coord(20,17));
    require(currentGameMap->terrainAttackReachable(*infantry,*unreachable),"Vehicle reachability blocked infantry");
    auto* near=spawn(enemy,Unit_Soldier,Coord(25,16));
    require(currentGameMap->terrainAttackReachable(*hunter,*near),"Ridge filter rejected a target within weapon range");
    near->cancelDeployment();
    // Open a remote gap. The same target becomes reachable, then unreachable when closed.
    for(int x=24;x<=37;++x)currentGameMap->getTile(x,1)->setType(Terrain_Rock);
    require(currentGameMap->terrainAttackReachable(*hunter,*unreachable),"New mountain gap did not invalidate connectivity");
    require(currentGameMap->terrainConnectivityBuilds==builds+1,"A batch of terrain edits caused more than one rebuild");
    hunter->doSetAttackMode(HUNT);
    require(hunter->findTarget()==unreachable,"Hunt failed to use a reachable detour around the ridge");
    for(int x=24;x<=37;++x)currentGameMap->getTile(x,1)->setType(Terrain_Mountain);
    require(!currentGameMap->terrainAttackReachable(*hunter,*unreachable),"Closing a gap left stale connectivity");
    // A target can die between queuing and processing a deferred path request.
    hunter->doSetAttackMode(HUNT);
    hunter->target.pointTo(Uint32(NONE_ID-1));
    hunter->setDestination(Coord(39,16));
    const auto stalePath=hunter->resolvePendingPathRequest();
    require(!hunter->getTarget() && stalePath.nodesExpanded==0,"Expired Hunt target consumed a path search");
    // Explicit attack orders retain their transport/manual intent.
    hunter->doAttackObject(unreachable,true);hunter->engageTarget();
    require(hunter->getTarget()==unreachable,"Terrain filtering replaced an explicit attack order");
    SDL_Log("HUNT_REACHABILITY_PASS: %s reachable alternatives, no-route rejection, infantry/range, cached traffic and terrain invalidation",mod.c_str());
    game.reset();currentGame=nullptr;
}
