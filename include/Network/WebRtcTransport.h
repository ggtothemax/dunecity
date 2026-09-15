/*
 *  This file is part of Dune Legacy.
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WEBRTCTRANSPORT_H
#define WEBRTCTRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/**
    Thin C++ wrapper around the browser WebRTC bridge
    (platform/web/webrtc_glue.js). Only usable in Emscripten builds; native
    desktop builds compile the same API as an inert stub so NetworkManager
    stays buildable everywhere. See docs/webrtc/IMPLEMENTATION-PLAN.md for the
    channel mapping:
      channel 0 = control DataChannel  { ordered: true }
      channel 1 = commands DataChannel { ordered: false, maxRetransmits: 0 }
*/
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
        Disconnect = 1,  // peer went away; cause is a NETWORKDISCONNECT_* code
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
    WebRtcTransport();
    ~WebRtcTransport();

    WebRtcTransport(const WebRtcTransport&) = delete;
    WebRtcTransport& operator=(const WebRtcTransport&) = delete;

    /// Enter the global matchmaking lobby. The lobby pairs the next two
    /// finders and assigns the roles; pairing is reported as a Matched event.
    /// Returns false if a match is already in progress.
    bool findMatch();

    /// Leave the matchmaking queue (only meaningful before pairing).
    bool cancelMatchmaking();

    /// Tear down signaling + peer connection. Queued events are dropped.
    void disconnect();

    /// Pop one queued event; returns false when the queue is empty.
    bool pollEvent(Event& outEvent);

    /// Send one application packet on the given channel (0 control, 1 commands).
    /// Returns false if the message was dropped (commands channel backpressure).
    bool sendToPeer(uint32_t peerHandle, int channel, const uint8_t* pData, size_t length);

    /// Last known RTT estimate for the peer (ms); 0 when not connected.
    uint32_t getRoundTripTimeMs(uint32_t peerHandle) const;

    State getState() const;

    /// Called (single-threaded, from the JS event loop) by the bridge shim.
    void enqueueEvent(Event&& event);

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

#endif // WEBRTCTRANSPORT_H
