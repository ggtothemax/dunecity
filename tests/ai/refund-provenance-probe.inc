// Real engine fixture; only compiled by the diagnostic driver.
//
// Exercises where a construction refund goes. Credits paid out of the exempt
// starting cash must return to that pool — before the first refinery the
// earned store has no capacity at all, so routing them through it silently
// destroyed everything already spent on a cancelled order. The earned share
// stays capacity-limited, the absolute 999999 ceiling still holds, and no path
// may turn earned income into exempt starting cash.
//
// Everything below runs through the shipped entry points: BuilderBase
// production and cancellation, prerequisite removal, completion, owner change
// and House::payCredits/returnCredits.
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>
#include <structures/StructureBase.h>
#include <Map.h>
#include <Tile.h>

void runRefundProvenanceProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("REFUND_PROVENANCE_FAIL: %s",why);throw std::runtime_error(why);}};
    House* house = pLocalHouse ? pLocalHouse : game.getHouse(HOUSE_HARKONNEN);
    require(house!=nullptr,"Missing measured house");

    // Keep the registered players alive but out of House::update, so nothing
    // earns or spends between the assertions.
    auto savedPlayers=std::move(house->players);
    struct RestorePlayers{House* house; decltype(savedPlayers)& players;
        ~RestorePlayers(){house->players=std::move(players);}} restore{house,savedPlayers};

    auto fixture=[&](int capacity,FixPoint stored,FixPoint city,FixPoint starting){
        house->capacity=capacity;
        house->storedCredits=stored; house->cityCredits=city; house->startingCredits=starting;
        house->powerUsageTimer=MILLI2CYCLES(15*1000);
        house->oldCredits=house->getCredits();
    };

    // A yard of our own to produce from, on ground cleared for it.
    Coord plot=Coord::Invalid();
    for(int y=2;y+6<currentGameMap->getSizeY() && plot.isInvalid();++y)
        for(int x=2;x+6<currentGameMap->getSizeX() && plot.isInvalid();++x) {
            bool clear=true;
            for(int dy=-1;dy<=4 && clear;++dy)
                for(int dx=-1;dx<=4 && clear;++dx) {
                    const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                    if(!tile || tile->hasAnObject() || tile->hasCityZone() || tile->isRoad()) clear=false;
                }
            if(clear) plot=Coord(x,y);
        }
    require(plot.isValid(),"No clear plot for the refund fixture on this map");
    for(int dy=0;dy<4;++dy)
        for(int dx=0;dx<4;++dx) {
            auto* tile=currentGameMap->getTile(plot.x+dx,plot.y+dy);
            tile->setType(Terrain_Rock); tile->setRoad(false); tile->setOwner(house->getHouseID());
        }
    auto* yard=dynamic_cast<BuilderBase*>(house->placeStructure(NONE_ID,Structure_ConstructionYard,
        plot.x,plot.y,false,true));
    require(yard!=nullptr,"Could not place the producing yard");
    yard->updateBuildList();

    // Whatever this mod's yard can actually build; a structure, so no unit
    // limit can stall the order.
    Uint32 item=ItemID_Invalid;
    Uint32 itemPrice=0;
    for(const BuildItem& entry : yard->getBuildList())
        if(isStructure(entry.itemID) && entry.price>itemPrice && entry.itemID!=Structure_Slab1
            && entry.itemID!=Structure_Slab4 && entry.itemID!=Structure_Road) {
            item=entry.itemID; itemPrice=entry.price;
        }
    require(item!=ItemID_Invalid,"The yard offers nothing to produce");
    // Stop well short of the price, so every measurement is of an unfinished
    // order rather than one waiting to be placed.
    const FixPoint partialOrder=std::min(FixPoint(100),FixPoint(int(itemPrice))/2);

    // Pay into the current order through the real production path until it has
    // spent at least `wanted`, without ever finishing the item.
    auto produceUpTo=[&](FixPoint wanted){
        yard->doProduceItem(item);
        for(int pass=0; pass<20000 && yard->getProductionProgress()<wanted; ++pass) {
            const FixPoint before=yard->getProductionProgress();
            yard->updateProductionProgress();
            if(yard->getProductionProgress()==before) break;   // out of money
        }
    };
    auto cancel=[&]{ yard->doCancelItem(item); };

    // --- 1. The opening: no refinery, so no earned storage at all. Cancelling
    //        must hand the starting cash back instead of destroying it.
    fixture(0,0,0,1000);
    produceUpTo(partialOrder);
    const FixPoint spent=yard->getProductionProgress();
    require(spent>0,"The order did not consume any credits");
    require(yard->getProductionProgressFromStartingCredits()==spent,
            "Starting cash paid for the order but was not recorded as its source");
    require(house->getStartingCredits()==FixPoint(1000)-spent,
            "Production did not come out of the starting cash");
    require(house->getEarnedCredits()==0,"Production created earned credits");
    cancel();
    require(house->getStartingCredits()==FixPoint(1000),
            "Cancelling an opening order destroyed the starting cash it had spent");
    require(house->getEarnedCredits()==0,
            "An opening refund was laundered into the capacity-limited earned pool");
    require(house->getCredits()==1000,"Refunded credits reappeared outside both pools");
    require(yard->getProductionProgressFromStartingCredits()==0,
            "The refunded order kept a claim on the starting cash");

    // --- 2. Earned money stays earned: a refund never mints exempt cash.
    fixture(1000,500,0,1000);
    produceUpTo(partialOrder);
    require(yard->getProductionProgressFromStartingCredits()==0,
            "Earned credits paid for the order but were recorded as starting cash");
    const FixPoint earnedSpent=yard->getProductionProgress();
    require(house->getStoredCredits()==FixPoint(500)-earnedSpent,
            "Production did not consume the earned credits first");
    cancel();
    require(house->getStartingCredits()==FixPoint(1000),
            "Refunding an earned-funded order topped up the exempt starting cash");
    require(house->getStoredCredits()==FixPoint(500),"The earned refund did not return to the store");

    // --- 3. The earned share is still capacity-limited. Losing the storage
    //        while the order runs destroys that part of the refund, and the
    //        starting pool must not absorb it.
    fixture(1000,500,0,1000);
    produceUpTo(partialOrder);
    house->capacity=0;
    house->enforceCreditCapacity();
    cancel();
    require(house->getEarnedCredits()==0,"A refund was stored past a lost capacity");
    require(house->getStartingCredits()==FixPoint(1000),
            "An unstorable refund was moved into the exempt starting cash");

    // --- 4. Mixed and fractional payments return exactly what each pool paid.
    fixture(1000,FixPoint(1)/4,0,FixPoint(41)/2);
    produceUpTo(1000);   // more than the house owns: drains both pools
    const FixPoint fromStarting=yard->getProductionProgressFromStartingCredits();
    require(fromStarting>0 && fromStarting<yard->getProductionProgress(),
            "A mixed payment was attributed to a single pool");
    cancel();
    require(house->getStartingCredits()==FixPoint(41)/2,
            "A fractional starting-cash payment was not refunded exactly");
    require(house->getStoredCredits()==FixPoint(1)/4,
            "A fractional earned payment was not refunded exactly");

    // --- 5. The absolute ceiling survives a refund.
    fixture(1000,0,0,FixPoint(House::MAX_GAME_CREDITS));
    produceUpTo(partialOrder);
    house->startingCredits=FixPoint(House::MAX_GAME_CREDITS);   // as if income arrived meanwhile
    cancel();
    require(house->getCredits()<=House::MAX_GAME_CREDITS,
            "A refund pushed the house past the absolute credit ceiling");

    // --- 6. Losing a prerequisite cancels the order the same way.
    fixture(0,0,0,1000);
    produceUpTo(partialOrder);
    {
        auto iter=yard->buildList.begin();
        while(iter!=yard->buildList.end() && iter->itemID!=item) ++iter;
        require(iter!=yard->buildList.end(),"The produced item left the build list early");
        yard->removeItem(yard->buildList,iter,item);
    }
    require(house->getStartingCredits()==FixPoint(1000),
            "Losing a prerequisite destroyed the starting cash spent on the order");
    yard->updateBuildList();

    // The complete builder stream (not just a standalone queue item) retains
    // a partial payment and can refund it after loading.
    fixture(0,0,0,1000);
    produceUpTo(partialOrder);
    {
        const auto previousVersion=game.loadedSavegameVersion;
        game.loadedSavegameVersion=SAVEGAMEVERSION;
        OMemoryStream out;out.open();yard->save(out);
        const auto savedProgress=yard->getProductionProgress();
        const auto savedStarting=yard->getProductionProgressFromStartingCredits();
        yard->removeBuiltItemFromProductionQueue(); // discard the fixture's duplicate claim
        IMemoryStream in(out.getData(),static_cast<int>(out.getDataLength()));
        {
            ConstructionYard restored(in);
            require(restored.getProductionProgress()==savedProgress
                && restored.getProductionProgressFromStartingCredits()==savedStarting,
                "Builder save/load lost the partial payment provenance");
            restored.doCancelItem(item);
            require(house->getStartingCredits()==1000,
                "Loaded construction cancellation lost its starting-cash refund");
        }
        game.loadedSavegameVersion=previousVersion;
    }

    // --- 7. Completion spends the money for good: no phantom refund afterwards.
    fixture(0,0,0,1000);
    produceUpTo(partialOrder);
    const FixPoint afterCompletion=house->getStartingCredits();
    yard->removeBuiltItemFromProductionQueue();
    require(yard->getProductionProgressFromStartingCredits()==0,
            "A completed item kept its claim on the starting cash");
    yard->refundProductionProgress();
    require(house->getStartingCredits()==afterCompletion,
            "A completed item refunded starting cash it had already consumed");

    // --- 8. Capture: the new owner inherits the progress, never the previous
    //        owner's exemption.
    House* other=nullptr;
    for(int i=0;i<NUM_HOUSES;++i) if(auto* candidate=game.getHouse(i)) if(candidate!=house) {other=candidate;break;}
    if(other!=nullptr) {
        fixture(0,0,0,1000);
        produceUpTo(partialOrder);
        const FixPoint otherStartingBefore=other->getStartingCredits();
        const FixPoint otherStored=other->storedCredits, otherCity=other->cityCredits;
        const int otherCapacity=other->capacity;
        other->capacity=0;
        yard->currentProductionQueue.front().startingCreditsPaid=20;
        yard->setOwner(other);
        require(yard->currentProductionQueue.front().startingCreditsPaid==0,
                "Captured prepaid order retained another owner's exemption");
        require(yard->getProductionProgressFromStartingCredits()==0,
                "Captured progress kept a claim on the new owner's starting cash");
        yard->refundProductionProgress();
        require(other->getStartingCredits()==otherStartingBefore,
                "Capturing a producing builder minted starting cash for the captor");
        other->capacity=otherCapacity;
        other->storedCredits=otherStored; other->cityCredits=otherCity;
        yard->setOwner(house);
    }

    // --- 9. Save format: the provenance of a pre-paid order (Starport, Love
    //        Factory) survives a round trip once the save version carries it,
    //        and defaults to "earned" on saves that predate it.
    {
        ProductionQueueItem saved(Unit_Trike,400,FixPoint(75)/2);
        OMemoryStream out;
        out.open();
        saved.save(out);
        const auto previousVersion=game.loadedSavegameVersion;
        game.loadedSavegameVersion=SAVEGAMEVERSION;
        IMemoryStream in(out.getData(),static_cast<int>(out.getDataLength()));
        ProductionQueueItem loaded;
        loaded.load(in);
        game.loadedSavegameVersion=previousVersion;
        require(loaded.itemID==Unit_Trike && loaded.price==400,"The queued order did not survive the round trip");
        const bool carried = SAVEGAMEVERSION >= 9846;
        require(loaded.startingCreditsPaid==(carried ? FixPoint(75)/2 : FixPoint(0)),
                carried ? "A saved order lost which pool paid for it"
                        : "An order claimed starting cash the save format cannot carry");
    }

    {
        OMemoryStream old;old.open();old.writeUint32(Unit_Trike);old.writeUint32(400);old.writeUint32(12345);
        IMemoryStream in(old.getData(),static_cast<int>(old.getDataLength()));
        const auto previousVersion=game.loadedSavegameVersion;game.loadedSavegameVersion=9845;
        ProductionQueueItem loaded;loaded.load(in);game.loadedSavegameVersion=previousVersion;
        require(loaded.startingCreditsPaid==0 && in.readUint32()==12345,
                "Legacy queue loading invented provenance or consumed the next field");
    }

    yard->destroy();
    SDL_Log("REFUND_PROVENANCE_PASS: refunds return to the pool that paid, without laundering or loss");
}
