#include <units/InfantryBase.h>

void runCreditCaptureProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok) throw std::runtime_error(why);};
    House* captor=pLocalHouse;
    House* victim=nullptr;
    for(int i=0;i<NUM_HOUSES;++i) if(auto* candidate=game.getHouse(i))
        if(candidate!=captor && candidate->getTeamID()!=captor->getTeamID()) {victim=candidate;break;}
    require(victim,"No hostile house for storage capture test");
    Coord plot=Coord::Invalid();
    for(int y=2;y+5<currentGameMap->getSizeY() && plot.isInvalid();++y)
        for(int x=2;x+5<currentGameMap->getSizeX() && plot.isInvalid();++x) {
            bool clear=true;
            for(int dy=-1;dy<5 && clear;++dy) for(int dx=-1;dx<5 && clear;++dx) {
                auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAnObject() || tile->hasCityZone() || tile->isRoad()) clear=false;
            }
            if(clear) plot=Coord(x,y);
        }
    require(plot.isValid(),"No plot for storage capture test");
    for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx)
        currentGameMap->getTile(plot.x+dx,plot.y+dy)->setType(Terrain_Rock);
    for(Uint32 type : {Structure_Silo,Structure_Refinery}) {
        for(House* owner : {captor,victim}) {
            owner->capacity=0;owner->storedCredits=0;owner->cityCredits=0;owner->startingCredits=0;
            for(auto* structure:structureList) if(structure->getOwner()==owner)
                owner->capacity+=game.objectData.data[structure->getItemID()][owner->getHouseID()].capacity;
        }
        auto* target=victim->placeStructure(NONE_ID,type,plot.x,plot.y,false,true);
        require(target,"Could not place capture target");
        target->setHealth(1);
        const int oldCapacity=victim->getCapacity();
        const int capturedCapacity=game.objectData.data[type][victim->getHouseID()].capacity;
        victim->storedCredits=FixPoint(oldCapacity)/2;
        victim->cityCredits=FixPoint(oldCapacity)-victim->storedCredits;
        auto* infantry=dynamic_cast<InfantryBase*>(captor->createUnit(Unit_Soldier));
        require(infantry,"Could not create capturing infantry");
        infantry->deploy(plot);infantry->doCaptureStructure(target);
        infantry->justStoppedMoving=true;infantry->checkPos();
        StructureBase* captured=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==captor && structure->getItemID()==type && structure->getLocation()==plot) captured=structure;
        require(captured,"Infantry did not capture storage");
        require(victim->getCapacity()==oldCapacity-capturedCapacity,"Capture removed wrong storage capacity");
        require(victim->getEarnedCredits()==FixPoint(oldCapacity-capturedCapacity),"Capture charged the old owner twice");
        require(captor->getEarnedCredits()==FixPoint(capturedCapacity),"Captured credits were lost or duplicated");
        require(captor->getEarnedCredits()<=captor->getCapacity(),"Captured credits exceeded storage");
        captured->destroy();infantry->destroy();
    }
    SDL_Log("CREDIT_CAPTURE_PASS: silo and refinery transfer mixed holdings without double loss or overflow");
}
