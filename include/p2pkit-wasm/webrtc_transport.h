/*
 *  p2pkit-wasm: generic WebRTC transport for C++/Emscripten games.
 *  Extracted from Dune Legacy's browser multiplayer transport; carries the
 *  same Dune Legacy license (GPLv2) as its origin.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This file is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef P2PKIT_WASM_WEBRTCTRANSPORT_H
#define P2PKIT_WASM_WEBRTCTRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

/**
    Thin C++ wrapper around a browser WebRTC bridge (js/p2pkit_webrtc_glue.js,
    linked into the page as an Emscripten --js-library by the consuming game).
    Only usable in Emscripten builds; native desktop builds compile the same
    API as an inert stub so game networking code stays buildable everywhere.

    Channel mapping (defaults; labels and options are overridable through the
    glue config object, see js/p2pkit_webrtc_glue.js and README.md):
      channel 0 = "control"  DataChannel { ordered: true }
                  — reliable; queued under backpressure, flushed in order
      channel 1 = "commands" DataChannel { ordered: false, maxRetransmits: 0 }
                  — lossy; sends are dropped (reported as failures) under
                    backpressure so the game can resend recent cycles
*/
namespace p2pkit_wasm {

class WebRtcTransport {
public:
    enum class State {
        Idle = 0,
        Connecting = 1,
        Connected = 2,
        Failed = 3
    };

    enum class EventType {
        Connect = 0,     // both data channels open; peer is now a valid handle
        Disconnect = 1,  // peer went away; cause is the game's disconnect code
        Message = 2,     // one application packet arrived on a data channel
        State = 3,       // transport-level state change (see getState())
        Matched = 4      // the lobby paired us; cause is the assigned MatchRole
    };

    enum class MatchRole {
        Host = 0,    // we were the first finder: we create the offer
        Joiner = 1   // we were the second finder: we answer the offer
    };

    struct Event {
        EventType type = EventType::State;
        uint32_t peerHandle = 0;
        int channel = 0;
        int cause = 0;
        std::vector<uint8_t> data;
    };

#ifdef __EMSCRIPTEN__

    WebRtcTransport() {
        detail::activeTransport = this;
    }
    ~WebRtcTransport() {
        if(detail::activeTransport == this) {
            detail::activeTransport = nullptr;
        }
        disconnect();
    }

    WebRtcTransport(const WebRtcTransport&) = delete;
    WebRtcTransport& operator=(const WebRtcTransport&) = delete;

    /// Enter the global matchmaking lobby. The lobby pairs the next two
    /// finders and assigns the roles; pairing is reported as a Matched event.
    /// Returns false if a match is already in progress.
    bool findMatch() {
        return webrtcFindMatch() != 0;
    }

    /// Leave the matchmaking queue (only meaningful before pairing).
    bool cancelMatchmaking() {
        return webrtcCancelMatch() != 0;
    }

    /// Tear down signaling + peer connection. Queued events are dropped.
    void disconnect() {
        webrtcDisconnect();
        eventQueue.clear();
    }

    /// Pop one queued event; returns false when the queue is empty.
    bool pollEvent(Event& outEvent) {
        if(eventQueue.empty()) {
            return false;
        }

        outEvent = std::move(eventQueue.front());
        eventQueue.pop_front();
        return true;
    }

    /// Send one application packet on the given channel (0 control, 1 commands).
    /// Returns false if the message was dropped (commands channel backpressure).
    bool sendToPeer(uint32_t peerHandle, int channel, const uint8_t* pData, size_t length) {
        (void) peerHandle;   // single peer in v1; the handle is checked by the caller
        if(pData == nullptr || length == 0 || length > static_cast<size_t>(INT32_MAX)) {
            return false;
        }
        return webrtcSendTo(static_cast<int>(peerHandle), channel, pData, static_cast<int>(length)) != 0;
    }

    /// Last known RTT estimate for the peer (ms); 0 when not connected.
    uint32_t getRoundTripTimeMs(uint32_t peerHandle) const {
        (void) peerHandle;
        return static_cast<uint32_t>(webrtcGetRttMs());
    }

    State getState() const {
        return static_cast<State>(webrtcGetState());
    }

    /// Called (single-threaded, from the JS event loop) by the bridge shim.
    void enqueueEvent(Event&& event) {
        // Bound the queue defensively; the game drains it every frame.
        if(eventQueue.size() > 4096) {
            std::fprintf(stderr, "p2pkit-wasm: WebRtcTransport event queue overflow, dropping oldest event\n");
            eventQueue.pop_front();
        }
        eventQueue.push_back(std::move(event));
    }

private:
    std::deque<Event> eventQueue;

#else
    // Host-build stub: the browser transport does not exist on native desktop.
    bool findMatch() { return false; }
    bool cancelMatchmaking() { return false; }
    void disconnect() { }
    bool pollEvent(Event&) { return false; }
    bool sendToPeer(uint32_t, int, const uint8_t*, size_t) { return false; }
    uint32_t getRoundTripTimeMs(uint32_t) const { return 0; }
    State getState() const { return State::Idle; }
    void enqueueEvent(Event&&) { }
#endif
};

namespace detail {
// The browser supports exactly one peer connection (v1 two-player model), so a
// single transport instance receives all bridge events.
inline WebRtcTransport* activeTransport = nullptr;
} // namespace detail

} // namespace p2pkit_wasm

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include <utility>

// JS bridge (js/p2pkit_webrtc_glue.js + the consuming game's --js-library
// adapter), linked with --js-library.
extern "C" {
int webrtcFindMatch();
int webrtcCancelMatch();
int webrtcSendTo(int peerHandle, int channel, const uint8_t* pData, int length);
int webrtcGetState();
int webrtcGetRttMs();
void webrtcDisconnect();
}

extern "C" inline EMSCRIPTEN_KEEPALIVE void webrtcOnEvent(int type, int peerHandle, int channel, int cause, uint8_t* pData, int length) {
    if(p2pkit_wasm::detail::activeTransport == nullptr) {
        return;
    }

    p2pkit_wasm::WebRtcTransport::Event event;
    event.type = static_cast<p2pkit_wasm::WebRtcTransport::EventType>(type);
    event.peerHandle = static_cast<uint32_t>(peerHandle);
    event.channel = channel;
    event.cause = cause;
    if(pData != nullptr && length > 0) {
        event.data.assign(pData, pData + length);
    }

    p2pkit_wasm::detail::activeTransport->enqueueEvent(std::move(event));
}

#endif // __EMSCRIPTEN__

#endif // P2PKIT_WASM_WEBRTCTRANSPORT_H
