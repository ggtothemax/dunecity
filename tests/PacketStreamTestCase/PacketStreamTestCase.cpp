/*
 *  PacketStreamTestCase.cpp - Byte-compatibility tests for the ENet-independent
 *  packet streams used by the browser (WebRTC) transport.
 *
 *  Proves that NetworkPacketBuffer/NetworkPacketView produce and consume exactly
 *  the same bytes as the existing ENet packet streams for representative
 *  primitive, string, and container encodings, and for real game payloads
 *  (Command/CommandList and representative network packets).
 */

#include <catch2/catch_all.hpp>

#include <Network/ENetPacketOStream.h>
#include <Network/ENetPacketIStream.h>
#include <Network/NetworkPacketBuffer.h>
#include <Network/NetworkPacketView.h>
#include <Network/NetworkManager.h>
#include <Command.h>   // header-only: CMDTYPE enum for the command list payload

#include <enet/enet.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

// Serialize the same payload through ENetPacketOStream and return the bytes.
template<typename F>
std::vector<uint8_t> bytesViaEnet(F&& writePayload) {
    ENetPacketOStream stream(ENET_PACKET_FLAG_RELIABLE);
    writePayload(stream);
    ENetPacket* pPacket = stream.getPacket();
    std::vector<uint8_t> bytes(pPacket->data, pPacket->data + pPacket->dataLength);
    enet_packet_destroy(pPacket);
    return bytes;
}

// Serialize the same payload through NetworkPacketBuffer and return the bytes.
template<typename F>
std::vector<uint8_t> bytesViaBuffer(F&& writePayload) {
    NetworkPacketBuffer buffer;
    writePayload(buffer);
    return buffer.takeBytes();
}

} // namespace

TEST_CASE("PacketStreams: primitive encodings are byte-identical to ENet streams",
          "[network][packet][webrtc]") {
    const auto bytesEnet = bytesViaEnet([](OutputStream& s) {
        s.writeUint8(0xAB);
        s.writeUint8(0x00);
        s.writeUint16(0xBEEF);
        s.writeUint16(0x0102);
        s.writeUint32(0xDEADBEEF);
        s.writeUint32(0x01020304);
        s.writeUint64(0x0123456789ABCDEFULL);
        s.writeBool(true);
        s.writeBool(false);
        s.writeSint32(-1234567);
        s.writeSint64(-9876543210LL);
        s.writeFloat(-3.5f);
        s.writeBools(true, false, true, true, false, false, true, false);
    });

    const auto bytesBuffer = bytesViaBuffer([](OutputStream& s) {
        s.writeUint8(0xAB);
        s.writeUint8(0x00);
        s.writeUint16(0xBEEF);
        s.writeUint16(0x0102);
        s.writeUint32(0xDEADBEEF);
        s.writeUint32(0x01020304);
        s.writeUint64(0x0123456789ABCDEFULL);
        s.writeBool(true);
        s.writeBool(false);
        s.writeSint32(-1234567);
        s.writeSint64(-9876543210LL);
        s.writeFloat(-3.5f);
        s.writeBools(true, false, true, true, false, false, true, false);
    });

    REQUIRE(bytesEnet == bytesBuffer);
    REQUIRE(bytesEnet.size() == 1 + 1 + 2 + 2 + 4 + 4 + 8 + 1 + 1 + 4 + 8 + 4 + 1);
}

TEST_CASE("PacketStreams: uint16 is 2 bytes little-endian", "[network][packet][webrtc]") {
    const auto bytes = bytesViaBuffer([](OutputStream& s) {
        s.writeUint16(0x1234);
    });

    REQUIRE(bytes.size() == 2);
    REQUIRE(bytes[0] == 0x34);  // low byte first
    REQUIRE(bytes[1] == 0x12);
}

TEST_CASE("PacketStreams: uint32 is 4 bytes little-endian", "[network][packet][webrtc]") {
    const auto bytes = bytesViaBuffer([](OutputStream& s) {
        s.writeUint32(0x12345678);
    });

    REQUIRE(bytes.size() == 4);
    REQUIRE(bytes[0] == 0x78);
    REQUIRE(bytes[1] == 0x56);
    REQUIRE(bytes[2] == 0x34);
    REQUIRE(bytes[3] == 0x12);
}

TEST_CASE("PacketStreams: string encodings are byte-identical to ENet streams",
          "[network][packet][webrtc]") {
    const auto bytesEnet = bytesViaEnet([](OutputStream& s) {
        s.writeString(std::string(""));
        s.writeString(std::string("Player"));
        s.writeString(std::string("pläyer\0name", 12));  // UTF-8 + embedded NUL byte
        s.writeString(std::string(300, 'x'));            // longer than the initial 16-byte allocation
    });

    const auto bytesBuffer = bytesViaBuffer([](OutputStream& s) {
        s.writeString(std::string(""));
        s.writeString(std::string("Player"));
        s.writeString(std::string("pläyer\0name", 12));  // UTF-8 + embedded NUL byte
        s.writeString(std::string(300, 'x'));            // longer than the initial 16-byte allocation
    });

    REQUIRE(bytesEnet == bytesBuffer);

    // Empty string is a bare zero length prefix
    REQUIRE(bytesBuffer.size() == 4 + (4 + 6) + (4 + 12) + (4 + 300));
}

