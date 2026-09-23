#include <Menu/PlaySetup.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/MenuBase.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/TextManager.h>
#include <GUI/MsgBox.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <misc/fnkdat.h>
#include <misc/string_util.h>
#include <misc/WebRuntime.h>
#include <mod/ModManager.h>
#include <Network/WorkshopGameContent.h>
#include <globals.h>
#include <main.h>
#include <sand.h>
#include <algorithm>
#include <utility>

namespace {
std::string userDirectory(const char* name) {
    char path[FILENAME_MAX];
    return fnkdat(name, path, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0 ? path : "";
}

std::vector<std::string> maps() {
    std::vector<std::string> result;
    for(const char* kind : {"singleplayer", "multiplayer"}) {
        std::string dir = getDuneLegacyDataDir() + "/maps/" + kind + "/";
        if(getFileNamesList(dir, "ini", true).empty()) dir = getDuneLegacyDataDir() + "/data/maps/" + kind + "/";
        for(const auto& source : {dir, userDirectory((std::string("maps/") + kind + "/").c_str())}) {
            if(source.empty()) continue;
            for(const auto& file : getFileNamesList(source, "ini", true)) result.push_back(source + file);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return strToLower(getBasename(a)) < strToLower(getBasename(b));
    });
    return result;
}

// Use the existing validated save header parser. A file's folder is not its game type.
GameInitSettings readSetup(const std::string& path, GameInitSettings::HouseInfoList& houses) {
    const auto bytes = readCompleteFile(path);
    IMemoryStream stream(bytes.data(), bytes.size());
    return GameInitSettings::readSaveSetup(stream, houses);
}

class PlayError final : public MenuBase {
public:
    explicit PlayError(const std::string& message) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
        openWindow(MsgBox::create(message));
    }
    void onChildWindowClose(Window*) override { quit(); }
};

class GameLibrary final : public MenuBase {
public:
    explicit GameLibrary(bool replay) : replay(replay) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
    }
    int showMenu() override {
        const auto saves = userDirectory("save/");
        const auto shared = userDirectory("mpsave/");
        const auto recordings = userDirectory("replay/");
        if(replay) openWindow(LoadSaveWindow::create(false, _("Replays"), recordings, "rpl"));
        else openWindow(LoadSaveWindow::create(false, _("Load Game"),
            std::vector<std::string>{saves, shared}, std::vector<std::string>{_("Saved games"), _("Online saves")}, "dls"));
        return MenuBase::showMenu();
    }
    void onChildWindowClose(Window* child) override {
        auto* picker = dynamic_cast<LoadSaveWindow*>(child);
        if(!picker) { quit(); return; }
        const auto path = picker->getFilename();
        if(path.empty()) { quit(); return; }
        // Run after the modal has been removed, rather than starting a nested game from its destructor.
        selected = path;
    }
    void update() override {
        if(selected.empty()) return;
        const auto path = std::exchange(selected, {});
        try {
            if(replay) startReplay(path);
            else {
                GameInitSettings::HouseInfoList houses;
                auto saved = readSetup(path, houses);
                WorkshopGameContent::resolveMod(saved);
                if(isNetworkGameType(saved.getGameType())) {
                    auto& mods = ModManager::instance();

                    GameInitSettings init(getBasename(path), readCompleteFile(path), settings.general.playerName + "'s saved game");
                    init.configureCoopSave(saved, houses);
                    if(isCoopGameType(saved.getGameType())) {
                        init.enableCoop(true, settings.general.playerName + "'s co-op campaign");
                    }
                    CrossplayMenu(init, true).showMenu();
                } else startSinglePlayerGame(GameInitSettings(path));
            }
            quit();
        } catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); }
    }
private:
    bool replay;
    std::string selected;
};

std::string recentGame() {
    const auto dir = userDirectory("save/");
    if(dir.empty()) return {};
    const auto installed = ModManager::instance().listMods();
    for(const auto& file : getFileNamesList(dir, "dls", true, FileListOrder_ModifyDate_Dsc)) {
        try {
            GameInitSettings::HouseInfoList houses;
            const auto saved = readSetup(dir + file, houses);
            if(!isNetworkGameType(saved.getGameType())
               && (!saved.getModRevisionHash().empty() || std::any_of(installed.begin(), installed.end(), [&](const auto& mod) { return mod.name == saved.getModName(); }))) return dir + file;
        } catch(const std::exception&) { /* Skip corrupt, newer or unavailable saves. */ }
    }
    return {};
}
}

