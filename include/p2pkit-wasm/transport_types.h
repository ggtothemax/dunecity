/*
 *  p2pkit-wasm: packet stream aliases and flag constants for the WebRTC
 *  transport. Extracted from Dune Legacy's browser multiplayer; carries the
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

#ifndef P2PKIT_WASM_TRANSPORT_TYPES_H
#define P2PKIT_WASM_TRANSPORT_TYPES_H

#include <p2pkit-wasm/packet.h>
#include <p2pkit-wasm/packet_view.h>

#include <cstdint>

namespace p2pkit_wasm {

// Browser builds serialize packets through these ENet-independent streams and
// transmit them over WebRTC DataChannels (see webrtc_transport.h).
using PacketOStream = PacketBuffer;
using PacketIStream = PacketView<>;

// Mirror of the ENet packet flags used by packet construction call sites in
// ENet-based games. The values are irrelevant without ENet (the send path
// picks the DataChannel), but keeping the names lets shared packet-construction
// code stay byte-identical across both transports.
constexpr uint32_t PACKET_FLAG_RELIABLE    = 1;  // == ENET_PACKET_FLAG_RELIABLE
constexpr uint32_t PACKET_FLAG_UNSEQUENCED = 2;  // == ENET_PACKET_FLAG_UNSEQUENCED

static_assert(PACKET_FLAG_RELIABLE == 1, "PACKET_FLAG_RELIABLE must stay 1 (mirrors ENET_PACKET_FLAG_RELIABLE)");
static_assert(PACKET_FLAG_UNSEQUENCED == 2, "PACKET_FLAG_UNSEQUENCED must stay 2 (mirrors ENET_PACKET_FLAG_UNSEQUENCED)");

} // namespace p2pkit_wasm

#endif // P2PKIT_WASM_TRANSPORT_TYPES_H