TEST_CASE("PacketStreams: container encodings are byte-identical to ENet streams",
          "[network][packet][webrtc]") {
    const std::list<Uint32> list { 1, 2, 3 };
    const std::vector<Uint32> vector { 0xDEADBEEF, 0x01020304 };
    const std::set<Uint32> set { 7, 42, 4242 };

    const auto bytesEnet = bytesViaEnet([&](OutputStream& s) {
        s.writeUint32List(list);
        s.writeUint32Vector(vector);
        s.writeUint32Set(set);
    });

    const auto bytesBuffer = bytesViaBuffer([&](OutputStream& s) {
        s.writeUint32List(list);
        s.writeUint32Vector(vector);
        s.writeUint32Set(set);
    });

    REQUIRE(bytesEnet == bytesBuffer);
    REQUIRE(bytesBuffer.size() == (4 + 3*4) + (4 + 2*4) + (4 + 3*4));
}

TEST_CASE("PacketStreams: CommandList payload is byte-identical to ENet streams",
          "[network][packet][webrtc]") {
    // Encodes the exact Command/CommandList wire layout produced by
    // Command::save() (uint8 playerID, uint32 commandID, uint32Vector params)
    // and CommandList::save() (uint32 entryCount, per entry uint32 cycle,
    // uint32 commandCount, commands...).
    const Uint32 CMD_TEST_SYNC_ID = static_cast<Uint32>(CMD_TEST_SYNC);
    const auto writeCommandList = [&](OutputStream& s) {
        s.writeUint32(NETWORKPACKET_COMMANDLIST);
        s.writeUint32(2);   // two command list entries

        s.writeUint32(100); // entry 1: cycle
        s.writeUint32(2);   // two commands
        s.writeUint8(0);    // playerID
        s.writeUint32(CMD_TEST_SYNC_ID);
        s.writeUint32Vector({ 42u, 7u });
        s.writeUint8(3);    // playerID
        s.writeUint32(CMD_TEST_SYNC_ID);
        s.writeUint32Vector({ 0x12345678u });

        s.writeUint32(250); // entry 2: cycle
        s.writeUint32(1);   // one command
        s.writeUint8(1);    // playerID
        s.writeUint32(CMD_TEST_SYNC_ID);
        s.writeUint32Vector({ 9u, 9u, 9u, 9u });
    };

    const auto bytesEnet = bytesViaEnet(writeCommandList);
    const auto bytesBuffer = bytesViaBuffer(writeCommandList);

    REQUIRE(bytesEnet == bytesBuffer);

    // And decodes back through the view
    NetworkPacketView view(bytesBuffer.data(), bytesBuffer.size());
    REQUIRE(view.readUint32() == NETWORKPACKET_COMMANDLIST);
    REQUIRE(view.readUint32() == 2);
    REQUIRE(view.readUint32() == 100);
    REQUIRE(view.readUint32() == 2);
    REQUIRE(view.readUint8() == 0);
    REQUIRE(view.readUint32() == CMD_TEST_SYNC_ID);
    const auto params1 = view.readUint32Vector();
    REQUIRE(params1 == std::vector<Uint32> { 42u, 7u });
    REQUIRE(view.readUint8() == 3);
    REQUIRE(view.readUint32() == CMD_TEST_SYNC_ID);
    REQUIRE(view.readUint32Vector() == std::vector<Uint32> { 0x12345678u });
    REQUIRE(view.readUint32() == 250);
    REQUIRE(view.readUint32() == 1);
    REQUIRE(view.readUint8() == 1);
    REQUIRE(view.readUint32() == CMD_TEST_SYNC_ID);
    REQUIRE(view.readUint32Vector() == std::vector<Uint32> { 9u, 9u, 9u, 9u });
}

