/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/PlaySetup.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>

#include <GUI/Spacer.h>
#include <GUI/GUIStyle.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <GUI/dune/DuneStyle.h>

#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/draw_util.h>
#include <misc/FrameYield.h>
#include <misc/string_util.h>

#include <INIMap/INIMapPreviewCreator.h>
#include <GameInitSettings.h>

#include <globals.h>
#include <main.h>
#include <mod/ModManager.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>


CustomGameMenu::CustomGameMenu(bool multiplayer, bool LANServer, CustomPlaySetup* newSetup)
 : MenuBase(), setup(newSetup), bMultiplayer(multiplayer), bLANServer(LANServer),
   currentGameOptions(newSetup ? newSetup->rules : effectiveGameOptions) {
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    windowWidget.addWidget(&mainVBox, Point(24,23), Point(getRendererWidth() - 48, getRendererHeight() - 32));

    captionLabel.setText(setup ? _("Custom Game — Choose Map") : bMultiplayer ? (bLANServer ? _("LAN Game") : _("Internet Game")) : _("Custom Game"));
    captionLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&captionLabel, 24);
    mainVBox.addWidget(VSpacer::create(24));
    if(setup) {
        connectionChoice.addEntry(_("Offline"));
        connectionChoice.addEntry(_("Online"));
        connectionChoice.setSelectedItem(setup->online ? 1 : 0);
        connectionChoice.setOnSelectionChange([this](bool) {
            const bool online = connectionChoice.getSelectedIndex() == 1;
            visibilityChoice.setVisible(online);
            visibilityChoice.setEnabled(online);
            allowJoinAfterStartCheckbox.setVisible(online);
            allowJoinAfterStartCheckbox.setEnabled(online);
        });
        connectionRow.addWidget(&connectionChoice, 130);
        connectionRow.addWidget(HSpacer::create(8));
        visibilityChoice.addEntry(_("Private - invite code"));
        visibilityChoice.addEntry(_("Public - anyone"));
        visibilityChoice.setSelectedItem(setup->publicGame ? 1 : 0);
        visibilityChoice.setVisible(setup->online);
        visibilityChoice.setEnabled(setup->online);
        connectionRow.addWidget(&visibilityChoice, 180);
        connectionRow.addWidget(Spacer::create());
        mainVBox.addWidget(&connectionRow, 28);
        allowJoinAfterStartCheckbox.setText(_("Allow hot join"));
        allowJoinAfterStartCheckbox.setChecked(setup->allowJoinAfterStart);
        allowJoinAfterStartCheckbox.setVisible(setup->online);
        allowJoinAfterStartCheckbox.setEnabled(setup->online);
        mainVBox.addWidget(&allowJoinAfterStartCheckbox, 24);
    }

    mainVBox.addWidget(Spacer::create(), 0.05);

    mainVBox.addWidget(&mainHBox, 0.80);

    mainHBox.addWidget(Spacer::create(), 0.05);
    mainHBox.addWidget(&leftVBox, 0.8);

    leftVBox.addWidget(&mapTypeButtonsHBox, 24);

    // "All Maps" combines every .ini file from the four standard map
    // directories (SP install, SP user, MP install, MP user). Default
    // tab in multiplayer so the host doesn't have to hunt for maps;
    // also a useful escape hatch in single-player when the user just
    // wants every map at a glance.
    allMapsButton.setText(_("All Maps"));
    allMapsButton.setToggleButton(true);
    allMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 4));
    mapTypeButtonsHBox.addWidget(&allMapsButton);

    singleplayerMapsButton.setText(_("SP Maps"));
    singleplayerMapsButton.setToggleButton(true);
    singleplayerMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 0));
    mapTypeButtonsHBox.addWidget(&singleplayerMapsButton);

    singleplayerUserMapsButton.setText(_("SP User Maps"));
    singleplayerUserMapsButton.setToggleButton(true);
    singleplayerUserMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 1));
    mapTypeButtonsHBox.addWidget(&singleplayerUserMapsButton);

    multiplayerMapsButton.setText(_("MP Maps"));
    multiplayerMapsButton.setToggleButton(true);
    multiplayerMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 2));
    mapTypeButtonsHBox.addWidget(&multiplayerMapsButton);

    multiplayerUserMapsButton.setText(_("MP User Maps"));
    multiplayerUserMapsButton.setToggleButton(true);
    multiplayerUserMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 3));
    mapTypeButtonsHBox.addWidget(&multiplayerUserMapsButton);

    dummyButton.setEnabled(false);
    mapTypeButtonsHBox.addWidget(&dummyButton, 17);
    mapList.setAutohideScrollbar(false);
    mapList.setOnSelectionChange(std::bind(&CustomGameMenu::onMapListSelectionChange, this, std::placeholders::_1));
    mapList.setOnDoubleClick(std::bind(&CustomGameMenu::onNext, this));
    leftVBox.addWidget(&mapList, 0.95);

    leftVBox.addWidget(VSpacer::create(10));

    multiplePlayersPerHouseCheckbox.setText(setup ? _("Shared house") : _("Multiple players per house"));
    multiplePlayersPerHouseCheckbox.setChecked(setup ? setup->sharedHouse : settings.general.multiplePlayersPerHouse);
    multiplePlayersPerHouseCheckbox.setOnClick(std::bind(&CustomGameMenu::onMultiplePlayersPerHouseChange, this));
    optionsHBox.addWidget(&multiplePlayersPerHouseCheckbox);
    optionsHBox.addWidget(Spacer::create());
    gameOptionsButton.setText(setup ? _("Game Rules") : _("Game Options..."));
    gameOptionsButton.setOnClick(std::bind(&CustomGameMenu::onGameOptions, this));
    optionsHBox.addWidget(&gameOptionsButton, 140);

    leftVBox.addWidget(Spacer::create(), 0.05);

    leftVBox.addWidget(&optionsHBox, 0.05);

    mainHBox.addWidget(HSpacer::create(8));
    mainHBox.addWidget(Spacer::create(), 0.05);

    mainHBox.addWidget(&rightVBox, 180);
    mainHBox.addWidget(Spacer::create(), 0.05);
    minimap.setSurface( GUIStyle::getInstance().createButtonSurface(130,130,_("Choose map"), true, false) );
    rightVBox.addWidget(&minimap);

    rightVBox.addWidget(VSpacer::create(10));
    rightVBox.addWidget(&mapPropertiesHBox, 0.01);
    mapPropertiesHBox.addWidget(&mapPropertyNamesVBox, 75);
    mapPropertiesHBox.addWidget(&mapPropertyValuesVBox, 105);
    mapPropertyNamesVBox.addWidget(Label::create(_("Size") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertySize);
    mapPropertyNamesVBox.addWidget(Label::create(_("Players") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyPlayers);
    mapPropertyNamesVBox.addWidget(Label::create(_("Author") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyAuthors);
    mapPropertyNamesVBox.addWidget(Label::create(_("License") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyLicense);
    
    rightVBox.addWidget(VSpacer::create(15));
    
    // Mod selection
    rightVBox.addWidget(&modHBox, 25);
    modLabel.setText(_("Mod:"));
    modHBox.addWidget(&modLabel, 40);
    modHBox.addWidget(HSpacer::create(5));
    modHBox.addWidget(&modDropDown, 130);
    
    // Populate mod dropdown
    availableMods = ModManager::instance().listMods();
    std::string activeModName = setup && !setup->mods.empty() ? setup->mods[setup->mod].name : ModManager::instance().getActiveModName();
    int activeIndex = 0;
    for (size_t i = 0; i < availableMods.size(); i++) {
        modDropDown.addEntry(availableMods[i].displayName);
        if (availableMods[i].name == activeModName) {
            activeIndex = static_cast<int>(i);
        }
    }
    if (!availableMods.empty()) {
        modDropDown.setSelectedItem(activeIndex);
    }
    if(setup) modDropDown.setOnSelectionChange([this](bool interactive) {
        const int choice = modDropDown.getSelectedIndex();
        if(!interactive || choice < 0 || choice >= static_cast<int>(availableMods.size())) return;
        auto& manager = ModManager::instance();
        const auto previous = manager.getActiveModName();
        if(previous == availableMods[choice].name) return;
        if(manager.setActiveMod(availableMods[choice].name)) {
            currentGameOptions = effectiveGameOptions = manager.loadEffectiveGameOptions(settings.gameOptions);
        } else {
            for(size_t i = 0; i < availableMods.size(); ++i)
                if(availableMods[i].name == previous) modDropDown.setSelectedItem(static_cast<int>(i));
        }
    });
    
    rightVBox.addWidget(Spacer::create());

    mainVBox.addWidget(Spacer::create(), 0.05);

    mainVBox.addWidget(VSpacer::create(20));
    mainVBox.addWidget(&buttonHBox, 24);
    mainVBox.addWidget(VSpacer::create(14), 0.0);

    buttonHBox.addWidget(HSpacer::create(70));
    cancelButton.setText(_("Back"));
    cancelButton.setOnClick(std::bind(&CustomGameMenu::onCancel, this));
    buttonHBox.addWidget(&cancelButton, 0.1);

    buttonHBox.addWidget(Spacer::create(), 0.0625);

    buttonHBox.addWidget(Spacer::create(), 0.25);
    loadButton.setText(_("Load"));
    loadButton.setVisible(bMultiplayer && !setup);
    loadButton.setEnabled(bMultiplayer && !setup);
    loadButton.setOnClick(std::bind(&CustomGameMenu::onLoad, this));
    buttonHBox.addWidget(&loadButton, 0.175);
    buttonHBox.addWidget(Spacer::create(), 0.25);

    buttonHBox.addWidget(Spacer::create(), 0.0625);

    nextButton.setText(setup ? _("Players") : _("Next"));
    nextButton.setOnClick(std::bind(&CustomGameMenu::onNext, this));
    buttonHBox.addWidget(&nextButton, 0.1);
    buttonHBox.addWidget(HSpacer::create(90));

    // Default tab: "All Maps" in multiplayer (host gets every option),
    // "SP Maps" in single-player (matches the original behaviour).
    onMapTypeChange(setup ? setup->mapCategory : bMultiplayer ? 4 : 0);
    if(setup && !setup->maps.empty()) {
        const auto& selected = setup->maps[setup->map];
        auto restoreSelection = [&]() {
            for(int i = 0; i < mapList.getNumEntries(); ++i) {
                const auto& dir = currentMapDirectory.empty() ? mapEntryDirectories_[i] : currentMapDirectory;
                if(strToLower(dir + mapList.getEntry(i) + ".ini") == strToLower(selected)) {
                    mapList.setSelectedItem(i);
                    return true;
                }
            }
            return false;
        };
        if(!restoreSelection()) { onMapTypeChange(4); restoreSelection(); }
    }
}

CustomGameMenu::~CustomGameMenu()
{
    ;
}


void CustomGameMenu::onChildWindowClose(Window* pChildWindow) {
    LoadSaveWindow* pLoadSaveWindow = dynamic_cast<LoadSaveWindow*>(pChildWindow);
    if(pLoadSaveWindow != nullptr) {
        std::string filename = pLoadSaveWindow->getFilename();

        if(filename != "") {
            std::string savegamedata = readCompleteFile(filename);

            std::string servername = settings.general.playerName + "'s Game";
            GameInitSettings gameInitSettings(getBasename(filename, true), savegamedata, servername);

            int ret = CustomGamePlayers(gameInitSettings, true, bLANServer).showMenu();
            if(ret != MENU_QUIT_DEFAULT) {
                quit(ret);
            }
        }
    }

    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        currentGameOptions = pGameOptionsWindow->getGameOptions();
        // Choices made here become the new defaults, the same as in Options.
        // Game Rules only persists defaults when the player requests it.
    }
}

void CustomGameMenu::onMultiplePlayersPerHouseChange() {
    if(setup) return; // Local setup is committed with Players, not as a global preference.
    // Remember the choice across games and restarts.
    settings.general.multiplePlayersPerHouse = multiplePlayersPerHouseCheckbox.isChecked();
    INIFile config(getConfigFilepath());
    config.setBoolValue("General", "Multiple Players Per House", settings.general.multiplePlayersPerHouse);
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save 'Multiple Players Per House' to the configuration file");
    }
}

void CustomGameMenu::onNext()
{
    if(mapList.getSelectedIndex() < 0) {
        return;
    }

    if(setup) {
        auto path = getSelectedMapPath();
        getCaseInsensitiveFilename(path);
        const auto selected = std::find(setup->maps.begin(), setup->maps.end(), path);
        if(selected == setup->maps.end()) return;
        const int mapIndex = static_cast<int>(selected - setup->maps.begin());
        int selectedMod = setup->mod;
        const int choice = modDropDown.getSelectedIndex();
        if(choice >= 0 && choice < static_cast<int>(availableMods.size())) {
            for(size_t i = 0; i < setup->mods.size(); ++i)
                if(setup->mods[i].name == availableMods[choice].name) selectedMod = static_cast<int>(i);
        }
        if(mapIndex != setup->map || selectedMod != setup->mod) setup->players = ChangeEventList{};
        setup->map = mapIndex;
        setup->mod = selectedMod;
        setup->online = connectionChoice.getSelectedIndex() == 1;
        setup->publicGame = visibilityChoice.getSelectedIndex() == 1;
        setup->allowJoinAfterStart = allowJoinAfterStartCheckbox.isChecked();
        setup->sharedHouse = multiplePlayersPerHouseCheckbox.isChecked();
        setup->rules = currentGameOptions;
        quit(MENU_SETUP_PLAYERS);
        return;
    }

    // Activate selected mod
    int modIndex = modDropDown.getSelectedIndex();
    if (modIndex >= 0 && modIndex < static_cast<int>(availableMods.size())) {
        ModManager::instance().setActiveMod(availableMods[modIndex].name);
        // Reload effective game options with new mod
        effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    }

    std::string mapFilename = getSelectedMapPath();
    getCaseInsensitiveFilename(mapFilename);

    GameInitSettings gameInitSettings;
    if(bMultiplayer) {
        std::string servername = settings.general.playerName + "'s Game";
        gameInitSettings = GameInitSettings(getBasename(mapFilename, true), readCompleteFile(mapFilename), servername, multiplePlayersPerHouseCheckbox.isChecked(), currentGameOptions);
    } else {
        gameInitSettings = GameInitSettings(getBasename(mapFilename, true), readCompleteFile(mapFilename), multiplePlayersPerHouseCheckbox.isChecked(), currentGameOptions);
    }

#ifdef __EMSCRIPTEN__
    // Browser build: the lobby-creation constructor below is a long
    // synchronous block (map parse + widget build + signaling room setup).
    // Let queued input and signaling callbacks run before it starts.
    yieldFrameToBrowser();
#endif

    int ret = CustomGamePlayers(gameInitSettings, true, bLANServer).showMenu();
    if(ret != MENU_QUIT_DEFAULT) {
        quit(ret);
    }
}

void CustomGameMenu::onCancel()
{
    quit();
}

void CustomGameMenu::onLoad()
{
    char tmp[FILENAME_MAX];
    fnkdat("mpsave/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string savepath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Game"), savepath, "dls"));
}

