#ifndef OBSERVERSTREAMPOLICY_H
#define OBSERVERSTREAMPOLICY_H

#include <cstdint>

namespace ObserverStreamPolicy {
constexpr std::uint32_t chunkBytes=48u*1024;
// Bound unacknowledged data without paying a full round trip per chunk. The
// existing global per-update send allowance still limits spectator bandwidth.
constexpr std::uint32_t windowBytes=4*chunkBytes;

inline bool canSendChunk(std::uint32_t acknowledged, std::uint32_t sent, std::uint32_t total) {
    return acknowledged<=sent && sent<total && sent-acknowledged<windowBytes;
}

inline bool validChunkAck(std::uint32_t acknowledged, std::uint32_t sent,
                          std::uint32_t total, std::uint32_t next) {
    return sent<=total && next>acknowledged && next<=sent
        && (next==total || next%chunkBytes==0);
}
}

#endif
