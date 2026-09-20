// Exercise the production browser ownership methods without starting game services.
// Only construction and the JS socket ABI are fixtures; lifecycle code is linked unchanged.
#include <Network/NetworkManager.h>
#include <GameInitSettings.h>
#include <cassert>
#include <cstdio>

extern "C" {
int webrtcFindMatch() { return 1; }
int webrtcCancelMatch() { return 1; }
int webrtcSendTo(int, int, const uint8_t*, int) { return 1; }
int webrtcGetState() { return 0; }
int webrtcGetRttMs() { return 0; }
void webrtcDisconnect() { }
}
NetworkManager::NetworkManager(int, const std::string&) {
    pWebRtcTransport = std::make_unique<WebRtcTransport>();
}
NetworkManager::~NetworkManager() { clearAllPeers(); }
// No game settings are constructed by this isolated peer fixture.
GameInitSettings::~GameInitSettings() = default;

int main() {
    using PeerData = NetworkManager::PeerData;
    auto add = [](NetworkManager& manager, bool admitted, bool client) {
        auto* peer = manager.findPeerByWebRtcId(1, true);
        auto* data = new PeerData(peer, admitted ? PeerData::PeerState::Connected : PeerData::PeerState::WaitingForName);
        data->name = "opponent";
        peer->data = data;
        (admitted ? manager.peerList : manager.awaitingConnectionList).push_back(peer);
        if(client) { manager.connectPeer = peer; manager.connectPeerWebRtcId = 1; }
        return peer;
    };
    auto empty = [](NetworkManager& manager) {
        assert(manager.peerList.empty());
        assert(manager.awaitingConnectionList.empty());
        assert(manager.connectPeer == nullptr);
        assert(manager.connectPeerWebRtcId == 0);
        assert(manager.pendingWebRtcDisconnects.empty());
    };
    for(bool admitted : {false, true}) for(bool client : {false, true}) {
        NetworkManager manager(0, "");
        add(manager, admitted, client);
        manager.clearAllPeers();
        empty(manager);
        manager.clearAllPeers(); // idempotent Back, then destructor
        add(manager, admitted, client); // reconnect then destructor
    }
    {
        NetworkManager manager(0, "");
        // The client host alias may temporarily be outside either list.
        auto* peer = manager.findPeerByWebRtcId(7, true);
        peer->data = new PeerData(peer, PeerData::PeerState::WaitingForConnect);
        manager.connectPeer = peer;
        manager.clearAllPeers();
        empty(manager);
    }
    for(bool admitted : {false, true}) for(bool client : {false, true}) {
        NetworkManager manager(0, "");
        auto* peer = add(manager, admitted, client);
        int callbacks = 0;
        manager.setOnPeerDisconnected([&](const std::string& name, bool host, int cause) {
            ++callbacks;
            assert(name == "opponent" && host == client && cause == NETWORKDISCONNECT_TIMEOUT);
            empty(manager); // aliases are gone before callback
            manager.clearAllPeers(); // reentrant teardown
        });
        manager.disconnectPeer(peer, NETWORKDISCONNECT_TIMEOUT);
        manager.disconnectPeer(peer, NETWORKDISCONNECT_TIMEOUT);
        // Packet adapters still borrow this data until update finishes.
        assert(static_cast<PeerData*>(peer->data)->name == "opponent");
        assert(static_cast<PeerData*>(peer->data)->refusals.isDisconnecting());
        manager.drainWebRtcDisconnects();
        empty(manager);
        assert(callbacks == (admitted || client ? 1 : 0));
        manager.drainWebRtcDisconnects();
    }
    {
        NetworkManager manager(0, "");
        auto* peer = add(manager, true, true);
        manager.disconnectPeer(peer, NETWORKDISCONNECT_TIMEOUT);
        manager.releaseWebRtcPeer(peer, NETWORKDISCONNECT_QUIT, false);
        manager.drainWebRtcDisconnects(); // remote event won the race
        empty(manager);
        manager.pWebRtcTransport.reset(); // room sessions have no matchmaking transport
        assert(manager.getWebRtcState() == WebRtcTransport::State::Idle);
    }
    std::puts("WebRTC peer lifecycle: PASS (teardown, reconnect, rejection, event races, reentrancy)");
}
