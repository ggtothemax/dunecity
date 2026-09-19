#include <misc/DesktopUpdater.h>
#include <config.h>
#include <SDL.h>
#include <chrono>
#include <stdexcept>

#if defined(DUNECITY_DESKTOP_UPDATER)
#include <UpdateConfig.h>
#include <curl/curl.h>
#include <fstream>
#include <memory>
#include <vector>
#if defined(__linux__)
#include <misc/AppImageUpdate.h>
#include <sys/stat.h>
#include <unistd.h>
#include <spawn.h>
#include <fcntl.h>
extern char** environ;
#endif

namespace {
struct Download {
    std::string text;
    FILE* file = nullptr;
    std::uint64_t size = 0, limit = 4096;
    std::atomic<bool>* cancel = nullptr;
};
size_t receive(char* data, size_t size, size_t count, void* context) {
    auto& d = *static_cast<Download*>(context);
    if (size && count > SIZE_MAX / size) return 0;
    const auto bytes = size * count;
    if (bytes > d.limit - d.size || d.cancel->load()) return 0;
    if (d.file) { if (fwrite(data, 1, bytes, d.file) != bytes) return 0; }
    else { try { d.text.append(data, bytes); } catch (...) { return 0; } }
    d.size += bytes;
    return bytes;
}
int progress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic<bool>*>(p)->load() ? 1 : 0;
}
void fetch(const std::string& url, Download& d) {
    if (!DesktopUpdates::isHttpsUrl(url)) throw std::runtime_error("Update requires a secure download URL");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Cannot start the update download");
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_NETRC, CURL_NETRC_IGNORED);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, d.file ? 900L : 30L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "DuneCity-Updater/" VERSION);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, d.cancel);
    const auto result = curl_easy_perform(curl.get());
    long status = 0; curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status == 404 && !d.file)
        throw std::runtime_error("No update has been published to this channel yet.");
    if (result != CURLE_OK || status != 200)
        throw std::runtime_error("Could not download the update. Check your connection and try again later.");
}
#if defined(__linux__)
std::string restartPath, backupPath;
std::string appImagePath() {
    const char* image = getenv("APPIMAGE");
    if (!image || image[0] != '/') return {};
    struct stat st{};
    if (lstat(image, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid()) return {};
    return image;
}
#endif
}
#endif

