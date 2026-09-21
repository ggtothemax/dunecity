#ifndef SNAPSHOTTRANSFER_H
#define SNAPSHOTTRANSFER_H

#include <cstdint>
#include <string>

// Lossless spectator checkpoint transport. The save format itself is unchanged.
namespace SnapshotTransfer {
constexpr std::uint32_t maxBytes = 8u * 1024 * 1024;
constexpr std::uint32_t compressedAck = 1;

// Empty headers identify the legacy raw stream; DCZ1 carries its decoded length.
// A zero decoded size means raw. All sizes are bounded before allocation.
bool readHeader(const std::string& header, std::uint32_t wireSize, std::uint32_t& decodedSize);
bool decode(const std::string& wire, std::uint32_t decodedSize, std::string& result);

struct Sender {
    enum class Ack { Invalid, Ready, RetryRaw };
    std::string bytes;
    bool prepare(std::string raw);
    std::string header() const;
    Ack acknowledge(std::uint32_t offset);
    std::uint32_t decodedSize() const { return rawSize; }
private:
    std::string rawFallback;
    std::uint32_t rawSize = 0;
};
}
#endif
