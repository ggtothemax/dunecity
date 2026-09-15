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

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/**
    ENet-independent growable packet buffer used by the browser (WebRTC) transport.
    Byte-for-byte compatible with ENetPacketOStream: same little-endian primitive
    encoding, same string framing (uint32 length + raw bytes), same container
    framing inherited from OutputStream. The constructor accepts the ENet packet
    flags used at the existing call sites but ignores them; the send mode is chosen
    by the transport that transmits the finished buffer, not by the buffer itself.
*/
class NetworkPacketBuffer : public OutputStream
{
public:
    explicit NetworkPacketBuffer(uint32_t flags = 0)
     : currentPos(0) {
        (void) flags;   // ENet packet flags are meaningless without ENet; kept for call-site compatibility
        buffer.reserve(16);
    }

    void flush() override
    {
        ;
    }

    // write operations

    void writeString(const std::string& str) override
    {
        ensureBufferSize(currentPos + str.length() + sizeof(Uint32));

        writeUint32(static_cast<Uint32>(str.length()));

        if(!str.empty()) {
            memcpy(buffer.data() + currentPos, str.data(), str.length());
            currentPos += str.length();
        }
    }

    void writeUint8(Uint8 x) override
    {
        ensureBufferSize(currentPos + sizeof(Uint8));
        buffer[currentPos] = x;
        currentPos += sizeof(Uint8);
    }

    void writeUint16(Uint16 x) override
    {
        ensureBufferSize(currentPos + sizeof(Uint16));
        const Uint16 tmp = SDL_SwapLE16(x);
        memcpy(buffer.data() + currentPos, &tmp, sizeof(Uint16));
        currentPos += sizeof(Uint16);
    }

    void writeUint32(Uint32 x) override
    {
        ensureBufferSize(currentPos + sizeof(Uint32));
        const Uint32 tmp = SDL_SwapLE32(x);
        memcpy(buffer.data() + currentPos, &tmp, sizeof(Uint32));
        currentPos += sizeof(Uint32);
    }

    void writeUint64(Uint64 x) override
    {
        ensureBufferSize(currentPos + sizeof(Uint64));
        const Uint64 tmp = SDL_SwapLE64(x);
        memcpy(buffer.data() + currentPos, &tmp, sizeof(Uint64));
        currentPos += sizeof(Uint64);
    }

    void writeBool(bool x) override
    {
        writeUint8(x == true ? 1 : 0);
    }

    void writeFloat(float x) override
    {
        Uint32 tmp;
        memcpy(&tmp, &x, sizeof(Uint32)); // workaround for a strange optimization in gcc 4.1
        writeUint32(tmp);
    }

    void ensureBufferSize(size_t minBufferSize) {
        if(minBufferSize < buffer.size()) {
            return;
        }

        size_t newBufferSize = ((buffer.size() * 3) / 2);
        if(newBufferSize < minBufferSize) {
            newBufferSize = minBufferSize;
        }

        buffer.resize(newBufferSize, 0);
    }

    /**
        \return pointer to the written bytes (invalidated by further writes)
    */
    const uint8_t* getData() const { return buffer.data(); }

    /**
        \return number of bytes written so far
    */
    size_t getDataLength() const { return currentPos; }

    /**
        \return the written bytes as an owned copy
    */
    std::vector<uint8_t> takeBytes() const { return std::vector<uint8_t>(buffer.begin(), buffer.begin() + currentPos); }

private:
    size_t  currentPos;
    std::vector<uint8_t> buffer;
};

#endif // NETWORKPACKETBUFFER_H
