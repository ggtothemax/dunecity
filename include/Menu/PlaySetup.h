#ifndef PLAYSETUP_H
#define PLAYSETUP_H

#include <GameInitSettings.h>
#include <Network/ChangeEventList.h>
#include <mod/ModInfo.h>
#include <string>
#include <vector>

// Local setup only: no connection is opened until the player chooses Create Lobby.
struct CustomPlaySetup {
    std::vector<std::string> maps;
    std::vector<ModInfo> mods;
    int map = 0;
    int mapCategory = 4;
    int mod = 0;
    bool online = false;
    bool publicGame = true;
    bool allowJoinAfterStart = true;
    bool sharedHouse = false;
    SettingsClass::GameOptionsClass rules;
    ChangeEventList players;
};

constexpr int MENU_SETUP_CHANGED = -20;
constexpr int MENU_SETUP_HOST = -21;
constexpr int MENU_SETUP_MAP = -22;
constexpr int MENU_SETUP_PLAYERS = -23;
// The Offline/Online choice is remembered separately for the campaign and for custom
// games; the two menus are entered for different reasons and share no other state.
enum class PlayModeScope { Campaign, CustomGame };
void rememberPlayMode(PlayModeScope scope, bool online);
void playCustomGame(bool online);
void showGameLibrary(bool replays = false);
bool hasRecentGame();
void continueRecentGame();

#endif
