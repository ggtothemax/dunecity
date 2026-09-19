#include <Network/NetworkManager.h>
#include <Network/GameInitSettingsPolicy.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <algorithm>
#include <limits>

namespace {
constexpr Uint32 prepareAck=std::numeric_limits<Uint32>::max();
constexpr Uint32 maxSnapshot=5u*1024*1024;
constexpr Uint32 chunkSize=48u*1024;
}

bool NetworkManager::sendJoinSync(Uint32 operation, Uint32 offset, const std::string& data, Uint32 recipient) {
    if(!isDirectSession()) return false;
    ENetPacketOStream packet(ENET_PACKET_FLAG_RELIABLE);
    packet.writeUint32(bIsServer ? NETWORKPACKET_JOIN_SYNC : NETWORKPACKET_JOIN_ACK);
    packet.writeUint32(operation); packet.writeUint32(joinTransaction); packet.writeUint32(offset); packet.writeString(data);
    return sendPacketOverRelay(packet,0,bIsServer ? recipient : relayHostPeerId());
}

bool NetworkManager::beginLateJoin(const std::string& request, const std::string& name, const GameInitSettings& snapshot, bool spectator) {
    auto* direct=getDirectTransport();
    if(!direct || !bIsServer || !bGameInProgress || lateJoinPaused() || !direct->allowsLateJoin()) return false;
    std::string error;
    if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(snapshot,error)) { joinStatus=error; return false; }
    joinAsSpectator=spectator;
    joinSpectators=spectators;
    // Retired observers must not accumulate across repeated visits.
    for(auto it=joinSpectators.begin();it!=joinSpectators.end();) {
        const bool present=*it==playerName || std::any_of(direct->peers().begin(),direct->peers().end(),[&](const auto& p){return p.name==*it;});
        if(!present) it=joinSpectators.erase(it); else ++it;
    }
    if(spectator) joinSpectators.insert(name);
    else joinSpectators.erase(name); // A seated spectator may become a controller.
    Uint32 promotingPeer=0;
    for(const auto& peer : direct->peers()) if(peer.spectator && peer.name==name) promotingPeer=peer.id;
    for(const auto& house : snapshot.getHouseInfoList()) for(const auto& p : house.playerInfoList) {
        if(joinSpectators.count(p.playerName)) { joinStatus="That name already belongs to a game controller."; return false; }
    }
    OMemoryStream stream; stream.open();
    stream.writeUint32(static_cast<Uint32>(joinSpectators.size()));
    for(const auto& observer : joinSpectators) stream.writeString(observer);
    snapshot.save(stream);
    if(stream.getDataLength()>maxSnapshot) { joinStatus="This game is too large to synchronize."; return false; }
    joinBytes.assign(stream.getData(),stream.getDataLength());
    joinSnapshot=std::make_unique<GameInitSettings>(snapshot);
    joinRequestId=request; joinName=name;
    joinTransaction=std::max(simulationSeed,joinTransaction)+1; if(joinTransaction==0) joinTransaction=1;
    joinAcks.clear(); joinOriginalPeers.clear();
    for(const auto& peer : direct->peers()) if(!peer.spectator) joinOriginalPeers.push_back(peer.id);
    joinStage=JoinStage::Preparing; joinDeadline=SDL_GetTicks()+120000;
    if(promotingPeer) {
        // Stop only this viewer's stream. Existing players are already entering the
        // usual player-admission barrier; unrelated spectators never join it.
        observerTransfers.erase(promotingPeer);
        if(!sendJoinSync(20,0,name,promotingPeer)) { abortLateJoin("Could not reach the spectator."); return false; }
    }
    joinStatus="Pausing to add "+name+"...";
    if(!joinOriginalPeers.empty() && !sendJoinSync(1,static_cast<Uint32>(joinBytes.size()),name)) {
        abortLateJoin("Could not pause all players."); return false;
    }
    return true;
}

void NetworkManager::receiveJoinSync(Uint32 peer, Uint32 operation, Uint32 transaction, Uint32 offset, const std::string& data) {
    auto* direct=getDirectTransport(); if(!direct) return;
    if(operation==20) {
        if(!bIsServer && peer==relayHostPeerId() && data==playerName && direct->prepareSpectatorPromotion()) {
            joinExpected=true; observerIncoming.clear();
            joinStatus="The host accepted your request. Preparing your player slot...";
        }
        return;
    }
    if(operation>=10) { receiveObserverPacket(peer,operation,transaction,offset,data); return; }
    if(direct->isSpectating() || direct->isSpectatorPeer(peer)) return;
    if(bIsServer) {
        if(operation!=0 || transaction!=joinTransaction || !lateJoinPaused() || !direct->findPeer(peer)) return;
        if((joinStage==JoinStage::Preparing && offset==prepareAck)
           || (joinStage==JoinStage::Sending && offset==joinNextOffset)) joinAcks[peer]=offset;
        return;
    }
    if(operation==1) {
        if(lateJoinPaused() || (!bGameInProgress && !joinExpected) || offset==0 || offset>maxSnapshot) return;
        joinTransaction=transaction; joinName=data; joinBytes.clear(); joinTotal=offset;
        joinDeadline=SDL_GetTicks()+120000; joinStage=JoinStage::Receiving;
        joinStatus="Synchronizing the game for "+data+"...";
        if(bGameInProgress && !joinExpected && !direct->openJoinWindow(data)) { abortLateJoin("Could not prepare the game connection."); return; }
        bGameInProgress=false;
        sendJoinSync(0,prepareAck,{});
        return;
    }
    if(transaction!=joinTransaction || !lateJoinPaused()) return;
    if(operation==3) { abortLateJoin("The host cancelled the join request."); return; }
    if(operation!=2 || joinStage!=JoinStage::Receiving || offset!=joinBytes.size()
       || data.empty() || data.size()>chunkSize || data.size()>joinTotal-joinBytes.size()) {
        abortLateJoin("The saved game transfer was incomplete."); return;
    }
    joinBytes+=data;
    if(joinBytes.size()==joinTotal) {
        try {
            IMemoryStream stream(joinBytes.data(),static_cast<int>(joinBytes.size()));
            const auto count=stream.readUint32();
            if(count>RoomRelay::Limits::kMaxPeersPerRoom) throw std::runtime_error("Too many spectators.");
            std::set<std::string> observers;
            for(Uint32 i=0;i<count;++i) {
                const auto name=stream.readString();
                if(!RoomRelay::isAcceptableDisplayName(name) || !observers.insert(name).second) throw std::runtime_error("Invalid spectator.");
            }
            auto next=std::make_unique<GameInitSettings>(stream);
            for(const auto& house : next->getHouseInfoList()) for(const auto& p : house.playerInfoList)
                if(observers.count(p.playerName)) throw std::runtime_error("Spectator also controls a house.");
            joinSpectators=std::move(observers);
            std::string error;
            if(stream.getRemainingLength()!=0 || !GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(*next,error)
               || (next->getGameType()!=GameType::LoadMultiplayer && next->getGameType()!=GameType::LoadCoop))
                throw std::runtime_error("Invalid game snapshot.");
            joinSnapshot=std::move(next);
        } catch(const std::exception&) { abortLateJoin("The saved game could not be read."); return; }
    }
    sendJoinSync(0,static_cast<Uint32>(joinBytes.size()),{});
}

