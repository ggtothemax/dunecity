#include <misc/AppImageUpdate.h>
#include <Network/UpdateManifest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace AppImageUpdate {
std::string replace(const std::string& original, const std::string& staged,
                    const std::string& expectedHash) {
    struct stat old{}, next{};
    if (lstat(original.c_str(), &old) || lstat(staged.c_str(), &next) ||
        !S_ISREG(old.st_mode) || !S_ISREG(next.st_mode) ||
        old.st_uid != geteuid() || next.st_uid != geteuid() ||
        old.st_dev != next.st_dev || old.st_ino == next.st_ino)
        throw std::runtime_error("The AppImage must be in a writable folder owned by you");
    if (DesktopUpdates::fileSha256(staged) != expectedHash)
        throw std::runtime_error("The downloaded update failed verification");
    int fd = open(staged.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("Cannot open the downloaded update");
    const bool flushed = fchmod(fd, 0755) == 0 && fsync(fd) == 0;
    close(fd);
    if (!flushed) throw std::runtime_error("Cannot save the downloaded update");
    std::string pattern = original + ".previous-XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end()); temp.push_back(0);
    fd = mkstemp(temp.data());
    if (fd < 0) throw std::runtime_error("Cannot create an AppImage backup");
    close(fd);
    unlink(temp.data());
    if (link(original.c_str(), temp.data())) throw std::runtime_error("Cannot back up the current AppImage");
    struct stat now{};
    if (lstat(original.c_str(), &now) || now.st_dev != old.st_dev || now.st_ino != old.st_ino ||
        rename(staged.c_str(), original.c_str())) {
        unlink(temp.data());
        throw std::runtime_error("Cannot replace the AppImage; the current game has been kept");
    }
    const auto parent = original.substr(0, original.find_last_of('/'));
    fd = open(parent.c_str(), O_RDONLY);
    if (fd >= 0) { fsync(fd); close(fd); }
    return temp.data();
}
}
