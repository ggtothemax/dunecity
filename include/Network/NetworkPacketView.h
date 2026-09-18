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

#ifndef NETWORKPACKETVIEW_H
#define NETWORKPACKETVIEW_H

#include <misc/InputStream.h>
#include <misc/exceptions.h>

#include <p2pkit-wasm/packet_view.h>

#include <cstdint>
#include <string>

/**
    Eof factory injected into the p2pkit SDK's PacketView so truncated reads
    keep throwing the game's own exception type (InputStream::eof, built by the
    THROW macro); existing catch sites are unchanged.
*/
struct NetworkPacketEofFactory
{
    [[noreturn]] static void raise(const char* what) {
        THROW(InputStream::eof, "%s", what);
    }
};

/**
    ENet-independent non-owning view over received packet bytes, used by the
    browser (WebRTC) transport. The read logic is owned by the p2pkit
    Emscripten SDK (p2pkit_wasm::PacketView, installed from the p2pkit
    dependency pinned in platform/web/package.json); this class is the thin
    Dune-side adaptation to the game's InputStream interface and exception
    contract. Byte-for-byte compatible with ENetPacketIStream. The view does
    not own the memory: the caller must keep the underlying buffer alive while
    the view is used (the WebRTC transport queues owned copies and hands each
    queue entry to handlePacket exactly once).
*/
class NetworkPacketView : public InputStream
{
public:
    NetworkPacketView(const uint8_t* pData, size_t dataLength)
     : view(pData, dataLength) {
        ;
    }

    size_t getRemainingLength() const override
    {
        return view.getRemainingLength();
    }

    std::string readString() override
    {
        return view.readString();
    }

    Uint8 readUint8() override
    {
        return view.readUint8();
    }

    Uint16 readUint16() override
    {
        return view.readUint16();
    }

    Uint32 readUint32() override
    {
        return view.readUint32();
    }

    Uint64 readUint64() override
    {
        return view.readUint64();
    }

    bool readBool() override
    {
        return view.readBool();
    }

    float readFloat() override
    {
        return view.readFloat();
    }

private:
    p2pkit_wasm::PacketView<NetworkPacketEofFactory> view;
};

#endif // NETWORKPACKETVIEW_H
