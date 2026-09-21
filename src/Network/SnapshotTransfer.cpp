#include <Network/SnapshotTransfer.h>
#include <zlib.h>
#include <utility>

namespace SnapshotTransfer {
bool readHeader(const std::string& header, std::uint32_t wireSize, std::uint32_t& decodedSize) {
    decodedSize = 0;
    if(wireSize == 0 || wireSize > maxBytes) return false;
    if(header.empty()) return true;
    if(header.size() != 8 || header.compare(0, 4, "DCZ1") != 0) return false;
    std::uint32_t size = 0;
    for(unsigned i = 0; i < 4; ++i)
        size |= std::uint32_t(static_cast<unsigned char>(header[4 + i])) << (8 * i);
    if(size == 0 || size > maxBytes || wireSize >= size) return false;
    decodedSize = size;
    return true;
}

bool Sender::prepare(std::string raw) {
    bytes.clear(); rawFallback.clear(); rawSize = 0;
    if(raw.empty() || raw.size() > maxBytes) return false;
    std::string compressed(compressBound(static_cast<uLong>(raw.size())), '\0');
    uLongf size = compressed.size();
    // Fast compression keeps checkpoint capture from stalling active players.
    const auto status = compress2(reinterpret_cast<Bytef*>(&compressed[0]), &size,
        reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), Z_BEST_SPEED);
    if(status == Z_OK && size + 8 < raw.size()) {
        compressed.resize(size);
        rawSize = static_cast<std::uint32_t>(raw.size());
        rawFallback = std::move(raw);
        bytes = std::move(compressed);
    } else bytes = std::move(raw);
    return true;
}

std::string Sender::header() const {
    if(!rawSize) return {};
    std::string result = "DCZ1";
    for(unsigned i = 0; i < 4; ++i) result += static_cast<char>((rawSize >> (8 * i)) & 255);
    return result;
}

Sender::Ack Sender::acknowledge(std::uint32_t offset) {
    if(rawSize) {
        if(offset == compressedAck) {
            std::string().swap(rawFallback);
            return Ack::Ready;
        }
        // Old clients ignore the header data and acknowledge with zero. Send a
        // replacement legacy header before any chunks, then await its ACK.
        if(offset == 0 && !rawFallback.empty()) {
            bytes = std::move(rawFallback);
            rawSize = 0;
            return Ack::RetryRaw;
        }
        return Ack::Invalid;
    }
    return offset == 0 ? Ack::Ready : Ack::Invalid;
}

bool decode(const std::string& wire, std::uint32_t decodedSize, std::string& result) {
    result.clear();
    if(wire.empty() || wire.size() > maxBytes || decodedSize > maxBytes) return false;
    if(decodedSize == 0) { result = wire; return true; }
    std::string decoded(decodedSize, '\0');
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(wire.data()));
    stream.avail_in = static_cast<uInt>(wire.size());
    stream.next_out = reinterpret_cast<Bytef*>(&decoded[0]);
    stream.avail_out = decodedSize;
    if(inflateInit(&stream) != Z_OK) return false;
    const auto status = inflate(&stream, Z_FINISH);
    // Reject expansion past the bound, wrong length, truncation, trailing data,
    // and checksum errors. Never grow the buffer based on untrusted input.
    const bool valid = status == Z_STREAM_END && stream.total_out == decodedSize && stream.avail_in == 0;
    inflateEnd(&stream);
    if(valid) result = std::move(decoded);
    return valid;
}
}
