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

#ifndef NETWORKTRANSPORTTYPES_H
#define NETWORKTRANSPORTTYPES_H

#include <misc/SDL2pp.h>

#include <cstdint>

#ifdef __EMSCRIPTEN__

#include <Network/NetworkPacketBuffer.h>
#include <Network/NetworkPacketView.h>

// Browser builds serialize packets through the ENet-independent streams and
// transmit them over WebRTC DataChannels (see WebRtcTransport).
using NetworkPacketOStream = NetworkPacketBuffer;
using NetworkPacketIStream = NetworkPacketView;

// Mirror of the ENet packet flags used by the existing packet construction
// call sites. The values are irrelevant without ENet (the send path picks the
// DataChannel), but keeping the names lets packet construction code stay
// byte-identical across both transports.
constexpr uint32_t NETWORK_PACKET_FLAG_RELIABLE    = 1;  // == ENET_PACKET_FLAG_RELIABLE
constexpr uint32_t NETWORK_PACKET_FLAG_UNSEQUENCED = 2;  // == ENET_PACKET_FLAG_UNSEQUENCED

#else

#include <Network/ENetPacketOStream.h>
#include <Network/ENetPacketIStream.h>

#include <enet/enet.h>

// Native builds keep the exact ENet packet streams: these aliases make the
// shared packet-construction code transport-neutral without changing any
// byte produced by the native ENet backend.
using NetworkPacketOStream = ENetPacketOStream;
using NetworkPacketIStream = ENetPacketIStream;

#define NETWORK_PACKET_FLAG_RELIABLE    ENET_PACKET_FLAG_RELIABLE
#define NETWORK_PACKET_FLAG_UNSEQUENCED ENET_PACKET_FLAG_UNSEQUENCED

static_assert(ENET_PACKET_FLAG_RELIABLE == 1, "ENET_PACKET_FLAG_RELIABLE must stay 1 (mirrored in the browser build)");
static_assert(ENET_PACKET_FLAG_UNSEQUENCED == 2, "ENET_PACKET_FLAG_UNSEQUENCED must stay 2 (mirrored in the browser build)");

#endif

#endif // NETWORKTRANSPORTTYPES_H
