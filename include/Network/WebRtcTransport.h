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

#include <p2pkit-wasm/webrtc_transport.h>

/**
    The browser WebRTC transport is owned by the p2pkit Emscripten SDK:
    p2pkit_wasm::WebRtcTransport (header-only, installed from the p2pkit
    dependency pinned in platform/web/package.json) wraps the browser bridge —
    the SDK's --js-library glue (node_modules/p2pkit/emscripten/js/
    p2pkit_webrtc_glue.cjs) linked next to DuneCity's own adapter
    (platform/web/dunecity_webrtc_config.js) in src/CMakeLists.txt. Only usable
    in Emscripten builds; native desktop builds compile the same API as an
    inert stub so NetworkManager stays buildable everywhere. See
    docs/webrtc/IMPLEMENTATION-PLAN.md for the channel mapping:
      channel 0 = control DataChannel  { ordered: true }
      channel 1 = commands DataChannel { ordered: false, maxRetransmits: 0 }
    DuneCity keeps this historical global alias so existing call sites (and the
    State/EventType/MatchRole/Event names they use) are unchanged.
*/
using WebRtcTransport = p2pkit_wasm::WebRtcTransport;

#endif // WEBRTCTRANSPORT_H
