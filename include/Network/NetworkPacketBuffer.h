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

#ifndef NETWORKPACKETBUFFER_H
#define NETWORKPACKETBUFFER_H

#include <misc/OutputStream.h>

#include <p2pkit-wasm/packet.h>

#include <cstdint>
#include <vector>

/**
    ENet-independent growable packet buffer used by the browser (WebRTC)
    transport. The byte encoding is owned by the p2pkit Emscripten SDK
    (p2pkit_wasm::PacketBuffer, installed from the p2pkit dependency pinned in
    platform/web/package.json); this class is the thin Dune-side adaptation to
    the game's OutputStream interface. Byte-for-byte compatible with
    ENetPacketOStream: same little-endian primitive encoding, same string
    framing (uint32 length + raw bytes), same container framing inherited from
    OutputStream. The constructor accepts the ENet packet flags used at the
    existing call sites but ignores them; the send mode is chosen by the
    transport that transmits the finished buffer, not by the buffer itself.
*/
class NetworkPacketBuffer : public OutputStream
{
public:
    explicit NetworkPacketBuffer(uint32_t flags = 0)
     : buffer(flags) {
    }

    void flush() override
    {
        buffer.flush();
    }

    // write operations

    void writeString(const std::string& str) override
    {
        buffer.writeString(str);
    }

    void writeUint8(Uint8 x) override
    {
        buffer.writeUint8(x);
    }

    void writeUint16(Uint16 x) override
    {
        buffer.writeUint16(x);
    }

    void writeUint32(Uint32 x) override
    {
        buffer.writeUint32(x);
    }

    void writeUint64(Uint64 x) override
    {
        buffer.writeUint64(x);
    }

    void writeBool(bool x) override
    {
        buffer.writeBool(x);
    }

    void writeFloat(float x) override
    {
        buffer.writeFloat(x);
    }

    /**
        \return pointer to the written bytes (invalidated by further writes)
    */
    const uint8_t* getData() const { return buffer.getData(); }

    /**
        \return number of bytes written so far
    */
    size_t getDataLength() const { return buffer.getDataLength(); }

    /**
        \return the written bytes as an owned copy
    */
    std::vector<uint8_t> takeBytes() const { return buffer.takeBytes(); }

    /**
        Grows the backing storage to at least minBufferSize bytes. Forwards to
        the SDK's public PacketBuffer::ensureBufferSize; exposed so callers
        that pre-reserve (as the ENet stream did) keep a public seam.
    */
    void ensureBufferSize(size_t minBufferSize) { buffer.ensureBufferSize(minBufferSize); }

private:
    p2pkit_wasm::PacketBuffer buffer;
};

#endif // NETWORKPACKETBUFFER_H
