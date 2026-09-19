#include <misc/DesktopUpdater.h>
#include <UpdateConfig.h>
#include <config.h>
#include <SDL.h>
#include <winsparkle.h>
#include <atomic>
#include <string>

namespace {
std::atomic<bool> active{false}, failed{false};
bool initialized = false;
void __cdecl done() { active.store(false); }
void __cdecl error() { failed.store(true); active.store(false); }
int __cdecl canShutdown() { return active.load() ? 1 : 0; }
void __cdecl shutdown() { SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event); }
}
namespace NativeUpdater {
bool begin(std::string& message) {
    if (!initialized) {
        const std::string version = VERSION;
        const std::wstring wideVersion(version.begin(), version.end());
        win_sparkle_set_app_details(L"Dune City Team", L"Dune City", wideVersion.c_str());
        win_sparkle_set_appcast_url(DUNECITY_UPDATE_FEED_BASE "/appcast-windows-x64.xml");
        if (!win_sparkle_set_eddsa_public_key(DUNECITY_UPDATE_PUBLIC_KEY)) {
            message = "The update signing key is invalid."; return false;
        }
        win_sparkle_set_automatic_check_for_updates(0);
        win_sparkle_set_can_shutdown_callback(canShutdown);
        win_sparkle_set_shutdown_request_callback(shutdown);
        win_sparkle_set_error_callback(error);
        win_sparkle_set_did_not_find_update_callback(done);
        win_sparkle_set_update_cancelled_callback(done);
        win_sparkle_set_update_skipped_callback(done);
        win_sparkle_set_update_postponed_callback(done);
        win_sparkle_set_update_dismissed_callback(done);
        win_sparkle_init(); initialized = true;
    }
    active.store(true); failed.store(false);
    win_sparkle_check_update_with_ui_and_install();
    return true;
}
bool busy() { return active.load(); }
std::string takeError() {
    return failed.exchange(false) ? "The update could not be installed. Your current game has been kept. Please try again later." : "";
}
void cleanup() { if (initialized) win_sparkle_cleanup(); }
}