void rememberPlayMode(PlayModeScope scope, bool online) {
    const bool campaign = scope == PlayModeScope::Campaign;
    bool& remembered = campaign ? settings.general.campaignOnline : settings.general.customGameOnline;
    if(remembered == online) return;
    remembered = online;
    INIFile config(getConfigFilepath());
    config.setBoolValue("General", campaign ? "Campaign Online" : "Custom Game Online", online);
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save the Offline/Online choice to the configuration file");
        return;
    }
    WebRuntime::syncPersistentFiles();
}

void showGameLibrary(bool replays) { GameLibrary(replays).showMenu(); }
bool hasRecentGame() { return !recentGame().empty(); }
void continueRecentGame() {
    const auto path = recentGame();
    if(!path.empty()) startSinglePlayerGame(GameInitSettings(path));
    else showGameLibrary();
}

void playCustomGame(bool online) {
    CustomPlaySetup setup;
    setup.maps = maps();
    setup.mods = ModManager::instance().listModChoices();
    setup.rules = effectiveGameOptions;
    setup.online = online;
    // One controller per house is the legible default; advanced sharing stays available.
    setup.sharedHouse = false;
    for(size_t i = 0; i < setup.mods.size(); ++i)
        if(setup.mods[i].matchesSelectionName(ModManager::instance().getActiveModName())) setup.mod = static_cast<int>(i);
    if(setup.maps.empty()) { PlayError(_("No custom maps found. Create a map in Editors > Map Editor, then return here.")).showMenu(); return; }
    bool chooseMap = true;
    for(;;) {
        if(chooseMap) {
            CustomGameMenu browser(false, false, &setup);
            if(browser.showMenu() != MENU_SETUP_PLAYERS) return;
            chooseMap = false;
        }
        auto& mods = ModManager::instance();
        const auto oldMod = mods.getActiveModName();
        if(!setup.mods.empty() && setup.mods[setup.mod].name != oldMod) {
            if(mods.setActiveMod(setup.mods[setup.mod].name)) setup.rules = effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
            else for(size_t i = 0; i < setup.mods.size(); ++i) if(setup.mods[i].name == oldMod) setup.mod = static_cast<int>(i);
        }
        const auto path = setup.maps[setup.map];
        GameInitSettings init(getBasename(path, true), readCompleteFile(path), setup.sharedHouse, setup.rules);
        // A downloaded map's sidecar pins the authored dependency. A changed working map
        // is captured as a new revision instead of silently reusing stale metadata.
        if(existsFile(path + ".workshop.ini")) {
            try {
                const auto selectedMod = mods.getActiveModName();
                if(WorkshopGameContent::applyMapDependency(path, init)) {
                    if(selectedMod != mods.getActiveModName()) {
                        setup.rules = effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
                        init.setGameOptions(setup.rules);
                    }
                    // Keep the setup picker honest after loading the map's exact dependency.
                    const auto active = mods.getActiveModName();
                    auto entry = std::find_if(setup.mods.begin(), setup.mods.end(), [&](const ModInfo& info) { return info.name == active; });
                    if(entry == setup.mods.end()) { setup.mods.push_back(mods.getModInfo(active)); setup.mod = static_cast<int>(setup.mods.size() - 1); }
                    else setup.mod = static_cast<int>(entry - setup.mods.begin());
                }
            } catch(const std::exception& error) { PlayError(error.what()).showMenu(); chooseMap = true; continue; }
        }
        int result;
        {
            CustomGamePlayers menu(init, true, false, &setup);
            result = menu.showMenu();
        }
        if(result == MENU_SETUP_CHANGED) continue;
        if(result == MENU_SETUP_MAP || result == MENU_QUIT_DEFAULT) { chooseMap = true; continue; }
        if(result != MENU_SETUP_HOST) return;
        GameInitSettings networkInit(getBasename(path, true), readCompleteFile(path), settings.general.playerName + "'s custom game", setup.sharedHouse, setup.rules);
        networkInit.setModRevision(init.getModRevisionHash(), init.getModRevisionVersion());
        networkInit.setMapRevision(init.getMapRevisionHash(), init.getMapRevisionVersion(), init.getMapRevisionManifest());
        if(CrossplayMenu(networkInit, setup.publicGame, setup.players, setup.allowJoinAfterStart).showMenu() == MENU_QUIT_GAME_FINISHED) return;
    }
}
