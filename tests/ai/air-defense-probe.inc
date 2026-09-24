// Real target selection with a tempting raid and simultaneous home/worker threat.
void runAirDefenseProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot && enemy,"Air defence fixture missing houses or bot");
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->supportMode=false;
    bot->manualUnitOrders.clear();
    Coord origin=Coord::Invalid();
    for(int y=2;y+24<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+24<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<24;++dy)for(int dx=0;dx<24;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Air defence fixture needs clear space");
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);require(unit,"Air fixture unit failed");
        unit->deploy(origin+offset);unit->setVisible(owner->getTeamID(),true);return unit;
    };
    auto* air=spawn(owner,Unit_Ornithopter,Coord(8,8));
    auto* worker=spawn(owner,Unit_Harvester,Coord(19,19));
    auto* attacker=spawn(enemy,Unit_Tank,Coord(3,7));
    auto* launcher=spawn(enemy,Unit_Launcher,Coord(22,2));
    auto* base=owner->placeStructure(NONE_ID,Structure_ConstructionYard,origin.x+2,origin.y+2,true,true);
    auto* raid=enemy->placeStructure(NONE_ID,Structure_ConstructionYard,origin.x+10,origin.y+6,true,true);
    require(base && raid,"Air fixture buildings failed");
    raid->setVisible(owner->getTeamID(),true);
    auto savedUnits=unitList;auto savedStructures=structureList;
    unitList.clear();structureList.clear();
    unitList.push_back(air);unitList.push_back(worker);unitList.push_back(attacker);
    structureList.push_back(base);structureList.push_back(raid);
    try {
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                              QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            const auto& config=getQuantBotConfig();
            auto settings=config.getSettings(static_cast<int>(difficulty));
            settings.ornithopterAttackEnabled=true;
            attacker->setLocation(origin+Coord(3,7));attacker->doAttackObject(base,true);
            bot->doSetAttackMode(air,STOP);bot->doAttackObject(air,raid,true);
            bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==attacker,"Aircraft kept raiding while its construction yard was attacked");
            attacker->setLocation(origin+Coord(18,19));attacker->doAttackObject(worker,true);
            bot->doAttackObject(air,raid,true);
            bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==attacker,"Aircraft ignored an attacked harvester outside its base");
            settings.ornithopterAttackEnabled=false;
            bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==attacker,"Disabling offensive raids disabled defensive interception");
            settings.ornithopterAttackEnabled=true;
            attacker->setHealth(0);bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==raid,"Aircraft did not resume raiding after the threat died");
            attacker->setHealth(attacker->getMaxHealth());
        }
        const auto& config=getQuantBotConfig();auto settings=config.hard;
        settings.ornithopterAttackEnabled=true;
        // A real attack overrides raid safety, even under an anti-air escort.
        launcher->setLocation(attacker->getLocation());unitList.push_back(launcher);
        bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==attacker,"Anti-air escort suppressed an active harvester rescue");
        attacker->doSetAttackMode(STOP);
        launcher->doAttackObject(worker,true);
        bot->doAttackObject(air,raid,true);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==launcher,"Aircraft refused to intercept a launcher attacking its harvester");
        // Exercise the damage callback, not just the standalone target chooser.
        bot->doAttackObject(air,raid,true);bot->defenceResponseCycles.clear();
        bot->onDamage(worker,1,launcher->getObjectID());
        require(air->getTarget()==launcher,"Harvester damage did not immediately interrupt the air raid");
        // Nearby threats without an actual attack still obey normal raid safety.
        launcher->doSetAttackMode(STOP);
        bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()!=attacker && air->getTarget()!=launcher,
                "Proximity alone bypassed anti-air safety");
        launcher->setLocation(origin+Coord(3,7));launcher->doAttackObject(base,true);
        bot->doAttackObject(air,raid,true);bot->defenceResponseCycles.clear();
        bot->onDamage(base,1,launcher->getObjectID());
        require(air->getTarget()==launcher,"Base damage did not recall aircraft from a raid");
        launcher->setHealth(0);attacker->setHealth(0);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==raid,"Aircraft failed to resume raids after the emergency ended");

        // Damaged aircraft keep fighting. Nothing repairs an ornithopter —
        // UnitBase::doRepair is empty and only ground units drive to a repair
        // yard — so reserving one for repair parks it for the rest of the match.
        attacker->setHealth(attacker->getMaxHealth());
        launcher->setHealth(launcher->getMaxHealth());
        launcher->setLocation(origin+Coord(3,7));launcher->doAttackObject(base,true);
        air->setHealth(air->getMaxHealth()/8);
        require(air->isBadlyDamaged(),"Damaged-air fixture is not badly damaged");
        require(!bot->reserveDamagedUnitForRepair(air),
                "An unrepairable damaged aircraft was still reserved for repair");
        bot->doSetAttackMode(air,STOP);bot->doAttackObject(air,raid,true);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==launcher,"A damaged ornithopter refused to defend its base");

        // A live interception is stable. Repeated damage callbacks and a moving
        // attacker must not restart the attack run: the reported match had 149
        // target switches in five minutes, 144 of them while the previous
        // target was still alive.
        const ObjectBase* held=air->getTarget();
        attacker->setLocation(origin+Coord(2,3));attacker->doAttackObject(base,true);
        for(int volley=0;volley<6;++volley) {
            bot->defenceResponseCycles.clear();
            launcher->setLocation(origin+Coord(3+volley%3,7+volley%2));
            launcher->doAttackObject(base,true);
            bot->onDamage(base,1,volley%2 ? attacker->getObjectID() : launcher->getObjectID());
            require(air->getTarget()==held,"Aircraft re-chose its target on a later volley");
        }
        // A worker attacked out in the field does not pull aircraft off the base.
        attacker->setLocation(origin+Coord(18,19));attacker->doAttackObject(worker,true);
        bot->defenceResponseCycles.clear();
        bot->onDamage(worker,1,attacker->getObjectID());
        require(air->getTarget()==launcher,"A remote harvester rescue outranked an attack on the base");
        // With the base safe the same wing does rescue the worker, and a fresh
        // attack on the base then takes it back.
        launcher->doSetAttackMode(STOP);launcher->setHealth(0);
        bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==attacker,"Aircraft ignored the remaining harvester attacker");
        launcher->setHealth(launcher->getMaxHealth());
        launcher->setLocation(origin+Coord(3,7));launcher->doAttackObject(base,true);
        bot->defenceResponseCycles.clear();
        bot->onDamage(base,1,launcher->getObjectID());
        require(air->getTarget()==launcher,"An attack on the base did not outrank a rescue under way");
        air->setHealth(air->getMaxHealth());
        launcher->setHealth(0);attacker->setHealth(0);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==raid,"Aircraft did not resume raiding after the stable interception ended");

        unitList.remove(launcher);
        bot->onHumanUnitOrder(air->getObjectID());bot->doAttackObject(air,raid,true);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==raid,"Defensive automation replaced a human aircraft order");
    } catch(...) {unitList=savedUnits;structureList=savedStructures;throw;}
    unitList=savedUnits;structureList=savedStructures;
    SDL_Log("AIR_DEFENSE_PASS: base/harvester attacks preempt raids and AA avoidance; damaged air still attacks,"
        " interceptions stay stable across volleys, base outranks remote rescue;"
        " proximity safety, resumption and human control preserved");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=air_defense outcome=passed");
}
