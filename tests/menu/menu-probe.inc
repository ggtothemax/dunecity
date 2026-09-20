#include <Network/NetworkManager.h>
// Test-only entry point, linked against the real menu objects by run-menu-probe.py.
#include <Menu/HouseChoiceMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/AboutMenu.h>
#include <Menu/HowToPlayMenu.h>
#include <Menu/ModMenu.h>
#include <Menu/DisplayMenu.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/dune/JoinProgressWindow.h>
#include <GUI/MsgBox.h>
#include <GUI/QstBox.h>
#include <Menu/PlaySetup.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <FileClasses/LoadSavePNG.h>
#include <players/PlayerFactory.h>

void menuRequire(bool value, const char* message) {
    if(!value) { SDL_Log("MENU_PROBE_FAIL: %s", message); throw std::runtime_error(message); }
}
void menuCapture(Window& menu, const std::string& name) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    menu.draw();
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    auto surface = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888));
    menuRequire(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch)==0,"Capture failed");
    const auto path = std::string(std::getenv("MENU_PROBE_OUT"))+"/"+std::getenv("MENU_PROBE_WIDTH")+"-"+name+".bmp";
    menuRequire(SDL_SaveBMP(surface.get(),path.c_str())==0,"Screenshot write failed");
}
int runMenuProbe() {
    menuRequire(getRendererWidth()==std::atoi(std::getenv("MENU_PROBE_WIDTH"))
                && getRendererHeight()==std::atoi(std::getenv("MENU_PROBE_HEIGHT")),
                "Probe did not render at the requested dimensions");
    settings.general.playerName = "Menu tester";
    settings.network.relayUseDevelopmentEndpoint = true;
    // Disable online discovery in this fixture; admission and transport have separate tests.
    settings.network.directDevelopmentEndpoint = "";
    const auto installedSkin = std::filesystem::path(ModManager::instance().getModPath("dunecity"))
        / "graphics_skins" / "Dune2" / "zones" / "dunecity_atreides_residential_zone" / "zone.ini";
    menuRequire(std::filesystem::is_regular_file(installedSkin),
                "Bundled DuneCity skin was not installed into the fresh profile");
    ModManager::instance().setActiveMod("vanilla");
    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    {
        HouseChoiceMenu menu(false);
        for(auto* dropdown : {&menu.modDropDown, &menu.enemyAIDropDown,
                              &menu.supportBotDropDown, &menu.startLevelDropDown}) {
            dropdown->onOpenListBoxButton();
            menuCapture(menu, dropdown == &menu.modDropDown ? "campaign-mod-open" : "campaign-options-open");
            const auto* geometry = menu.windowWidget.getWidgetDataFromWidget(dropdown);
            const int top = geometry->position.y + (dropdown->bListBoxAbove
                ? -dropdown->listBox.getSize().y : dropdown->getSize().y);
            menuRequire(top >= 0 && top + dropdown->listBox.getSize().y <= menu.getSize().y,
                        "Campaign dropdown extends outside the menu clip rectangle");
            dropdown->onOpenListBoxButton();
        }
        // A long list must shrink to whole rows, expose its scrollbar, and
        // still accept a click on the last item after wheel scrolling.
        auto& dropdown = menu.supportBotDropDown;
        dropdown.setNumVisibleEntries(40);
        for(int i = dropdown.getNumEntries(); i < 40; ++i)
            dropdown.addEntry("Extra partner " + std::to_string(i), i);
        dropdown.onOpenListBoxButton();
        menuCapture(menu, "campaign-scrolling-options");
        auto& list = dropdown.listBox;
        menuRequire(list.isScrollbarVisible(), "Constrained dropdown has no scrollbar");
        const auto* geometry = menu.windowWidget.getWidgetDataFromWidget(&dropdown);
        const int top = geometry->position.y + (dropdown.bListBoxAbove
            ? -list.getSize().y : dropdown.getSize().y);
        menuRequire(top >= 0 && top + list.getSize().y <= menu.getSize().y,
                    "Long campaign dropdown extends outside the menu");
        const int entryHeight = GUIStyle::getInstance().getListBoxEntryHeight();
        menuRequire((list.getSize().y - 2) % entryHeight == 0, "Dropdown clips a partial row");
        const int x = menu.getPosition().x + geometry->position.x + 10;
        const int y = menu.getPosition().y + top + 3;
        for(int i = 0; i < 40; ++i) menu.handleMouseWheel(x, y, false);
        const int lastRow = 39 - list.firstVisibleElement;
        menuRequire(lastRow >= 0 && (lastRow + 1) * entryHeight <= list.getSize().y - 2,
                    "Wheel scrolling cannot reach the last dropdown entry");
        menu.handleMouseLeft(x, y + lastRow * entryHeight, true);
        menuRequire(dropdown.getSelectedIndex() == 39 && !dropdown.bShowListBox,
                    "Last visible dropdown entry cannot be selected through the menu");
        dropdown.setSelectedItem(0);
    }
    {
        HouseChoiceMenu menu(false);
        menuRequire(HouseChoiceMenu::isPublicGame(),"Campaign should default to public");
        menu.supportBotDropDown.setSelectedItem(4);
        menuRequire(HouseChoiceMenu::getSupportBotIndex()==4,"Support setup fixture failed");
        auto& list=menu.supportBotDropDown.listBox;
        // A rapid second selection must notify the campaign setup even when
        // it falls within the list widget's double-click interval.
        list.scrollbar.setCurrentValue(3);
        menu.supportBotDropDown.onOpenListBoxButton();
        menuCapture(menu,"ai-partner-selection");
        list.lastClickTime=SDL_GetTicks();
        const int row=8-list.firstVisibleElement;
        const int rowY=1+row*GUIStyle::getInstance().getListBoxEntryHeight()+2;
        const int overlayY=rowY+(menu.supportBotDropDown.bListBoxAbove
            ? -list.getSize().y : menu.supportBotDropDown.getSize().y);
        menu.supportBotDropDown.handleMouseLeftOverlay(10,overlayY,true);
        menuRequire(menu.supportBotDropDown.getSelectedEntryIntData()==8,"Full AI selection fixture missed its row");
        menuRequire(HouseChoiceMenu::getSupportBotIndex()==8,"Full AI selection left campaign launch set to economy-only Support");
        menuRequire(!menu.supportBotDropDown.bShowListBox,"AI dropdown did not close after selection");
        for(int option=0;option<10;++option) {
            menu.supportBotDropDown.setSelectedItem(option);
            menuRequire(HouseChoiceMenu::getSupportBotIndex()==option,"AI partner choice mapping changed");
        }
        menu.supportBotDropDown.setSelectedItem(8);
        menu.enemyAIDropDown.setSelectedItem(2);
        menu.startLevelDropDown.setSelectedItem(6);
        // A stale cached callback result must never override the live widgets
        // when the player presses Start.
        HouseChoiceMenu::s_supportBotIndex=4;
        HouseChoiceMenu::s_enemyAIIndex=0;
        HouseChoiceMenu::s_startLevel=1;
        menu.hostCoopButton.handleMouseLeft(10,10,true);
        menu.hostCoopButton.handleMouseLeft(10,10,false);
        menuRequire(menu.isQuiting() && HouseChoiceMenu::getSupportBotIndex()==8
            && HouseChoiceMenu::getEnemyAIIndex()==2 && HouseChoiceMenu::getStartLevel()==7,
            "Start launched stale AI or mission choices");
    }
    {
        ListBox list;
        list.addEntry("First");list.addEntry("Second");
        list.resize(200,100);
        int selections=0,doubleClicks=0;
        list.setOnSelectionChange([&](bool){++selections;});
        list.setOnDoubleClick([&](){++doubleClicks;});
        list.lastClickTime=SDL_GetTicks();
        list.handleMouseLeft(10,3,true);
        menuRequire(selections==1 && doubleClicks==0,"First row click incorrectly counted as a double click");
        list.lastClickTime=SDL_GetTicks();
        list.handleMouseLeft(10,GUIStyle::getInstance().getListBoxEntryHeight()+3,true);
        menuRequire(selections==2 && doubleClicks==0,"Rapid different-row selection triggered double click");
        list.lastClickTime=SDL_GetTicks();
        list.handleMouseLeft(10,GUIStyle::getInstance().getListBoxEntryHeight()+3,true);
        menuRequire(selections==2 && doubleClicks==1,"Same-row double click stopped activating the row");
    }
    {
        MainMenu menu;
        menuRequire(menu.campaignButton.isVisible() && menu.customButton.isVisible() && menu.onlineButton.isVisible(), "Home play actions missing");
        menuRequire(!menu.displayButton.isVisible(),"Duplicate Display entry remains");
        menuRequire(!menu.campaignButton.keyboardFocusVisible,"Home shows unsolicited keyboard focus");
        menuCapture(menu,"home");
        menu.canContinue=true; menu.refreshContextButtons();
        menuCapture(menu,"home-continue");
        menu.canContinue=false; menu.refreshContextButtons();
        SDL_Event tab{}; tab.type=SDL_KEYDOWN; tab.key.keysym.sym=SDLK_TAB;
        std::vector<TextButton*> tabOrder{&menu.campaignButton,&menu.customButton,&menu.loadButton,&menu.optionsButton,&menu.modesButton,&menu.quitButton};
#if defined(DUNECITY_DESKTOP_UPDATER)
        tabOrder.push_back(&menu.updateButton);
#endif
        tabOrder.push_back(&menu.onlineButton);
        for(auto* expected : tabOrder) {
            menu.handleInput(tab);
            menuRequire(menu.windowWidget.pActiveChildWidget==expected,"Home Tab order reaches a hidden destination or disagrees with screen order");
            menuRequire(expected->keyboardFocusVisible,"Keyboard navigation lost its visible focus");
        }
        menuCapture(menu,"home-keyboard");
        SDL_Event motion{}; motion.type=SDL_MOUSEMOTION; motion.motion.x=2; motion.motion.y=2;
        menu.handleInput(motion);
        menuRequire(!menu.campaignButton.keyboardFocusVisible,"Pointer movement left Campaign highlighted");
        menuRequire(!menu.campaignButton.bHover,"Pointer outside menu left Campaign hovered");
        menu.campaignButton.handleMouseMovement(5,5,false);
        menuRequire(menu.campaignButton.bHover,"Campaign no longer responds to hover");
        menu.campaignButton.handleMouseMovement(-1,-1,false);
        menuRequire(!menu.campaignButton.bHover,"Campaign hover stuck after pointer left");
        menuCapture(menu,"home-pointer");
#if defined(DUNECITY_DESKTOP_UPDATER)
        auto& updater = DesktopUpdater::instance();
        updater.state_ = DesktopUpdater::State::Available;
        updater.manifest_.version = "9.0.0";
        menu.bFirstLaunchPromptChecked = true;
        menu.update();
        menuRequire(menu.updateButton.getText()=="Update available", "Update notice missing");
        menu.onUpdate();
        auto* question = dynamic_cast<QstBox*>(menu.pChildWindow);
        menuRequire(question && menu.updatePromptOpen, "Update confirmation missing");
        menuCapture(menu,"update-confirmation");
        question->onButton(QSTBOX_BUTTON2);
        menu.processChildWindowOpenCloses();
        menuRequire(!menu.updatePromptOpen && !updater.busy(), "Later started an installation");
        updater.state_ = DesktopUpdater::State::Installing;
        auto* active = menu.windowWidget.pActiveChildWidget;
        menu.handleInput(tab);
        menuRequire(menu.windowWidget.pActiveChildWidget == active, "Installation allowed game-entry navigation");
        updater.state_ = DesktopUpdater::State::Current;
#endif

    }
    {
        HouseChoiceMenu menu;
        menuRequire(HouseChoiceMenu::isOnline(), "Campaign should default to online");
        menuRequire(HouseChoiceMenu::isPublicGame(), "Campaign should default to public");
        menuCapture(menu,"campaign-default");
        menu.connectionDropDown.setSelectedItem(0);
        menuRequire(!HouseChoiceMenu::isOnline(), "Campaign cannot be switched offline");
        menuRequire(!menu.visibilityDropDown.isEnabled() && menu.supportBotDropDown.isEnabled(),
                    "Offline campaign controls did not update");
        menu.onHouseButton(1);
        menuRequire(!menu.isQuiting(),"Selecting a house starts the game");
        HouseChoiceMenu::s_startLevel = 7;
        HouseChoiceMenu::s_singleMission = false;
        menu.populateLevels();
        menuRequire(menu.startLevelDropDown.getNumEntries()==9,"Campaign level range wrong");
        menuCapture(menu,"campaign-offline");
        HouseChoiceMenu::s_currentGameOptions.fogOfWar = !effectiveGameOptions.fogOfWar;
    }
    {
        HouseChoiceMenu menu(true,true);
        menuRequire(HouseChoiceMenu::getStartLevel()==7,"Retry lost campaign level");
        menuRequire(HouseChoiceMenu::getGameOptions().fogOfWar != effectiveGameOptions.fogOfWar,"Retry lost game rules");
        menuRequire(!menu.supportBotDropDown.isEnabled(),"Online campaign AI selector should defer to lobby");
        menuCapture(menu,"campaign-online");
        HouseChoiceMenu::s_singleMission = true;
        menu.populateLevels();
        menuRequire(menu.startLevelDropDown.getNumEntries()==22,"Single mission range wrong");
    }
    CustomPlaySetup setup;
    auto files = getFileNamesList(getDuneLegacyDataDir()+"/maps/multiplayer/","ini",true);
    auto dir = getDuneLegacyDataDir()+"/maps/multiplayer/";
    if(files.empty()) { dir = getDuneLegacyDataDir()+"/data/maps/multiplayer/"; files=getFileNamesList(dir,"ini",true); }
    menuRequire(!files.empty(),"No bundled custom maps");
    for(const auto& file : files) setup.maps.push_back(dir+file);
    setup.mods = ModManager::instance().listMods();
    for(size_t i=0;i<setup.mods.size();++i) if(setup.mods[i].name=="vanilla") setup.mod=i;
    setup.rules = effectiveGameOptions;
    for(bool online : {false, true}) {
        auto mapSetup = setup;
        mapSetup.online = online;
        CustomGameMenu browser(false, false, &mapSetup);
        menuRequire(browser.mapList.getNumEntries() > 10, "Original scrolling map browser is missing");
        menuRequire(browser.getSelectedMapPath() == setup.maps[setup.map], "Map browser lost selected map");
        menuRequire(browser.mainVBox.getMinimumSize().x <= browser.mainVBox.getSize().x, "Map browser controls exceed menu width");
        menuRequire(browser.mainVBox.getMinimumSize().y <= browser.mainVBox.getSize().y, "Map browser controls exceed menu height");
        menuRequire(browser.allowJoinAfterStartCheckbox.isChecked(),"Late join must be enabled by default");
        menuRequire(browser.allowJoinAfterStartCheckbox.isVisible()==online,"Late join setting is shown for an offline game");
        menuCapture(browser, online ? "map-browser-online" : "map-browser-offline");
        browser.allowJoinAfterStartCheckbox.setChecked(false);
        browser.onNext();
        menuRequire(browser.retVal == MENU_SETUP_PLAYERS, "Map selection did not proceed to players");
        menuRequire(!mapSetup.allowJoinAfterStart,"Late join opt-out was lost");
        menuRequire(mapSetup.online == online && mapSetup.map == setup.map, "Map browser changed mode or map");
    }
    const auto map = setup.maps.front();
    GameInitSettings init(getBasename(map,true),readCompleteFile(map),false,setup.rules);
    {
        // This shipped map has Player1, Player2, Player3 and Player5. The
        // fourth visible row still needs a real team before a match starts.
        const auto sparse = dir + "4P - 128x128 - Ergsun-Odenkirk.ini";
        auto sparseSetup = setup;
        sparseSetup.online = true;
        sparseSetup.sharedHouse = true;
        sparseSetup.players = ChangeEventList{};
        GameInitSettings sparseInit(getBasename(sparse,true),readCompleteFile(sparse),true,setup.rules);
        CustomGamePlayers menu(sparseInit,true,false,&sparseSetup);
        menuRequire(menu.numHouses == 4,"Sparse four-player map fixture changed");
        for(int slot=0;slot<menu.numHouses;++slot)
            menuRequire(menu.houseInfo[slot].teamDropDown.getSelectedEntryIntData()==slot+1,
                "Sparse PlayerN sections left an unassigned team that becomes 255 in a checkpoint");
        menuCapture(menu,"custom-sparse-teams");
    }
    {
        GameInitSettings coop(HOUSE_ATREIDES, setup.rules, 1);
        coop.enableCoop(true, "Keyboard co-op");
        auto scenario = pFileManager->openCampaignFile(coop.getFilename());
        std::string scenarioData(static_cast<size_t>(SDL_RWsize(scenario.get())), '\0');
        SDL_RWread(scenario.get(), scenarioData.data(), 1, scenarioData.size());
        coop.setScenarioData(scenarioData);
        CustomGamePlayers menu(coop,true);
        SDL_Event tab{}; tab.type=SDL_KEYDOWN; tab.key.keysym.sym=SDLK_TAB;
        // The host can start alone, with the second controller still open for a later join.
        menu.update();
        menuRequire(menu.nextButton.isEnabled(), "Campaign requires a partner before starting");
        menu.addAllPlayersToGameInitSettings();
        for(const auto& house : menu.gameInitSettings.getHouseInfoList()) {
            if(house.houseID == HOUSE_ATREIDES)
                menuRequire(house.playerInfoList.size() == 1, "Solo campaign added a phantom partner");
        }
        menuCapture(menu,"campaign-solo-lobby");
        menu.setPlayer2Slot("Keyboard partner",1);
        menu.update();
        bool reachedStart=false;
        for(int i=0;i<40;++i) { menu.handleInput(tab); if(menu.nextButton.isActive()) { SDL_Log("MENU_PROBE: campaign Start reached after %d Tabs",i+1); reachedStart=true; break; } }
        menuRequire(reachedStart,"Campaign lobby keyboard focus cannot reach Start after a partner joins");
        menuCapture(menu,"campaign-lobby");
    }
    ChangeEventList original;
    {
        CustomGamePlayers menu(init,true,false,&setup);
        original=menu.getChangeEventList();
        menuRequire(menu.numHouses>=2,"Fixture needs two houses");
        menu.update();
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Custom controls exceed menu width");
        menuCapture(menu,"custom-offline");
        menu.onCancel();
        menuRequire(menu.retVal == MENU_SETUP_MAP,"Back did not return to map browser");
        menuRequire(!setup.players.changeEventList.empty(),"Custom setup lost roster");
    }
    {
        auto roundTrip = setup;
        CustomGameMenu browser(false, false, &roundTrip);
        browser.onNext();
        menuRequire(roundTrip.players.changeEventList.size() == original.changeEventList.size(), "Returning through map browser lost the roster");
        menuRequire(roundTrip.map == setup.map, "Returning through map browser changed map");
    }
    setup.online=true;
    {
        CustomGamePlayers menu(init,true,false,&setup);
        auto restored=menu.getChangeEventList();
        menuRequire(restored.changeEventList.size()==original.changeEventList.size(),"Roster shape changed across online toggle");
        auto restoredEvent=restored.changeEventList.begin();
        for(const auto& a : original.changeEventList) {
            const auto& b=*restoredEvent++;
            menuRequire(a.eventType==b.eventType && a.slot==b.slot && a.newValue==b.newValue && a.newStringValue==b.newStringValue,"Online toggle changed player choices");
        }
        menu.update();
        menuCapture(menu,"custom-online");
        menu.onNext();
        menuRequire(!menu.isQuiting() && menu.hasChildWindow(),"Online setup allowed a lobby with every guest seat occupied by AI");
        menu.closeChildWindow();
        menu.processChildWindowOpenCloses();
        auto& opponent = menu.houseInfo[1].player1DropDown;
        for(int i=0;i<opponent.getNumEntries();++i)
            if(opponent.getEntry(i)==_("open")) { opponent.setSelectedItem(i); break; }
        menu.onNext();
        menuRequire(menu.retVal==MENU_SETUP_HOST,"Create Lobby did not return prepared setup");
    }
    {
        setup.sharedHouse=true;
        setup.players=ChangeEventList{};
        GameInitSettings sharedInit(getBasename(map,true),readCompleteFile(map),true,setup.rules);
        CustomGamePlayers menu(sharedInit,true,false,&setup);
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Shared house controls exceed menu width");
        menu.update(); menuCapture(menu,"custom-shared");
    }
    for(const auto& candidate : setup.maps) {
        if(getBasename(candidate).find("6P - 64x64") == std::string::npos) continue;
        for(bool shared : {false, true}) {
            auto sixSetup=setup;
            sixSetup.online=shared;
            sixSetup.sharedHouse=shared;
            sixSetup.map=static_cast<int>(std::distance(setup.maps.begin(), std::find(setup.maps.begin(), setup.maps.end(), candidate)));
            sixSetup.players=ChangeEventList{};
            GameInitSettings sixInit(getBasename(candidate,true),readCompleteFile(candidate),shared,setup.rules);
            CustomGamePlayers menu(sixInit,true,false,&sixSetup);
            menuRequire(menu.numHouses==6,"Six-house fixture not loaded");
            menuRequire(menu.mainVBox.getMinimumSize().y<=menu.mainVBox.getSize().y,"Six-house controls exceed menu height");
            menu.update();
            menuCapture(menu,shared ? "custom-six-houses" : "custom-six-offline");
        }
        break;
    }
    {
        menuRequire(ModManager::instance().setActiveMod("Tornie"),"Bonus-color fixture mod unavailable");
        GameInitSettings modInit(getBasename(map,true),readCompleteFile(map),false,setup.rules);
        CustomGamePlayers menu(modInit,true);
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Bonus color controls exceed menu width");
        auto& row=menu.houseInfo[0];
        menuRequire(row.bonusColorCheckbox.isVisible() && row.bonusColorCheckbox.isEnabled(),"Classic bonus color selector is unavailable");
        row.bonusColorCheckbox.setChecked(true);
        menu.onBonusColorCheckbox(0);
        menuRequire(row.colorDropDown.getNumEntries()>1,"Bonus palette has no colors");
        menuCapture(menu,"custom-bonus-colors");
        ModManager::instance().setActiveMod("vanilla");
    }
    for(const auto& mod : {std::string("vanilla"),std::string("dunecity")}) {
        menuRequire(ModManager::instance().setActiveMod(mod), "City status fixture unavailable");
        GameInitSettings skinInit(getBasename(map,true),readCompleteFile(map),false,setup.rules);
        CustomGamePlayers menu(skinInit,true);
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Skin controls exceed menu width");
        if(mod=="dunecity") {
            menuRequire(menu.duneCitySkinControls,"Dune City lobby skin selector missing");
            menu.houseInfo[0].graphicsSkinDropDown.setSelectedItem(1);
            menu.addAllPlayersToGameInitSettings();
            menuRequire(menu.gameInitSettings.getHouseInfoList().front().graphicsSkin==GameInitSettings::GraphicsSkin::Dune2,
                "Lobby skin selection is not retained in game settings");
        }
        menuRequire(menu.mapPropertyCity.getText()==(mod=="dunecity" ? _("On") : _("Off")), "Lobby city status disagrees with mod");
        menuCapture(menu,"city-status-"+mod);
    }
    ModManager::instance().setActiveMod("vanilla");
    setup.players=original;
    {
        CrossplayMenu menu;
        const auto settingsName = settings.general.playerName;
        settings.general.playerName = "";
        menu.onHostCustomGame();
        menuRequire(menu.hasChildWindow(),"Custom hosting bypasses the settings player name");
        menu.closeChildWindow(); menu.processChildWindowOpenCloses();
        menu.onHostCampaignCoop();
        menuRequire(menu.hasChildWindow(),"Campaign hosting bypasses the settings player name");
        menu.closeChildWindow(); menu.processChildWindowOpenCloses();
        settings.general.playerName = settingsName;
        menuRequire(menu.playerNameValue.getText()==settingsName,"Play Online does not display the settings name");
        menuRequire(CustomPlaySetup().publicGame && menu.visibilityChoice.getSelectedIndex()==1,"New games should default to public");
        menuRequire(menu.chatHistory.getParent()==&menu.windowWidget && menu.publicGameList.getParent()==&menu.windowWidget,
            "Chat and game list must share the first screen");
        menuRequire(menu.chatHistory.getSize().x>=220 && menu.publicGameList.getSize().x>=350,
            "Side-by-side lobby panes are too narrow");
        menu.stage=CrossplayMenu::Stage::Choosing;
        PublicRelayGame coop, custom;
        coop.mode="coop"; coop.hostName="Campaign host"; coop.roomCode="AAAA-BBBB-CCCC"; coop.players=1; coop.maxPeers=2;
        custom.mode="custom"; custom.hostName="Custom host"; custom.roomCode="DDDD-EEEE-FFFF"; custom.players=2; custom.maxPeers=4;
        coop.modName="vanilla"; custom.modName="dunecity";
        coop.mapName="Atreides mission 4"; custom.mapName="Habbanya-Autumn";
        custom.running=true; custom.allowLateJoin=true; custom.elapsedSeconds=620;
        menuRequire(menu.modFilter.getSelectedEntryIntData()==-1,"All mods must be the default");
        menu.allPublicGames={coop,custom}; menu.refreshDirectory();
        menuRequire(menu.publicGames.size()==2,"All mods hides another mod");
        for(int i=0;i<static_cast<int>(menu.availableMods.size());++i) {
            if(menu.availableMods[i].name!="dunecity") continue;
            menu.modFilter.setSelectedItem(i+1); menu.refreshDirectory();
            menuRequire(menu.publicGames.size()==1 && menu.publicGames[0].modName=="dunecity",
                "Specific mod filter includes other mods");
            menuRequire(ModManager::instance().getActiveModName()=="vanilla","Filtering changes active mod");
        }
        menu.modFilter.setSelectedItem(0); menu.refreshDirectory();
        menu.modeFilter.setSelectedItem(1); menu.refreshDirectory();
        menuRequire(menu.publicGames.size()==1 && menu.publicGames[0].mode=="coop","Campaign filter includes custom games");
        menu.modeFilter.setSelectedItem(2); menu.refreshDirectory();
        menuRequire(menu.publicGames.size()==1 && menu.publicGames[0].mode=="custom","Custom filter includes campaign games");
        menu.modeFilter.setSelectedItem(0); menu.refreshDirectory();
        menuCapture(menu,"join");
        menu.directory.cancel(); menu.directoryPending=false;
        // Use the installed current mod; these are local, deterministic UI checks.
        menu.publicGames[1].modName="vanilla";
        menu.publicGameList.setSelectedItem(1);
        settings.network.directDevelopmentEndpoint="http://127.0.0.1:1";
        menu.joinPublicGame();
        menu.processChildWindowOpenCloses();
        menuRequire(menu.pChildWindow==nullptr && menu.stage==CrossplayMenu::Stage::Requesting,
            "Running join should request admission without a mode prompt");
        menuRequire(menu.joiningRunning && menu.joiningAsSpectator,
            "Running join must enter as a passive spectator");
        menu.admission.cancel();
        menu.stage=CrossplayMenu::Stage::Choosing;
        settings.network.directDevelopmentEndpoint="";

        menu.joiningRunning=true;
        menu.onReceiveGameInfo(init,ChangeEventList());
        menuRequire(!menu.pendingGameInfo,"Original lobby setup hijacks checkpoint admission");
        menu.joiningRunning=false;
        menu.onReceiveGameInfo(init,ChangeEventList());
        menuRequire(menu.pendingGameInfo!=nullptr,"Waiting-game setup no longer opens its lobby");
        menu.pendingGameInfo.reset();

        for(bool hotJoin : {false,true}) {
            menu.stage=CrossplayMenu::Stage::Requesting;
            menu.joiningRunning=hotJoin;
            menu.admission.status_=RoomAdmissionClient::Status::Failed;
            menu.admission.response_.errorCode="version_mismatch";
            menu.admission.errorMessage_="Version mismatch. Host: 1.0.726. Yours: 1.0.727. Both players must use the same game version. Update or switch versions, then try again.";
            menu.update();
            menuRequire(menu.hasChildWindow(),"Version mismatch did not open an acknowledged prompt");
            menuRequire(menu.pChildWindow->getSize().x<=getRendererWidth() && menu.pChildWindow->getSize().y<=getRendererHeight(),
                "Version prompt exceeds screen bounds");
            auto* prompt=dynamic_cast<MsgBox*>(menu.pChildWindow);
            menuRequire(prompt!=nullptr && prompt->getText().find("1.0.726")!=std::string::npos
                && prompt->getText().find("1.0.727")!=std::string::npos,"Version prompt omits one of the versions");
            menuRequire(menu.stage==CrossplayMenu::Stage::Choosing && menu.joinTicket.empty(),
                "Version mismatch left an active join request");
            menuCapture(menu,hotJoin ? "hot-join-version-mismatch" : "join-version-mismatch");
            menu.closeChildWindow(); menu.processChildWindowOpenCloses();
            menu.update();
            menuRequire(!menu.hasChildWindow(),"Version prompt reopened without another join attempt");
        }
        menu.joiningRunning=true;
        menu.stage=CrossplayMenu::Stage::ClientWaiting;
        pNetworkManager=std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
        auto& progressNetwork=*pNetworkManager;
        progressNetwork.playerName=settingsName;
        progressNetwork.spectators.insert(settingsName);
        progressNetwork.joinStage=NetworkManager::JoinStage::Receiving;
        progressNetwork.observerTotal=128*1024;
        progressNetwork.observerBytes.assign(64*1024,'x');
        menu.update();
        menuRequire(menu.joinProgress.isVisible() && !menu.statusLabel.isVisible(),"Join progress is not visible");
        menuRequire(menu.joinProgress.getProgress()==50 && menu.joinProgress.getText().find("64 / 128 KiB")!=std::string::npos,
            "Join progress does not describe received bytes");
        menuCapture(menu,"hot-join-download-progress");
        progressNetwork.spectators.clear();
        progressNetwork.joinName="Codex Web Guest";
        progressNetwork.joinTotal=128*1024;
        progressNetwork.joinBytes.assign(64*1024,'x');
        {
            JoinProgressWindow progress;
            menuCapture(progress,"named-player-joining");
            progressNetwork.joinName=std::string(64,'W');
            progress.refresh();
            menuCapture(progress,"named-player-joining-long-name");
        }
        // The name must survive checkpoint handoff, but a completion notice
        // must wait until the replacement simulation has actually started.
        progressNetwork.joinName="Codex Web Guest";
        progressNetwork.joinStage=NetworkManager::JoinStage::Ready;
        progressNetwork.joinSnapshot=std::make_unique<GameInitSettings>();
        progressNetwork.joinSpectators.clear();
        menuRequire(progressNetwork.takeLateJoin()!=nullptr,"Checkpoint handoff failed");
        menuRequire(progressNetwork.lateJoinPlayerName()=="Codex Web Guest"
            && progressNetwork.recentJoinNotice().empty(),"Join announced before the new simulation started");
        progressNetwork.beginSimulation(1);
        menuRequire(progressNetwork.recentJoinNotice()=="Codex Web Guest joined to play"
            && progressNetwork.lateJoinPlayerName().empty(),"Completed join lost its player name");
        progressNetwork.joinNoticeTime=SDL_GetTicks()-12000;
        menuRequire(progressNetwork.recentJoinNotice().empty(),"Completed join banner did not expire");
        menu.teardownSession("Test transfer ended. Please try joining again.");
        menuRequire(!menu.joinProgress.isVisible() && menu.statusLabel.isVisible() && !menu.joiningRunning,
            "Join failure did not restore usable lobby controls");
        menuRequire(menu.statusLabel.getText().find("Please try")!=std::string::npos,"Join failure reason disappeared");
        menu.joiningRunning=false;
        menu.chat.cancel(); menu.chatSession.clear(); menu.chatPending=false;
        menu.nextChatPoll=0;
        menu.update();
        menuRequire(menu.chatAction==AdmissionOperation::ChatEnter,
            "Chat does not enter automatically from the first screen");
        menuRequire(!menu.hasChildWindow(),"Automatic chat entry opened a name confirmation");
        menu.chat.cancel();
        menu.chatPending=true;
        menu.chatAction=AdmissionOperation::ChatPoll;
        menu.chatSession="expired-session";
        menu.chat.status_=RoomAdmissionClient::Status::Failed;
        menu.chat.response_.errorCode="session_expired";
        menu.updateLobbyChat();
        menuRequire(menu.chatSession.empty() && !menu.chatPending,
            "Expired chat session was not released");
        menu.nextChatPoll=0;
        menu.update();
        menuRequire(menu.chatAction==AdmissionOperation::ChatEnter,
            "Expired chat requires manual name confirmation");
        menu.chat.cancel(); menu.chatPending=true; menu.chatAction=AdmissionOperation::ChatPoll;
        menu.chat.status_=RoomAdmissionClient::Status::Succeeded;
        menu.chat.response_=AdmissionResponse(); menu.chat.response_.ok=true;
        menu.chat.response_.hasPresence=true; menu.chat.response_.onlineCount=2;
        menu.chat.response_.waitingNames={"Alice","Bob"};
        menu.updateLobbyChat();
        menuRequire(menu.waitingLabel.getText()=="Players waiting: 2", "Waiting count not shown");
        menuRequire(menu.waitingNames.getText()=="Alice, Bob", "Waiting names not shown");
        menu.chatHistory.setText("Alice: Looking for a campaign partner.\nBob: I can join after this match.");
        menu.chatLabel.setText("Chatting as " + settingsName);
        menuCapture(menu,"public-chat");
        menu.chatPending=true; menu.chatAction=AdmissionOperation::ChatPoll;
        menu.chat.status_=RoomAdmissionClient::Status::Failed;
        menu.updateLobbyChat();
        menuRequire(menu.waitingLabel.getText()=="Players waiting"
            && menu.waitingNames.getText()=="Unable to refresh online players", "Failed poll leaves a stale count");
    }
    {
        GameInitSettings networkInit(getBasename(map,true),readCompleteFile(map),"Prepared custom",false,setup.rules);
        CrossplayMenu menu(networkInit,false,setup.players);
        menuRequire(menu.preparedGame->getFiledata()==init.getFiledata(),"Prepared hosting changed the selected map");
        menuRequire(menu.preparedPlayers.changeEventList.size()==original.changeEventList.size(),"Prepared hosting lost the roster");
        menuRequire(menu.visibilityChoice.getSelectedIndex()==0,"Explicit private room choice was not preserved");
    }
    {
        OptionsMenu menu;
        for(int page=0;page<5;++page) { menu.showPage(page); menuCapture(menu,"settings-"+std::to_string(page)); }
        menuRequire(menu.interfaceSizeDropDownBox.getParent()==&menu.interfaceHBox,"Interface size missing from Settings");
        menuRequire(menu.pages[1].getMinimumSize().y <= menu.pages[1].getSize().y,"Graphics controls overflow");
        menuRequire(menu.pages[4].getMinimumSize().y <= menu.pages[4].getSize().y,"Advanced controls overflow");
        menuRequire(menu.diagnosticLogsCheckbox.isChecked()==settings.general.diagnosticLogs,"Diagnostic preference not reflected in Settings");
        menu.diagnosticLogsCheckbox.setChecked(!settings.general.diagnosticLogs);
        menu.onChangeOption(true);
        menuRequire(menu.acceptButton.isEnabled(),"Diagnostic preference change cannot be applied");
        const bool diagnostics=menu.diagnosticLogsCheckbox.isChecked();
        menu.showPage(2);
        settings.network.metaServer=""; menu.metaServerTextBox.setText("");
        menu.playSFXCheckbox.setChecked(!settings.audio.playSFX);
        menu.wasdCameraCheckbox.setChecked(true);
        menu.leftClickOrdersCheckbox.setChecked(true);
        menu.movementPathsCheckbox.setChecked(false);
        menu.onOptionsOK();
        menuRequire(settings.general.wasdCamera && settings.general.leftClickOrders && !settings.general.showMovementPaths,"Input options not applied");
        INIFile savedOptions(getConfigFilepath());
        menuRequire(settings.general.diagnosticLogs==diagnostics
            && savedOptions.getBoolValue("General","Diagnostic Logs",!diagnostics)==diagnostics,"Diagnostic preference not applied/persisted");
        menuRequire(savedOptions.getBoolValue("General","WASD Camera",false)
            && savedOptions.getBoolValue("General","Left Click Orders",false)
            && !savedOptions.getBoolValue("General","Movement Paths",true),"Input options not persisted");
        menuRequire(menu.isQuiting(),"Unchanged legacy network field blocked audio apply");
    }
    {
        menuRequire(ModManager::instance().setActiveMod("dunecity"),"Skin options fixture mod unavailable");
        OptionsMenu menu;
        menu.showPage(0);
        menuRequire(menu.pages[0].getMinimumSize().y<=menu.pages[0].getSize().y,"Campaign skin controls overflow Settings");
        menu.duneCityCampaignSkinDropDownBox.setSelectedItem(1);
        menu.onOptionsOK();
        menuRequire(settings.general.duneCityCampaignSkin==1,"Campaign skin preference not applied");
        INIFile savedOptions(getConfigFilepath());
        menuRequire(savedOptions.getIntValue("General","DuneCity Campaign Skin",0)==1,"Campaign skin preference not persisted");
        menuCapture(menu,"settings-dunecity-skin");
        ModManager::instance().setActiveMod("vanilla");
    }
    { AboutMenu menu;menuCapture(menu,"about"); }
    { HowToPlayMenu menu;menuCapture(menu,"help"); }
    { ModMenu menu;menuCapture(menu,"mods"); }
    { DisplayMenu menu;menuCapture(menu,"display"); }
    { auto rules=settings.gameOptions;GameOptionsWindow menu(rules);menuCapture(menu,"game-rules"); }
    SDL_Log("MENU_PROBE_PASS: home, campaign retry, mission ranges, custom roster, online transition, settings validation and rendering");
    return MENU_QUIT_DEFAULT;
}