void CustomGameMenu::onGameOptions()
{
    openWindow(GameOptionsWindow::create(currentGameOptions));
}

std::string CustomGameMenu::getSelectedMapPath() const
{
    int idx = mapList.getSelectedIndex();
    if (idx < 0) return "";

    const std::string entry = mapList.getEntry(idx);
    if (currentMapDirectory.empty()) {
        // "All Maps" mode: per-entry source directory.
        if (idx >= static_cast<int>(mapEntryDirectories_.size())) return "";
        return mapEntryDirectories_[idx] + entry + ".ini";
    }
    return currentMapDirectory + entry + ".ini";
}

void CustomGameMenu::onMapTypeChange(int buttonID)
{
    if(setup) setup->mapCategory = buttonID;
    allMapsButton           .setToggleState(buttonID == 4);
    singleplayerMapsButton  .setToggleState(buttonID == 0);
    singleplayerUserMapsButton.setToggleState(buttonID == 1);
    multiplayerMapsButton   .setToggleState(buttonID == 2);
    multiplayerUserMapsButton.setToggleState(buttonID == 3);

    // Resolve the four standard map directories once; we either pick
    // one (single-directory mode) or scan them all (mode 4).
    std::string spDataDir   = getDuneLegacyDataDir() + "/maps/singleplayer/";
    std::string mpDataDir   = getDuneLegacyDataDir() + "/maps/multiplayer/";
    if(getFileNamesList(spDataDir, "ini", true, FileListOrder_Name_Asc).empty()) {
        spDataDir = getDuneLegacyDataDir() + "/data/maps/singleplayer/";
    }
    if(getFileNamesList(mpDataDir, "ini", true, FileListOrder_Name_Asc).empty()) {
        mpDataDir = getDuneLegacyDataDir() + "/data/maps/multiplayer/";
    }
    std::string spUserDir;
    std::string mpUserDir;
    {
        char tmp[FILENAME_MAX];
        if (fnkdat("maps/singleplayer/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0) {
            spUserDir = tmp;
        }
        if (fnkdat("maps/multiplayer/",  tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0) {
            mpUserDir = tmp;
        }
    }

    mapList.clearAllEntries();
    mapEntryDirectories_.clear();

    if (buttonID == 4) {
        // "All Maps": scan every directory and remember the source per
        // entry so onNext / preview can resolve the full path.
        currentMapDirectory.clear();

        struct Source { const std::string* dir; };
        const std::array<const std::string*, 4> sources = {
            &spDataDir, &spUserDir, &mpDataDir, &mpUserDir
        };

        // Collect (entryName, sourceDir) so we can sort once; the per-
        // directory sort wouldn't interleave the sources alphabetically.
        std::vector<std::pair<std::string, std::string>> all;
        for (const auto* src : sources) {
            if (!src || src->empty()) continue;
            for (const std::string& file :
                 getFileNamesList(*src, "ini", true,
                                  FileListOrder_Name_CaseInsensitive_Asc)) {
                std::string base = file.substr(0, file.length() - 4);
                all.emplace_back(std::move(base), *src);
            }
        }
        // Portable case-insensitive sort: lowercase both keys and
        // compare. strcasecmp is POSIX-only and MSVC's equivalent is
        // _stricmp — using strToLower (already in misc/string_util.h)
        // keeps a single code path across Windows / macOS / Linux.
        std::sort(all.begin(), all.end(),
                  [](const auto& a, const auto& b) {
                      return strToLower(a.first) < strToLower(b.first);
                  });
        for (const auto& [name, dir] : all) {
            mapList.addEntry(name);
            mapEntryDirectories_.push_back(dir);
#ifdef __EMSCRIPTEN__
            // Browser build: "All Maps" fills the list from every map
            // directory inside one input handler; yield periodically so the
            // page stays responsive while the list builds.
            if(mapList.getNumEntries() % 32 == 0) {
                yieldFrameToBrowser();
            }
#endif
        }
    } else {
        switch(buttonID) {
            case 0: currentMapDirectory = spDataDir; break;
            case 1: currentMapDirectory = spUserDir; break;
            case 2: currentMapDirectory = mpDataDir; break;
            case 3: currentMapDirectory = mpUserDir; break;
        }

        for(const std::string& file : getFileNamesList(currentMapDirectory, "ini", true, FileListOrder_Name_CaseInsensitive_Asc)) {
            mapList.addEntry(file.substr(0, file.length() - 4));
#ifdef __EMSCRIPTEN__
            // Browser build: same paced list build as the "All Maps" tab.
            if(mapList.getNumEntries() % 32 == 0) {
                yieldFrameToBrowser();
            }
#endif
        }
    }

    nextButton.setEnabled(mapList.getNumEntries() > 0);
    if(mapList.getNumEntries() > 0) {
        mapList.setSelectedItem(0);
    } else {
        minimap.setSurface( GUIStyle::getInstance().createButtonSurface(130,130,_("No map available"), true, false) );
        mapPropertySize.setText("");
        mapPropertyPlayers.setText("");
        mapPropertyAuthors.setText("");
        mapPropertyLicense.setText("");
    }
}

void CustomGameMenu::onMapListSelectionChange(bool bInteractive)
{
    nextButton.setEnabled(true);

    if(mapList.getSelectedIndex() < 0) {
        return;
    }

    std::string mapFilename = getSelectedMapPath();
    getCaseInsensitiveFilename(mapFilename);

    INIFile inimap(mapFilename);

#ifdef __EMSCRIPTEN__
    // Browser build: the INI parse above is a long synchronous block inside
    // the selection-change handler; hand the browser a slice before the
    // (also yielding) minimap render so clicks and signaling aren't queued
    // behind the whole parse+render.
    yieldFrameToBrowser();
#endif

    int sizeX = 0;
    int sizeY = 0;

    if(inimap.hasKey("MAP","Seed")) {
        // old map format with seed value
        int mapscale = inimap.getIntValue("BASIC", "MapScale", -1);

        switch(mapscale) {
            case 0: {
                sizeX = 62;
                sizeY = 62;
            } break;

            case 1: {
                sizeX = 32;
                sizeY = 32;
            } break;

            case 2: {
                sizeX = 21;
                sizeY = 21;
            } break;

            default: {
                sizeX = 64;
                sizeY = 64;
            }
        }
    } else {
        // new map format with saved map
        sizeX = inimap.getIntValue("MAP","SizeX", 0);
        sizeY = inimap.getIntValue("MAP","SizeY", 0);
    }

    mapPropertySize.setText(std::to_string(sizeX) + " x " + std::to_string(sizeY));

    sdl2::surface_ptr pMapSurface = nullptr;
    try {
        INIMapPreviewCreator mapPreviewCreator(&inimap);
        pMapSurface = mapPreviewCreator.createMinimapImageOfMap(1, DuneStyle::buttonBorderColor);
    } catch(...) {
        pMapSurface = sdl2::surface_ptr{ GUIStyle::getInstance().createButtonSurface(130, 130, "Error", true, false) };
        loadButton.setEnabled(false);
    }
    minimap.setSurface(std::move(pMapSurface) );

    int numPlayers = 0;
    if(inimap.hasSection("Atreides")) numPlayers++;
    if(inimap.hasSection("Ordos")) numPlayers++;
    if(inimap.hasSection("Harkonnen")) numPlayers++;
    if(inimap.hasSection("Fremen")) numPlayers++;
    if(inimap.hasSection("Mercenary")) numPlayers++;
    if(inimap.hasSection("Sardaukar")) numPlayers++;
    if(inimap.hasSection("Player1")) numPlayers++;
    if(inimap.hasSection("Player2")) numPlayers++;
    if(inimap.hasSection("Player3")) numPlayers++;
    if(inimap.hasSection("Player4")) numPlayers++;
    if(inimap.hasSection("Player5")) numPlayers++;
    if(inimap.hasSection("Player6")) numPlayers++;

    mapPropertyPlayers.setText(std::to_string(numPlayers));



    std::string authors = inimap.getStringValue("BASIC","Author", "-");
    if(authors.size() > 11) {
        authors = authors.substr(0,9) + "...";
    }
    mapPropertyAuthors.setText(authors);


    mapPropertyLicense.setText(inimap.getStringValue("BASIC","License", "-"));

}
