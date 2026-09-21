// Opt-in extension of the real three-peer late-join fixture. File barriers only
// coordinate test actions; every control travels through the production network.
#include <GUI/dune/InGameSettingsMenu.h>
#include <GUI/dune/InGameMenu.h>
#include "match-controls-wire-probe.inc"
static bool probeMatchControls(Game& game, const std::string& role, const std::string& out) {
    if(!std::getenv("JOIN_MATCH_CONTROLS")) return false;
    static bool checkedWire=false;
    if(!checkedWire) {probeMatchControlWire(); checkedWire=true;}
    const bool host=role=="Host", viewer=role=="Newcomer";
    const auto exists=[&](const std::string& name){return std::filesystem::exists(out+"/control-"+name);};
    const auto write=[&](const std::string& name,const std::string& value="yes"){joinWrite(out+"/control-"+name,value);};
    const auto read=[&](const std::string& name){return joinRead(out+"/control-"+name);};
    const int stage=exists("done-2") ? 3 : exists("done-1") ? 2 : 1;
    const std::string tag=std::to_string(stage), mine=role+"-"+tag;
    const Uint32 cycle=game.getGameCycleCount();
    if(exists("done-3") || (stage==2 && cycle<300) || (stage==3 && !viewer && cycle<700)) return false;
    if(stage==2 && !viewer && cycle==300) {
        write(role+"-ready");
        if(!exists("Host-ready") || !exists("Partner-ready")) return true;
    }
    const bool shouldPress=!viewer && ((stage==1 && !host && cycle>=100)
        || (stage==2 && cycle>=300) || (stage==3 && host && cycle>=700));
    if(shouldPress && !exists(mine+"-requested")) {
        game.toggleMatchPause(); write(mine+"-requested");
    }
    if(game.isGamePaused()) {
        if(!exists(mine+"-paused")) {
            write(mine+"-paused",std::to_string(cycle));
            game.onOptions();
            joinCapture(game,out+"/controls-menu-"+mine+".bmp");
            dynamic_cast<InGameMenu*>(game.pInGameMenu.get())->onSettings();
            joinCapture(game,out+"/controls-settings-"+mine+".bmp");
            game.pInGameMenu->closeChildWindow();
            game.resumeGame(); game.pInGameMenu.reset();
            if(!game.isGamePaused()) throw std::runtime_error("closing Options resumed shared pause");
            InGameSettingsMenu options;
            if(options.gameSpeedPlus.isEnabled()!=host || options.gameSpeedMinus.isEnabled()!=host)
                throw std::runtime_error("speed settings authority differs from host role");
            if(!host && game.requestGameSpeed(32)) throw std::runtime_error("client changed shared speed");
            if(viewer) {
                if(game.canToggleMatchPause()) throw std::runtime_error("spectator has pause authority");
                game.toggleMatchPause();
                if(!game.isGamePaused()) throw std::runtime_error("spectator resumed the match");
            }
            joinCapture(game,out+"/controls-"+mine+".bmp");
        } else if(read(mine+"-paused")!=std::to_string(cycle)) {
            throw std::runtime_error("simulation advanced during explicit pause");
        }
        if(exists("Host-"+tag+"-paused") && exists("Partner-"+tag+"-paused")) {
            if(read("Host-"+tag+"-paused")!=read("Partner-"+tag+"-paused"))
                throw std::runtime_error("players paused at different cycles");
            if(stage==1) {
                if(host && !exists("speed-set")) {
                    if(!game.requestGameSpeed(4)) throw std::runtime_error("host could not change paused speed");
                    write("speed-set");
                }
                if(!host && game.getGameSpeed()==4) write("speed-received");
                if(host && exists("speed-received")) {game.toggleMatchPause(); write("done-1");}
            } else if(stage==2 && !host) {
                game.toggleMatchPause(); write("done-2");
            } else if(stage==3 && host && exists("Newcomer-3-paused")) {
                if(read("Newcomer-3-paused")!=std::to_string(cycle))
                    throw std::runtime_error("late spectator did not inherit the paused cycle");
                game.toggleMatchPause(); write("done-3");
            }
        }
    }
    return false;
}
