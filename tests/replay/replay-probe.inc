// Diagnostic entry point linked to production engine objects; never shipped.
#include <Game.h>
#include <mod/ModManager.h>
#include <Network/GameStateDigest.h>
#include <memory>
#include <execinfo.h>
#include <dlfcn.h>

#ifdef REPLAY_TRACE_THROWS
static bool replayTraceThrows = false;
extern "C" void __cxa_throw(void* value, std::type_info* type, void(*destroy)(void*)) {
    if(replayTraceThrows) { void* frames[48]; const int size=backtrace(frames,48); backtrace_symbols_fd(frames,size,2); }
    using Throw = void(*)(void*,std::type_info*,void(*)(void*));
    static const auto original = reinterpret_cast<Throw>(dlsym(RTLD_NEXT,"__cxa_throw"));
    original(value,type,destroy);
    __builtin_unreachable();
}

#endif
int runReplayProbe() {
    ModManager::instance().setActiveMod("dunecity");
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->initReplay(std::getenv("REPLAY_PROBE_FILE"));
    game->initializeGameLoop();
    game->bPause = false;
#ifdef REPLAY_TRACE_THROWS
    replayTraceThrows = true;
#endif
    std::fprintf(stderr,"REPLAY_PROBE_READY\n");
    const auto end = static_cast<Uint32>(std::stoul(std::getenv("REPLAY_PROBE_CYCLES")));
    try {
        while(game->getGameCycleCount() < end) {
            const auto cycle = game->getGameCycleCount();
            game->updateGameState();
            if(game->getGameCycleCount() != cycle+1) throw std::runtime_error("Replay did not advance");
            if(game->getGameCycleCount()%5000 == 0) std::fprintf(stderr,"REPLAY_PROBE_CYCLE: %u\n",game->getGameCycleCount());
        }
    } catch(const std::exception& error) {
        std::fprintf(stderr,"REPLAY_PROBE_FAIL: cycle=%u error=%s\n",game->getGameCycleCount(),error.what());
        throw;
    }
    std::fprintf(stderr,"REPLAY_PROBE_PASS: %s\n",GameStateDigest::describe(game->computeStateDigest()).c_str());
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    return MENU_QUIT_DEFAULT;
}