void NetworkManager::updateLateJoin() {
    auto* direct=getDirectTransport();
    if(!direct || direct->isSpectating() || !lateJoinPaused() || lateJoinReady()) return;
    if(SDL_TICKS_PASSED(SDL_GetTicks(),joinDeadline)) { abortLateJoin("The join request timed out. Your game can continue."); return; }
    if(!bIsServer) return;
    if(joinStage==JoinStage::Preparing) {
        for(auto id : joinOriginalPeers) if(joinAcks[id]!=prepareAck) return;
        if(!direct->openJoinWindow(joinName) || !direct->manageJoin(joinAsSpectator ? "approve_spectator" : "approve",joinRequestId)) {
            abortLateJoin("The join request could not be approved."); return;
        }
        bGameInProgress=false; joinStage=JoinStage::Admission; return;
    }
    if(joinStage==JoinStage::Admission) {
        if(direct->joinDecisionPending()) return;
        if(!direct->joinDecisionSucceeded()) { abortLateJoin("The game service could not approve the request."); return; }
        joinStage=JoinStage::Connecting; joinStatus="Connecting "+joinName+" to every player...";
    }
    if(joinStage==JoinStage::Connecting) {
        if(!direct->meshReady() || static_cast<std::size_t>(std::count_if(direct->peers().begin(),direct->peers().end(),[](const auto& p){return !p.spectator;}))!=joinOriginalPeers.size()+1) return;
        const auto found=std::find_if(direct->peers().begin(),direct->peers().end(),[&](const auto& p){return p.name==joinName;});
        if(found==direct->peers().end()) return;
        if(!sendJoinSync(1,static_cast<Uint32>(joinBytes.size()),joinName,found->id)) { abortLateJoin("Could not reach the new player."); return; }
        joinOffset=joinNextOffset=0; joinAcks.clear();
        joinStage=JoinStage::Sending;
    }
    if(joinStage==JoinStage::Sending) {
        if(joinNextOffset!=joinOffset) {
            for(const auto& p : direct->peers()) if(!p.spectator && joinAcks[p.id]!=joinNextOffset) return;
            joinOffset=joinNextOffset;
        }
        if(joinOffset==joinBytes.size()) {
            joinStage=JoinStage::Starting; joinStatus="All players synchronized. Resuming...";
            if(!sendStartGame(0)) abortLateJoin("The players could not confirm the new roster.");
            return;
        }
        const auto data=joinBytes.substr(joinOffset,chunkSize);
        joinNextOffset=joinOffset+static_cast<Uint32>(data.size()); joinAcks.clear();
        if(!sendJoinSync(2,joinOffset,data)) { abortLateJoin("Could not send the saved game."); return; }
        joinStatus="Synchronizing game: "+std::to_string(100u*joinOffset/joinBytes.size())+"%";
    }
}

void NetworkManager::abortLateJoin(const std::string& reason) {
    if(!lateJoinPaused()) return;
    if(bIsServer) {
        sendJoinSync(3,0,{});
        if(auto* direct=getDirectTransport()) direct->manageJoin("abort",joinRequestId);
    }
    if(auto* direct=getDirectTransport()) direct->abortJoinWindow();
    joinStage=JoinStage::Idle; joinStatus=reason; joinBytes.clear(); joinSnapshot.reset();
    bGameInProgress=!joinExpected;
    if(joinExpected && pOnPeerDisconnected) pOnPeerDisconnected({},true,NETWORKDISCONNECT_TIMEOUT);
}

std::unique_ptr<GameInitSettings> NetworkManager::takeLateJoin() {
    if(!lateJoinReady()) return {};
    auto next=std::move(joinSnapshot);
    spectators=joinSpectators;
    if(auto* direct=getDirectTransport()) direct->setSpectators(spectators);
    resumeSeed=joinTransaction; joinLoading=true; joinExpected=false;
    joinStage=JoinStage::Idle; joinBytes.clear(); joinName.clear();
    if(auto* direct=getDirectTransport()) direct->completeJoinWindow();
    return next;
}