DesktopUpdater& DesktopUpdater::instance() { static DesktopUpdater updater; return updater; }
DesktopUpdater::~DesktopUpdater() {
#if defined(DUNECITY_DESKTOP_UPDATER)
    cancel_.store(true);
    if (checkTask_.valid()) checkTask_.wait();
    if (installTask_.valid()) installTask_.wait();
#if defined(__APPLE__) || defined(_WIN32)
    NativeUpdater::cleanup();
#endif
#endif
}
bool DesktopUpdater::supported() const {
#if !defined(DUNECITY_DESKTOP_UPDATER)
    return false;
#elif defined(__linux__)
    return !appImagePath().empty();
#elif defined(_WIN32)
    // Portable ZIP users install the EXE once. Thereafter NSIS remembers the
    // installation directory and updates it in place.
    char* base = SDL_GetBasePath();
    const bool installed = base && std::ifstream(std::string(base) + "dunecity-installed.txt").good();
    SDL_free(base); return installed;
#else
    return true;
#endif
}
void DesktopUpdater::check() {
#if defined(DUNECITY_DESKTOP_UPDATER)
    if (state_ == State::Checking || busy()) return;
    state_ = State::Checking; message_.clear(); cancel_.store(false);
    checkTask_ = std::async(std::launch::async, [this] {
        Download d; d.cancel = &cancel_;
        fetch(std::string(DUNECITY_UPDATE_FEED_BASE) + "/updates-" DUNECITY_UPDATE_PLATFORM ".txt", d);
        return DesktopUpdates::verifyManifest(d.text, DUNECITY_UPDATE_PUBLIC_KEY, DUNECITY_UPDATE_PLATFORM);
    });
#endif
}
void DesktopUpdater::poll() {
#if defined(DUNECITY_DESKTOP_UPDATER)
    try {
        if (state_ == State::Checking && checkTask_.valid() && checkTask_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            manifest_ = checkTask_.get();
            state_ = DesktopUpdates::compareVersions(manifest_.version, VERSION) > 0 ? State::Available : State::Current;
        }
        if (state_ == State::Installing) {
#if defined(__linux__)
            if (installTask_.valid() && installTask_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                backupPath = installTask_.get(); state_ = State::Restart;
            }
#else
            if (!NativeUpdater::busy()) {
                const auto error = NativeUpdater::takeError();
                if (!error.empty()) throw std::runtime_error(error);
                state_ = State::Available;
            }
#endif
        }
    } catch (const std::exception& e) { state_ = State::Failed; message_ = e.what(); SDL_Log("Updater: %s", e.what()); }
#endif
}
void DesktopUpdater::install() {
#if defined(DUNECITY_DESKTOP_UPDATER)
    if (state_ != State::Available || !supported()) return;
    state_ = State::Installing; message_.clear();
#if defined(__linux__)
    restartPath = appImagePath();
    const auto image = restartPath;
    const auto manifest = manifest_;
    installTask_ = std::async(std::launch::async, [this, image, manifest] {
        std::string pattern = image + ".download-XXXXXX";
        std::vector<char> path(pattern.begin(), pattern.end()); path.push_back(0);
        int fd = mkstemp(path.data());
        if (fd < 0) throw std::runtime_error("Move the AppImage to a writable folder before updating");
        FILE* file = fdopen(fd, "wb");
        if (!file) { close(fd); unlink(path.data()); throw std::runtime_error("Cannot create update download"); }
        try {
            Download d; d.file = file; d.limit = manifest.bytes; d.cancel = &cancel_;
            fetch(manifest.url, d);
            const bool written = fflush(file) == 0 && fsync(fileno(file)) == 0;
            fclose(file); file = nullptr;
            if (!written || d.size != manifest.bytes || DesktopUpdates::fileSha256(path.data()) != manifest.sha256)
                throw std::runtime_error("The update is incomplete or failed verification. Your current game has been kept.");
            std::ifstream in(path.data(), std::ios::binary);
            char magic[11]{}; in.read(magic, sizeof(magic));
            if (in.gcount() != sizeof(magic) || std::string(magic,4) != std::string("\177ELF",4) || magic[8] != 'A' || magic[9] != 'I' || magic[10] != 2)
                throw std::runtime_error("The update is not a supported AppImage");
            return AppImageUpdate::replace(image, path.data(), manifest.sha256);
        } catch (...) { if (file) fclose(file); unlink(path.data()); throw; }
    });
#else
    if (!NativeUpdater::begin(message_)) state_ = State::Failed;
#endif
#endif
}
void DesktopUpdater::relaunchAfterShutdown() {
#if defined(DUNECITY_DESKTOP_UPDATER) && defined(__linux__)
    if (instance().state_ != State::Restart || restartPath.empty()) return;
    // Do not inherit the old mounted AppImage runtime's library environment.
    std::vector<std::string> environment;
    for (char** p = environ; p && *p; ++p) {
        const std::string s(*p);
        if (s.compare(0,9,"APPIMAGE=") && s.compare(0,7,"APPDIR=") &&
            s.compare(0,16,"LD_LIBRARY_PATH=") && s.compare(0,11,"LD_PRELOAD=") &&
            s.compare(0,8,"ARGV0=")) environment.push_back(s);
    }
    std::vector<char*> env; for (auto& s : environment) env.push_back(s.data()); env.push_back(nullptr);
    char* args[] = {restartPath.data(), nullptr}; pid_t pid;
    if (posix_spawn(&pid, restartPath.c_str(), nullptr, nullptr, args, env.data()) != 0) {
        if (!backupPath.empty()) rename(backupPath.c_str(), restartPath.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dune City update", "The update could not restart. The previous AppImage has been restored. Please open it again.", nullptr);
    }
#endif
}
