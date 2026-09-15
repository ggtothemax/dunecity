/*
 *  p2pkit-wasm: ENet-independent read view over received packet bytes.
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

#ifndef P2PKIT_WASM_PACKET_VIEW_H
#define P2PKIT_WASM_PACKET_VIEW_H

#include <cstdint>
#include <cstring>
#include <list>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace p2pkit_wasm {

/// Thrown by PacketView reads that run past the end of the buffer, unless a
/// custom EofFactory is supplied (games with their own exception hierarchy
/// inject it so existing catch sites keep working).
class EofError : public std::runtime_error {
public:
    explicit EofError(const std::string& str) noexcept : std::runtime_error(str) { }
};

/// A EofFactory provides the exception a PacketView throws on truncated
/// input. `raise()` must never return.
struct DefaultEofFactory {
    [[noreturn]] static void raise(const char* what) { throw EofError(what); }
};

/**
    Non-owning view over received packet bytes, transport-independent.
    Byte-for-byte compatible with ENet's packet input stream (little-endian
    primitives, uint32-length-prefixed strings, counted containers). The view
    does not own the memory: the caller must keep the underlying buffer alive
    while the view is used.

    \tparam EofFactory  exception hook; see DefaultEofFactory. Games whose
                        existing code catches a specific eof type pass a
                        factory throwing that type.
*/
template<typename EofFactory = DefaultEofFactory>
class PacketView
{
public:
    PacketView(const uint8_t* pData, size_t dataLength)
     : currentPos(0), pData(pData), dataLength(dataLength) {
        ;
    }

    size_t getRemainingLength() const { return dataLength - currentPos; }

    std::string readString()
    {
        const uint32_t length = readUint32();

        if(static_cast<size_t>(currentPos) + length > dataLength) {
            EofFactory::raise("PacketView::readString(): End-of-File reached!");
        }

        std::string resultString(reinterpret_cast<const char*>(pData + currentPos), length);
        currentPos += length;
        return resultString;
    }

    uint8_t readUint8()
    {
        if(currentPos + sizeof(uint8_t) > dataLength) {
            EofFactory::raise("PacketView::readUint8(): End-of-File reached!");
        }

        uint8_t tmp;
        memcpy(&tmp, pData + currentPos, sizeof(uint8_t));
        currentPos += sizeof(uint8_t);
        return tmp;
    }

    uint16_t readUint16()
    {
        if(currentPos + sizeof(uint16_t) > dataLength) {
            EofFactory::raise("PacketView::readUint16(): End-of-File reached!");
        }

        const uint16_t tmp = static_cast<uint16_t>(pData[currentPos + 0])
                             | (static_cast<uint16_t>(pData[currentPos + 1]) << 8);
        currentPos += sizeof(uint16_t);
        return tmp;
    }

    uint32_t readUint32()
    {
        if(currentPos + sizeof(uint32_t) > dataLength) {
            EofFactory::raise("PacketView::readUint32(): End-of-File reached!");
        }

        const uint32_t tmp = static_cast<uint32_t>(pData[currentPos + 0])
                             | (static_cast<uint32_t>(pData[currentPos + 1]) << 8)
                             | (static_cast<uint32_t>(pData[currentPos + 2]) << 16)
                             | (static_cast<uint32_t>(pData[currentPos + 3]) << 24);
        currentPos += sizeof(uint32_t);
        return tmp;
    }

    uint64_t readUint64()
    {
        if(currentPos + sizeof(uint64_t) > dataLength) {
            EofFactory::raise("PacketView::readUint64(): End-of-File reached!");
        }

        uint64_t tmp = 0;
        for(unsigned int i = 0; i < sizeof(uint64_t); i++) {
            tmp |= static_cast<uint64_t>(pData[currentPos + i]) << (8 * i);
        }
        currentPos += sizeof(uint64_t);
        return tmp;
    }

    bool readBool()
    {
        return (readUint8() == 1 ? true : false);
    }

    float readFloat()
    {
        const uint32_t tmp = readUint32();
        float tmp2;
        memcpy(&tmp2, &tmp, sizeof(uint32_t)); // workaround for a strange optimization in gcc 4.1
        return tmp2;
    }

    int8_t readSint8() {
        uint8_t tmp = readUint8();
        return static_cast<int8_t>(tmp);
    }

    int16_t readSint16() {
        uint16_t tmp = readUint16();
        return static_cast<int16_t>(tmp);
    }

    int32_t readSint32() {
        uint32_t tmp = readUint32();
        return static_cast<int32_t>(tmp);
    }

    int64_t readSint64() {
        uint64_t tmp = readUint64();
        return static_cast<int64_t>(tmp);
    }

    /// Reads in up to 8 boolean values from a single byte.
    void readBools(bool* pVal1 = nullptr, bool* pVal2 = nullptr, bool* pVal3 = nullptr, bool* pVal4 = nullptr,
                   bool* pVal5 = nullptr, bool* pVal6 = nullptr, bool* pVal7 = nullptr, bool* pVal8 = nullptr) {
        const uint8_t val = readUint8();

        if(pVal1 != nullptr)   *pVal1 = ((val & 0x01) != 0);
        if(pVal2 != nullptr)   *pVal2 = ((val & 0x02) != 0);
        if(pVal3 != nullptr)   *pVal3 = ((val & 0x04) != 0);
        if(pVal4 != nullptr)   *pVal4 = ((val & 0x08) != 0);
        if(pVal5 != nullptr)   *pVal5 = ((val & 0x10) != 0);
        if(pVal6 != nullptr)   *pVal6 = ((val & 0x20) != 0);
        if(pVal7 != nullptr)   *pVal7 = ((val & 0x40) != 0);
        if(pVal8 != nullptr)   *pVal8 = ((val & 0x80) != 0);
    }

    /**
        Throws (via EofFactory) if elementCount elements of elementSize bytes
        cannot possibly fit into what is left of this view.
    */
    void requireReadableElements(size_t elementCount, size_t elementSize) const {
        if(elementSize == 0 || elementCount > getRemainingLength() / elementSize) {
            EofFactory::raise("PacketView: declared element count exceeds the remaining stream length!");
        }
    }

    std::list<uint32_t> readUint32List() {
        std::list<uint32_t> List;
        const uint32_t size = readUint32();
        requireReadableElements(size, sizeof(uint32_t));
        for(uint32_t i = 0; i < size; i++) {
            List.push_back(readUint32());
        }
        return List;
    }

    std::vector<uint32_t> readUint32Vector() {
        std::vector<uint32_t> vec;
        const uint32_t size = readUint32();
        requireReadableElements(size, sizeof(uint32_t));
        for(uint32_t i = 0; i < size; i++) {
            vec.push_back(readUint32());
        }
        return vec;
    }

    std::set<uint32_t> readUint32Set() {
        std::set<uint32_t> retSet;
        const uint32_t size = readUint32();
        requireReadableElements(size, sizeof(uint32_t));
        for(uint32_t i = 0; i < size; i++) {
            retSet.insert(readUint32());
        }
        return retSet;
    }

private:
    size_t              currentPos;
    const uint8_t*      pData;
    size_t              dataLength;
};

} // namespace p2pkit_wasm

#endif // P2PKIT_WASM_PACKET_VIEW_H
