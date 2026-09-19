#include <Network/UpdateManifest.h>
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace DesktopUpdates {
namespace {
std::array<unsigned, 3> versionParts(const std::string& text) {
    std::array<unsigned, 3> parts{};
    std::istringstream in(text);
    std::string part;
    for (auto& value : parts) {
        if (!std::getline(in, part, '.') || part.empty() || part.size() > 6 ||
            part.find_first_not_of("0123456789") != std::string::npos ||
            (part.size() > 1 && part.front() == '0'))
            throw std::runtime_error("Invalid update version");
        value = static_cast<unsigned>(std::stoul(part));
    }
    if (!in.eof()) throw std::runtime_error("Invalid update version");
    return parts;
}
std::vector<unsigned char> decode(const std::string& s, std::size_t expected) {
    if (s.size() != 4 * ((expected + 2) / 3) ||
        s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") != std::string::npos)
        throw std::runtime_error("Invalid update signature encoding");
    std::vector<unsigned char> out(s.size());
    int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(s.data()), static_cast<int>(s.size()));
    if (!s.empty() && s.back() == '=') --n;
    if (s.size() > 1 && s[s.size()-2] == '=') --n;
    if (n != static_cast<int>(expected)) throw std::runtime_error("Invalid update signature length");
    out.resize(expected);
    std::string canonical(s.size() + 1, '\0');
    canonical.resize(EVP_EncodeBlock(reinterpret_cast<unsigned char*>(canonical.data()), out.data(), static_cast<int>(out.size())));
    if (canonical != s) throw std::runtime_error("Invalid update signature encoding");
    return out;
}
}
bool isHttpsUrl(const std::string& url) {
    if (url.compare(0, 8, "https://") != 0 || url.size() > 2048 ||
        url.find_first_of("\r\n\t \\#") != std::string::npos) return false;
    auto end = url.find('/', 8);
    const auto host = url.substr(8, end == std::string::npos ? end : end-8);
    return !host.empty() && host.find('@') == std::string::npos;
}
int compareVersions(const std::string& a, const std::string& b) {
    const auto x = versionParts(a), y = versionParts(b);
    return x == y ? 0 : (x < y ? -1 : 1);
}
Manifest verifyManifest(const std::string& text, const std::string& publicKey,
                        const std::string& platform) {
    if (text.empty() || text.size() > 4096 || text.back() != '\n' || text.find('\r') != std::string::npos)
        throw std::runtime_error("Invalid update manifest");
    std::istringstream in(text);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    if (lines.size() != 7 || lines[0] != "DuneCityUpdate1" || lines[2] != platform)
        throw std::runtime_error("Update is for a different platform");
    const auto signature = decode(lines[6], 64), key = decode(publicKey, 32);
    const auto payloadSize = text.size() - lines[6].size() - 1;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key.data(), key.size()), EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!pkey || !ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1 ||
        EVP_DigestVerify(ctx.get(), signature.data(), signature.size(),
            reinterpret_cast<const unsigned char*>(text.data()), payloadSize) != 1)
        throw std::runtime_error("The update signature could not be verified");
    versionParts(lines[1]);
    if (!isHttpsUrl(lines[3]) || lines[4].empty() || lines[4].size() > 10 ||
        lines[4].find_first_not_of("0123456789") != std::string::npos ||
        lines[5].size() != 64 || lines[5].find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid update download information");
    const auto size = std::stoull(lines[4]);
    if (size == 0 || size > 536870912) throw std::runtime_error("Update exceeds the download limit");
    return {lines[1], lines[2], lines[3], lines[5], size};
}
std::string fileSha256(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!in || !ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot read update file");
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), buffer.size());
        if (EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<std::size_t>(in.gcount())) != 1)
            throw std::runtime_error("Cannot verify update file");
    }
    if (!in.eof()) throw std::runtime_error("Cannot read update file");
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned size = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &size) != 1) throw std::runtime_error("Cannot verify update file");
    std::ostringstream out;
    for (unsigned i = 0; i < size; ++i) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(digest[i]);
    return out.str();
}
}
