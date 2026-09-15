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

#include <Network/WebRtcTransport.h>

#include <SDL.h>

#include <utility>

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

// JS glue (platform/web/webrtc_glue.js, linked with --js-library)
extern "C" {
int webrtcFindMatch();
int webrtcCancelMatch();
int webrtcSendTo(int peerHandle, int channel, const uint8_t* pData, int length);
int webrtcGetState();
int webrtcGetRttMs();
void webrtcDisconnect();
}

namespace {
// The browser supports exactly one peer connection (v1 two-player model), so a
// single transport instance receives all bridge events.
WebRtcTransport* pActiveTransport = nullptr;
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void webrtcOnEvent(int type, int peerHandle, int channel, int cause, uint8_t* pData, int length) {
    if(pActiveTransport == nullptr) {
        return;
    }

    WebRtcTransport::Event event;
    event.type = static_cast<WebRtcTransport::EventType>(type);
    event.peerHandle = static_cast<uint32_t>(peerHandle);
    event.channel = channel;
    event.cause = cause;
    if(pData != nullptr && length > 0) {
        event.data.assign(pData, pData + length);
    }

    pActiveTransport->enqueueEvent(std::move(event));
}

WebRtcTransport::WebRtcTransport() {
    pActiveTransport = this;
}

WebRtcTransport::~WebRtcTransport() {
    if(pActiveTransport == this) {
        pActiveTransport = nullptr;
    }
    disconnect();
}

bool WebRtcTransport::findMatch() {
    return webrtcFindMatch() != 0;
}

bool WebRtcTransport::cancelMatchmaking() {
    return webrtcCancelMatch() != 0;
}

void WebRtcTransport::disconnect() {
    webrtcDisconnect();
    eventQueue.clear();
}

bool WebRtcTransport::pollEvent(Event& outEvent) {
    if(eventQueue.empty()) {
        return false;
    }

    outEvent = std::move(eventQueue.front());
    eventQueue.pop_front();
    return true;
}

bool WebRtcTransport::sendToPeer(uint32_t peerHandle, int channel, const uint8_t* pData, size_t length) {
    (void) peerHandle;   // single peer in v1; the handle is checked by the caller
    if(pData == nullptr || length == 0 || length > static_cast<size_t>(INT32_MAX)) {
        return false;
    }
    return webrtcSendTo(static_cast<int>(peerHandle), channel, pData, static_cast<int>(length)) != 0;
}

uint32_t WebRtcTransport::getRoundTripTimeMs(uint32_t peerHandle) const {
    (void) peerHandle;
    return static_cast<uint32_t>(webrtcGetRttMs());
}

WebRtcTransport::State WebRtcTransport::getState() const {
    return static_cast<State>(webrtcGetState());
}

void WebRtcTransport::enqueueEvent(Event&& event) {
    // Bound the queue defensively; the game drains it every frame.
    if(eventQueue.size() > 4096) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WebRtcTransport: event queue overflow, dropping oldest event");
        eventQueue.pop_front();
    }
    eventQueue.push_back(std::move(event));
}

#else // !__EMSCRIPTEN__

// Host-build stub: no browser, no WebRTC. The API exists so NetworkManager
// compiles unchanged; on native desktop the ENet backend is authoritative.

#endif
