/*
 *  p2pkit-wasm: ENet-independent growable packet buffer for WebRTC transport.
 *  Extracted from Dune Legacy's browser multiplayer; carries the same Dune
 *  Legacy license (GPLv2) as its origin.
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

#ifndef P2PKIT_WASM_PACKET_H
#define P2PKIT_WASM_PACKET_H

#include <cstdint>
#include <cstring>
#include <list>
#include <set>
#include <string>
#include <vector>

namespace p2pkit_wasm {

/**
    Growable output buffer for one application packet, transport-independent.
    Byte-for-byte compatible with ENet's packet output stream: same
    little-endian primitive encoding, same string framing (uint32 length +
    raw bytes), same container framing. The constructor accepts packet flags
    (e.g. ENet packet flags at ENet call sites) but ignores them; the send
    mode is chosen by the transport that transmits the finished buffer, not
    by the buffer itself.
*/
class PacketBuffer
{
public:
    explicit PacketBuffer(uint32_t flags = 0)
     : currentPos(0) {
        (void) flags;   // flags are transport hints; the transport picks the DataChannel
        buffer.reserve(16);
    }

    void flush()
    {
        ;
    }

    // write operations

    void writeString(const std::string& str)
    {
        ensureBufferSize(currentPos + str.length() + sizeof(uint32_t));

        writeUint32(static_cast<uint32_t>(str.length()));

        if(!str.empty()) {
            memcpy(buffer.data() + currentPos, str.data(), str.length());
            currentPos += str.length();
        }
    }

    void writeUint8(uint8_t x)
    {
        ensureBufferSize(currentPos + sizeof(uint8_t));
        buffer[currentPos] = x;
        currentPos += sizeof(uint8_t);
    }

    void writeUint16(uint16_t x)
    {
        ensureBufferSize(currentPos + sizeof(uint16_t));
        buffer[currentPos + 0] = static_cast<uint8_t>(x & 0xFF);
        buffer[currentPos + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
        currentPos += sizeof(uint16_t);
    }

    void writeUint32(uint32_t x)
    {
        ensureBufferSize(currentPos + sizeof(uint32_t));
        buffer[currentPos + 0] = static_cast<uint8_t>(x & 0xFF);
        buffer[currentPos + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
        buffer[currentPos + 2] = static_cast<uint8_t>((x >> 16) & 0xFF);
        buffer[currentPos + 3] = static_cast<uint8_t>((x >> 24) & 0xFF);
        currentPos += sizeof(uint32_t);
    }

    void writeUint64(uint64_t x)
    {
        ensureBufferSize(currentPos + sizeof(uint64_t));
        for(unsigned int i = 0; i < sizeof(uint64_t); i++) {
            buffer[currentPos + i] = static_cast<uint8_t>((x >> (8 * i)) & 0xFF);
        }
        currentPos += sizeof(uint64_t);
    }

    void writeBool(bool x)
    {
        writeUint8(x == true ? 1 : 0);
    }

    void writeFloat(float x)
    {
        uint32_t tmp;
        memcpy(&tmp, &x, sizeof(uint32_t)); // workaround for a strange optimization in gcc 4.1
        writeUint32(tmp);
    }

    void writeSint8(int8_t x) {
        uint8_t tmp;
        memcpy(&tmp, &x, sizeof(uint8_t));
        writeUint8(tmp);
    }

    void writeSint16(int16_t x) {
        uint16_t tmp;
        memcpy(&tmp, &x, sizeof(uint16_t));
        writeUint16(tmp);
    }

    void writeSint32(int32_t x) {
        uint32_t tmp;
        memcpy(&tmp, &x, sizeof(uint32_t));
        writeUint32(tmp);
    }

    void writeSint64(int64_t x) {
        uint64_t tmp;
        memcpy(&tmp, &x, sizeof(uint64_t));
        writeUint64(tmp);
    }

    /// Writes out up to 8 boolean values into a single byte.
    void writeBools(bool val1 = false, bool val2 = false, bool val3 = false, bool val4 = false,
                    bool val5 = false, bool val6 = false, bool val7 = false, bool val8 = false) {
        const uint8_t val = static_cast<uint8_t>(val1) | (val2 << 1) | (val3 << 2) | (val4 << 3)
                            | (val5 << 4) | (val6 << 5) | (val7 << 6) | (val8 << 7);
        writeUint8(val);
    }

    void writeUint32List(const std::list<uint32_t>& dataList) {
        writeUint32(static_cast<uint32_t>(dataList.size()));
        for(const uint32_t data : dataList) {
            writeUint32(data);
        }
    }

    void writeUint32Vector(const std::vector<uint32_t>& dataVector) {
        writeUint32(static_cast<uint32_t>(dataVector.size()));
        for(const uint32_t data : dataVector) {
            writeUint32(data);
        }
    }

    void writeUint32Set(const std::set<uint32_t>& dataSet) {
        writeUint32(static_cast<uint32_t>(dataSet.size()));
        for(const uint32_t data : dataSet) {
            writeUint32(data);
        }
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

} // namespace p2pkit_wasm

#endif // P2PKIT_WASM_PACKET_H
