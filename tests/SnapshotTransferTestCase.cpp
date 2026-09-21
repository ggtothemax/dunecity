#include <catch2/catch_test_macros.hpp>
#include <Network/SnapshotTransfer.h>
#include <random>
#include <zlib.h>

using namespace SnapshotTransfer;

TEST_CASE("Compressed spectator checkpoint restores every byte", "[network][spectator][compression]") {
    Sender sender;
    std::string original;
    for(unsigned i=0; i<20000; ++i) original.append("\0game-state\xff", 12);
    REQUIRE(sender.prepare(original));
    REQUIRE(sender.bytes.size() < original.size()/10);
    std::uint32_t decodedSize=0;
    REQUIRE(readHeader(sender.header(), sender.bytes.size(), decodedSize));
    REQUIRE(decodedSize == original.size());
    REQUIRE(sender.acknowledge(compressedAck) == Sender::Ack::Ready);
    std::string result;
    REQUIRE(decode(sender.bytes, decodedSize, result));
    REQUIRE(result == original);
}

TEST_CASE("Spectator compression negotiates with both legacy endpoints", "[network][spectator][compression]") {
    const std::string original(200000, 'x');
    Sender sender;
    REQUIRE(sender.prepare(original));
    SECTION("old receiver ignores proposal and gets a replacement raw header") {
        REQUIRE_FALSE(sender.header().empty());
        REQUIRE(sender.acknowledge(42) == Sender::Ack::Invalid);
        REQUIRE(sender.acknowledge(0) == Sender::Ack::RetryRaw);
        REQUIRE(sender.bytes == original);
        REQUIRE(sender.header().empty());
        REQUIRE(sender.acknowledge(compressedAck) == Sender::Ack::Invalid);
        REQUIRE(sender.acknowledge(0) == Sender::Ack::Ready);
    }
    SECTION("new receiver accepts legacy host without a compression acknowledgement") {
        std::uint32_t size=123;
        REQUIRE(readHeader({}, original.size(), size));
        REQUIRE(size == 0);
        std::string result;
        REQUIRE(decode(original, size, result));
        REQUIRE(result == original);
    }
}

TEST_CASE("Incompressible and tiny checkpoints remain raw", "[network][spectator][compression]") {
    Sender sender;
    std::mt19937 random(1234);
    std::string bytes(4096, '\0');
    for(auto& byte : bytes) byte=static_cast<char>(random());
    for(const auto& original : {std::string("a"), bytes}) {
        REQUIRE(sender.prepare(original));
        REQUIRE(sender.header().empty());
        REQUIRE(sender.bytes == original);
    }
}

TEST_CASE("Checkpoint headers and decompression enforce hard bounds", "[network][spectator][compression]") {
    Sender sender;
    const std::string original(200000, 'x');
    REQUIRE(sender.prepare(original));
    const auto header=sender.header();
    std::uint32_t size=0;
    REQUIRE_FALSE(readHeader({}, 0, size));
    REQUIRE_FALSE(readHeader({}, maxBytes+1, size));
    REQUIRE_FALSE(readHeader("DCZ2xxxx", sender.bytes.size(), size));
    REQUIRE_FALSE(readHeader(header+"x", sender.bytes.size(), size));
    REQUIRE_FALSE(readHeader(header.substr(0,7), sender.bytes.size(), size));
    REQUIRE_FALSE(readHeader(std::string("DCZ1\0\0\0\0",8), sender.bytes.size(), size));
    REQUIRE_FALSE(readHeader(std::string("DCZ1\xff\xff\xff\xff",8), sender.bytes.size(), size));
    REQUIRE_FALSE(readHeader(header, original.size(), size));
    std::string result="old contents";
    REQUIRE_FALSE(decode(sender.bytes, original.size()-1, result));
    REQUIRE(result.empty());
    REQUIRE_FALSE(decode(sender.bytes, original.size()+1, result));
    REQUIRE_FALSE(decode(sender.bytes, maxBytes+1, result));
    REQUIRE_FALSE(decode(sender.bytes.substr(0,sender.bytes.size()-1), original.size(), result));
    REQUIRE_FALSE(decode(sender.bytes+"x", original.size(), result));
    REQUIRE_FALSE(decode(sender.bytes+sender.bytes, original.size(), result));
    auto corrupt=sender.bytes;
    corrupt.back() ^= 1;
    REQUIRE_FALSE(decode(corrupt, original.size(), result));
    REQUIRE_FALSE(decode({}, 0, result));
    REQUIRE_FALSE(sender.prepare({}));
    REQUIRE_FALSE(sender.prepare(std::string(maxBytes+1, 'x')));
    REQUIRE(sender.prepare(std::string(maxBytes, 'x')));
    REQUIRE(decode(sender.bytes, maxBytes, result));
    REQUIRE(result == std::string(maxBytes, 'x'));
    // The same highly compressed input must not expand past a claimed small size.
    REQUIRE_FALSE(decode(sender.bytes, 16, result));
}

TEST_CASE("Checkpoint decoder accepts standard zlib streams", "[network][spectator][compression]") {
    const std::string original(200000, 'z');
    std::string wire(compressBound(original.size()), '\0');
    uLongf size=wire.size();
    REQUIRE(compress2(reinterpret_cast<Bytef*>(&wire[0]), &size,
        reinterpret_cast<const Bytef*>(original.data()), original.size(), Z_BEST_COMPRESSION) == Z_OK);
    wire.resize(size);
    std::string result;
    REQUIRE(decode(wire, original.size(), result));
    REQUIRE(result == original);
}
