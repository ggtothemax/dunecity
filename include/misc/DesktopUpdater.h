#pragma once
#include <Network/UpdateManifest.h>
#include <string>
#if defined(DUNECITY_DESKTOP_UPDATER)
#include <atomic>
#include <future>
#endif

class DesktopUpdater {
public:
    enum class State { Idle, Checking, Current, Available, Installing, Restart, Failed };
    static DesktopUpdater& instance();
    ~DesktopUpdater();
    void check();
    void poll(); // Main menu only: no prompts, swaps or restarts during a match.
    void install();
    State state() const { return state_; }
    const std::string& message() const { return message_; }
    const std::string& version() const { return manifest_.version; }
    bool supported() const;
    bool busy() const { return state_ == State::Installing; }
    // Called after normal game shutdown, so saves/settings are flushed first.
    static void relaunchAfterShutdown();
private:
    DesktopUpdater() = default;
    State state_ = State::Idle;
    DesktopUpdates::Manifest manifest_;
    std::string message_;
#if defined(DUNECITY_DESKTOP_UPDATER)
    std::future<DesktopUpdates::Manifest> checkTask_;
    std::future<std::string> installTask_;
    std::atomic<bool> cancel_{false};
#endif
};

namespace NativeUpdater {
// macOS/Windows adapters own their native dialog session and report errors.
bool begin(std::string& error);
bool busy();
std::string takeError();
void cleanup();
}