TEST_CASE("PacketStreams: representative control packets are byte-identical",
          "[network][packet][webrtc]") {
    SECTION("CHATMESSAGE") {
        const auto bytesEnet = bytesViaEnet([](OutputStream& s) {
            s.writeUint32(NETWORKPACKET_CHATMESSAGE);
            s.writeString(std::string("hello world"));
        });
        const auto bytesBuffer = bytesViaBuffer([](OutputStream& s) {
            s.writeUint32(NETWORKPACKET_CHATMESSAGE);
            s.writeString(std::string("hello world"));
        });
        REQUIRE(bytesEnet == bytesBuffer);
    }

    SECTION("CONFIG_HASH") {
        const auto write = [](OutputStream& s) {
            s.writeUint32(NETWORKPACKET_CONFIG_HASH);
            s.writeUint32(NETWORK_PROTOCOL_VERSION);
            s.writeString(std::string("dunecity1.0.531"));
            s.writeString(std::string("0123456789abcdef0123456789abcdef"));
            s.writeString(std::string("fedcba9876543210fedcba9876543210"));
        };
        REQUIRE(bytesViaEnet(write) == bytesViaBuffer(write));
    }

    SECTION("STARTGAME") {
        const auto write = [](OutputStream& s) {
            s.writeUint32(NETWORKPACKET_STARTGAME);
            s.writeUint32(5000u);
        };
        REQUIRE(bytesViaEnet(write) == bytesViaBuffer(write));
    }

    SECTION("KEEPALIVE") {
        const auto write = [](OutputStream& s) {
            s.writeUint32(NETWORKPACKET_KEEPALIVE);
            s.writeUint32(123456u);
        };
        REQUIRE(bytesViaEnet(write) == bytesViaBuffer(write));
    }
}

TEST_CASE("PacketStreams: ENet-written bytes decode through NetworkPacketView",
          "[network][packet][webrtc]") {
    const auto bytes = bytesViaEnet([](OutputStream& s) {
        s.writeUint32(0xA0B0C0D0u);
        s.writeUint16(0x1122);
        s.writeUint8(0x33);
        s.writeString(std::string("dune"));
        s.writeFloat(1.5f);
        s.writeBool(true);
    });

    NetworkPacketView view(bytes.data(), bytes.size());
    REQUIRE(view.readUint32() == 0xA0B0C0D0u);
    REQUIRE(view.readUint16() == 0x1122);
    REQUIRE(view.readUint8() == 0x33);
    REQUIRE(view.readString() == std::string("dune"));
    REQUIRE(view.readFloat() == 1.5f);
    REQUIRE(view.readBool() == true);
}

TEST_CASE("PacketStreams: buffer-written bytes decode through ENetPacketIStream",
          "[network][packet][webrtc]") {
    const auto bytes = bytesViaBuffer([](OutputStream& s) {
        s.writeUint32(0x01020304u);
        s.writeUint64(0x090A0B0C0D0E0F10ULL);
        s.writeString(std::string("city"));
        s.writeSint32(-7);
    });

    ENetPacket* pPacket = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    ENetPacketIStream stream(pPacket);
    REQUIRE(stream.readUint32() == 0x01020304u);
    REQUIRE(stream.readUint64() == 0x090A0B0C0D0E0F10ULL);
    REQUIRE(stream.readString() == std::string("city"));
    REQUIRE(stream.readSint32() == -7);
}

TEST_CASE("PacketStreams: views reject truncated packets like ENet streams",
          "[network][packet][webrtc]") {
    const auto bytes = bytesViaBuffer([](OutputStream& s) {
        s.writeUint32(NETWORKPACKET_CHATMESSAGE);
        s.writeString(std::string("hello world"));
    });

    SECTION("primitive overrun") {
        NetworkPacketView view(bytes.data(), 2);
        REQUIRE_THROWS_AS(view.readUint32(), InputStream::eof);
    }

    SECTION("string length overrun") {
        // header + bogus 0xFFFFFFFF length prefix
        const uint8_t truncated[] = { 0x06, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF };
        NetworkPacketView view(truncated, sizeof(truncated));
        REQUIRE(view.readUint32() == NETWORKPACKET_CHATMESSAGE);
        REQUIRE_THROWS_AS(view.readString(), InputStream::eof);
    }

    SECTION("string payload overrun") {
        // header + length 16 but only 3 bytes of payload
        const uint8_t truncated[] = { 0x06, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 'a', 'b', 'c' };
        NetworkPacketView view(truncated, sizeof(truncated));
        REQUIRE(view.readUint32() == NETWORKPACKET_CHATMESSAGE);
        REQUIRE_THROWS_AS(view.readString(), InputStream::eof);
    }
}

TEST_CASE("PacketStreams: buffer growth preserves earlier writes",
          "[network][packet][webrtc]") {
    NetworkPacketBuffer buffer;
    const std::string longStringA(40, 'a');
    const std::string longStringB(400, 'b');
    buffer.writeUint32(0xCAFEBABEu);
    buffer.writeString(longStringA);
    buffer.writeUint16(0x7777);
    buffer.writeString(longStringB);
    buffer.writeUint8(0x99);

    NetworkPacketView view(buffer.getData(), buffer.getDataLength());
    REQUIRE(view.readUint32() == 0xCAFEBABEu);
    REQUIRE(view.readString() == longStringA);
    REQUIRE(view.readUint16() == 0x7777);
    REQUIRE(view.readString() == longStringB);
    REQUIRE(view.readUint8() == 0x99);
}
