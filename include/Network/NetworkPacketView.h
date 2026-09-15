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

#include <cstdint>
#include <cstring>
#include <string>

/**
    ENet-independent non-owning view over received packet bytes, used by the
    browser (WebRTC) transport. Byte-for-byte compatible with ENetPacketIStream.
    The view does not own the memory: the caller must keep the underlying buffer
    alive while the view is used (the WebRTC transport queues owned copies and
    hands each queue entry to handlePacket exactly once).
*/
class NetworkPacketView : public InputStream
{
public:
    NetworkPacketView(const uint8_t* pData, size_t dataLength)
     : currentPos(0), pData(pData), dataLength(dataLength) {
        ;
    }

    std::string readString() override
    {
        const Uint32 length = readUint32();

        if(static_cast<size_t>(currentPos) + length > dataLength) {
            THROW(InputStream::eof, "NetworkPacketView::readString(): End-of-File reached!");
        }

        std::string resultString(reinterpret_cast<const char*>(pData + currentPos), length);
        currentPos += length;
        return resultString;
    }

    Uint8 readUint8() override
    {
        if(currentPos + sizeof(Uint8) > dataLength) {
            THROW(InputStream::eof, "NetworkPacketView::readUint8(): End-of-File reached!");
        }

        Uint8 tmp;
        memcpy(&tmp, pData + currentPos, sizeof(Uint8));
        currentPos += sizeof(Uint8);
        return tmp;
    }

    Uint16 readUint16() override
    {
        if(currentPos + sizeof(Uint16) > dataLength) {
            THROW(InputStream::eof, "NetworkPacketView::readUint16(): End-of-File reached!");
        }

        Uint16 tmp;
        memcpy(&tmp, pData + currentPos, sizeof(Uint16));
        currentPos += sizeof(Uint16);
        return SDL_SwapLE16(tmp);
    }

    Uint32 readUint32() override
    {
        if(currentPos + sizeof(Uint32) > dataLength) {
            THROW(InputStream::eof, "NetworkPacketView::readUint32(): End-of-File reached!");
        }

        Uint32 tmp;
        memcpy(&tmp, pData + currentPos, sizeof(Uint32));
        currentPos += sizeof(Uint32);
        return SDL_SwapLE32(tmp);
    }

    Uint64 readUint64() override
    {
        if(currentPos + sizeof(Uint64) > dataLength) {
            THROW(InputStream::eof, "NetworkPacketView::readUint64(): End-of-File reached!");
        }

        Uint64 tmp;
        memcpy(&tmp, pData + currentPos, sizeof(Uint64));
        currentPos += sizeof(Uint64);
        return SDL_SwapLE64(tmp);
    }

    bool readBool() override
    {
        return (readUint8() == 1 ? true : false);
    }

    float readFloat() override
    {
        const Uint32 tmp = readUint32();
        float tmp2;
        memcpy(&tmp2, &tmp, sizeof(Uint32)); // workaround for a strange optimization in gcc 4.1
        return tmp2;
    }

private:
    size_t              currentPos;
    const uint8_t*      pData;
    size_t              dataLength;
};

#endif // NETWORKPACKETVIEW_H
