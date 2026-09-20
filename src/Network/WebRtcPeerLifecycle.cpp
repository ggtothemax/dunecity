// Browser peer ownership. Lists and connectPeer are aliases of the same allocation.
#include <Network/NetworkManager.h>
#include <algorithm>

#ifdef __EMSCRIPTEN__
NetPeer* NetworkManager::findPeerByWebRtcId(uint32_t webRtcPeerId, bool bCreate) {
    for(NetPeer* pCurrentPeer : peerList) {
        if(pCurrentPeer->webRtcPeerId == webRtcPeerId) {
            return pCurrentPeer;
        }
    }
    for(NetPeer* pAwaitingPeer : awaitingConnectionList) {
        if(pAwaitingPeer->webRtcPeerId == webRtcPeerId) {
            return pAwaitingPeer;
        }
    }
    if(connectPeer != nullptr && connectPeer->webRtcPeerId == webRtcPeerId) {
        return connectPeer;
    }

    if(!bCreate) {
        return nullptr;
    }

    // v1 browser model: exactly two players, so a new transport peer is the
    // single remote player.
    NetPeer* pNewPeer = new NetPeer();
    pNewPeer->webRtcPeerId = webRtcPeerId;
    return pNewPeer;
}

void NetworkManager::disconnectPeer(NetPeer* peer, int cause) {
    if(peer == nullptr) return;
    // SDK disconnect() clears its event queue. Defer our own teardown until
    // packet handling has returned: its payload adapter still borrows PeerData.
    pendingWebRtcDisconnects.emplace(peer->webRtcPeerId, cause);
    if(peer->data) static_cast<PeerData*>(peer->data)->refusals.beginDisconnect();
    pWebRtcTransport->disconnect();
}

void NetworkManager::releaseWebRtcPeer(NetPeer* peer, int cause, bool notify) {
    if(peer == nullptr) return;
    auto* data = static_cast<PeerData*>(peer->data);
    const bool wasHost = peer == connectPeer;
    const bool established = std::find(peerList.begin(), peerList.end(), peer) != peerList.end();
    const std::string name = data ? data->name : std::string();
    // Remove every alias before freeing memory or invoking a reentrant callback.
    peerList.remove(peer);
    awaitingConnectionList.remove(peer);
    pendingWebRtcDisconnects.erase(peer->webRtcPeerId);
    if(wasHost) {
        connectPeer = nullptr;
        connectPeerWebRtcId = 0;
    }
    delete data;
    delete peer;
    if(notify && (wasHost || established) && pOnPeerDisconnected) {
        auto callback = pOnPeerDisconnected;
        callback(name, wasHost, cause);
    }
}

void NetworkManager::clearAllPeers() {
    releaseWebRtcPeer(connectPeer, NETWORKDISCONNECT_QUIT, false);
    while(!peerList.empty()) releaseWebRtcPeer(peerList.front(), NETWORKDISCONNECT_QUIT, false);
    while(!awaitingConnectionList.empty()) releaseWebRtcPeer(awaitingConnectionList.front(), NETWORKDISCONNECT_QUIT, false);
    pendingWebRtcDisconnects.clear();
    connectPeerWebRtcId = 0;
    bWebRtcHost = false;
}

void NetworkManager::drainWebRtcDisconnects() {
    while(!pendingWebRtcDisconnects.empty()) {
        const auto pending = *pendingWebRtcDisconnects.begin();
        pendingWebRtcDisconnects.erase(pendingWebRtcDisconnects.begin());
        releaseWebRtcPeer(findPeerByWebRtcId(pending.first, false), pending.second, true);
    }
}
#endif
