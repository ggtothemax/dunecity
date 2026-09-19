#pragma once

#include <cstdint>
#include <string>

namespace DesktopUpdates {
struct Manifest {
    std::string version, platform, url, sha256;
    std::uint64_t bytes = 0;
};
// A strictly parsed, Ed25519-signed manifest. Never accept metadata from the
// legacy metaserver as authority to replace executable code.
Manifest verifyManifest(const std::string& text, const std::string& publicKey,
                        const std::string& platform);
int compareVersions(const std::string& a, const std::string& b);
bool isHttpsUrl(const std::string& url);
std::string fileSha256(const std::string& path);
}
