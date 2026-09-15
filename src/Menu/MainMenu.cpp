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

#include <Menu/MainMenu.h>
#include <Menu/PlaySetup.h>
#include <GUI/MsgBox.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <MapEditor/MapEditor.h>

#include <Menu/SinglePlayerMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/MultiPlayerMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/DisplayMenu.h>
#include <misc/MenuLayout.h>
#include <Menu/ModMenu.h>
#include <Menu/Dune2REditorMenu.h>
#include <Menu/AboutMenu.h>
#include <Menu/HowToPlayMenu.h>

#include <GUI/QstBox.h>
#include <misc/DiscordManager.h>
#include <misc/fnkdat.h>
#include <mod/ModManager.h>
#include <mod/ModInfo.h>
#include <config.h>

#include <cstdio>
#include <cctype>
#include <fstream>
#include <vector>

namespace {
// Marker file under the user config dir. Once written, the first-launch
// "Enable city-sim mod?" prompt is suppressed forever.
std::string firstLaunchMarkerPath() {
    char tmp[FILENAME_MAX];
    if (fnkdat("dunecity-first-launch.done", tmp, FILENAME_MAX,
               FNKDAT_USER | FNKDAT_CREAT) < 0) {
        return std::string();
    }
    return std::string(tmp);
}

bool firstLaunchMarkerExists() {
    const std::string p = firstLaunchMarkerPath();
    if (p.empty()) return true; // fail closed: don't pester
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

void writeFirstLaunchMarker() {
    const std::string p = firstLaunchMarkerPath();
    if (p.empty()) return;
    std::ofstream out(p);
    out << "Dune City " << VERSION << "\n";
}

class ModesMenu final : public MenuBase {
public:
    ModesMenu() {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
        setWindowWidget(&content);
        title.setText(_("Extras"));
        title.setTextFontSize(22);
        title.setAlignment(Alignment_HCenter);
        const int x = (getSize().x-320)/2;
        const int top = (getSize().y-340)/2;
        content.addWidget(&title, Point(x,top), Point(320,36));
        const char* labels[] = {"Mods", "Map Editor", "Asset Editors", "Replays", "How to Play", "About & Credits", "Back"};
        for(int i=0; i<7; ++i) {
            buttons[i].setText(_(labels[i]));
            content.addWidget(&buttons[i], Point(x,top+48+i*40), Point(320,32));
        }
        buttons[0].setOnClick([]() { ModMenu().showMenu(); });
        buttons[1].setOnClick([]() { MapEditor().RunEditor(); });
        buttons[2].setOnClick([this]() {
            if(ModManager::instance().getActiveModName() == "Dune2R") Dune2REditorMenu().showMenu();
            else openWindow(MsgBox::create(_("Choose Dune2R in Mods to use its asset editors.")));
        });
        buttons[3].setOnClick([]() { showGameLibrary(true); });
        buttons[4].setOnClick([]() { HowToPlayMenu().showMenu(); });
        buttons[5].setOnClick([]() { AboutMenu().showMenu(); });
        buttons[6].setOnClick([this]() { quit(); });
    }
private:
    StaticContainer content;
    Label title;
    TextButton buttons[7];
};
} // namespace

MainMenu::MainMenu()
{
    // Update Discord Rich Presence
    DiscordManager::instance().setMainMenu();
    
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);
    enlargedStartMenus = validatedStartMenuMode(settings.video.startMenuMode) == 1;

    canContinue = hasRecentGame();
    continueButton.setText(_("Continue"));
    continueButton.setOnClick([this]() { continueRecentGame(); canContinue = hasRecentGame(); });
    customButton.setText(_("Custom Game"));
    customButton.setOnClick([this]() { playCustomGame(); canContinue = hasRecentGame(); });
    onlineButton.setText(_("Join Online"));
    onlineButton.setOnClick([]() { CrossplayMenu().showMenu(); });
    loadButton.setText(_("Load Game"));
    loadButton.setOnClick([this]() { showGameLibrary(); canContinue = hasRecentGame(); });
    campaignButton.setText(_("Campaign"));
    campaignButton.setOnClick([this]() { SinglePlayerMenu::playCampaign(); canContinue = hasRecentGame(); });
    campaignButton.setActive();
    modesButton.setText(_("Extras"));
    modesButton.setOnClick(std::bind(&MainMenu::onModes, this));
    dune2rEditorButton.setText("DUNE2R ASSETS");
    dune2rEditorButton.setOnClick(std::bind(&MainMenu::onDune2REditor, this));
    optionsButton.setText(_("Settings"));
    optionsButton.setOnClick(std::bind(&MainMenu::onOptions, this));
    displayButton.setText(_("DISPLAY"));
    displayButton.setOnClick(std::bind(&MainMenu::onDisplay, this));
    howToPlayButton.setText(_("HOW TO PLAY"));
    howToPlayButton.setOnClick(std::bind(&MainMenu::onHowToPlay, this));
    aboutButton.setText(_("ABOUT"));
    aboutButton.setOnClick(std::bind(&MainMenu::onAbout, this));
    quitButton.setText(_("QUIT"));
    quitButton.setOnClick(std::bind(&MainMenu::onQuit, this));
    SDL_Texture* pPlanet = pGFXManager->getUIGraphic(UI_PlanetBackground);
    SDL_Texture* pLogo = pGFXManager->getUIGraphic(UI_DuneLegacy);
    planetPicture.setTexture(pPlanet);
    logoPicture.setTexture(pLogo);

    if(enlargedStartMenus) {
        const StartMenuLayout layout{getSize().x, getSize().y, 6};
        planetPicture.setFitToSize(true);
        windowWidget.addWidget(&planetPicture, layout.planetBounds());
        logoPicture.setFitToSize(true);
        windowWidget.addWidget(&logoPicture, layout.logoBounds());

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        buttonBorder.setStretchToSize(true);
        windowWidget.addWidget(&buttonBorder, layout.borderBounds());
    } else {
        SDL_Rect planetBounds = calcAlignedDrawingRect(pPlanet);
        planetBounds.y = planetBounds.y - getHeight(pPlanet) / 2 + 10;
        windowWidget.addWidget(&planetPicture, planetBounds);

        SDL_Rect logoBounds = calcAlignedDrawingRect(pLogo);
        logoBounds.y = logoBounds.y + getHeight(pLogo) / 2 + 28;
        windowWidget.addWidget(&logoPicture, logoBounds);

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        SDL_Rect borderBounds = calcAlignedDrawingRect(pBorder);
        borderBounds.y = borderBounds.y + getHeight(pBorder) / 2 + 59;
        windowWidget.addWidget(&buttonBorder, borderBounds);

    }
    // Only visible destinations participate in keyboard navigation, in screen order.
    TextButton* allButtons[] = {&continueButton, &campaignButton, &customButton, &onlineButton,
                                &loadButton, &optionsButton, &modesButton, &quitButton};
    for(TextButton* button : allButtons) {
        windowWidget.addWidget(button, Point(0, 0), Point(1, 1));
    }
    // The generic product logo must not imply DuneCity rules when Vanilla is active.
    logoPicture.setVisible(false);
    activeModLabel.setTextFontSize(24);
    // Same-colour shadow supplies an extra pixel of weight to the lettering.
    activeModLabel.setTextColor(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
    activeModLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    windowWidget.addWidget(&activeModLabel, Point(0, 0), Point(1, 1));
    refreshContextButtons();
    modVersionLabel.setTextFontSize(14);
    modVersionLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
    modVersionLabel.setAlignment(enlargedStartMenus
        ? Alignment_HCenter
        : static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
    refreshModVersionLabel();
    if(enlargedStartMenus) {
        windowWidget.addWidget(&modVersionLabel, Point(24, getSize().y - 30), Point(getSize().x - 48, 24));
    } else {
        windowWidget.addWidget(&modVersionLabel, Point(12, getSize().y - 58), Point(220, 50));
    }
    if(canContinue) continueButton.setActive();
    else campaignButton.setActive();
}

void MainMenu::refreshModVersionLabel()
{
    std::string activeModName;
    std::string modDisplayName = "Vanilla";
    ModManager& modManager = ModManager::instance();
    if (modManager.isInitialized()) {
        activeModName = modManager.getActiveModName();
        // v1.0.510: defensive null guard. Tornie's ModInfo.displayName was
        // observed empty on some mod bundles (Tornie was registered but
        // the ModInfo was never populated past init). Reading an empty
        // string then concatenating with "\nv" was crashing in some
        // label rendering paths downstream. Fall back to the raw mod
        // name in that case.
        try {
            ModInfo info = modManager.getModInfo(activeModName);
            if (!info.displayName.empty()) {
                modDisplayName = info.displayName;
            } else if (!info.name.empty()) {
                modDisplayName = info.name;
            } else if (!activeModName.empty()) {
                modDisplayName = activeModName;
            }
        } catch (const std::exception& e) {
            SDL_Log("MainMenu: refreshModVersionLabel failed: %s — using raw mod name", e.what());
            modDisplayName = activeModName.empty() ? "Unknown" : activeModName;
        }
    }

    if (activeModName == lastShownModName && !modVersionLabel.getText().empty()) {
        return;
    }
    lastShownModName = activeModName;
    try {
        std::transform(modDisplayName.begin(), modDisplayName.end(), modDisplayName.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const std::string bannerText = "MOD: " + modDisplayName;
        int bannerFontSize = 24;
        const int bannerWidth = std::min(getSize().x - 48, 420);
        while (bannerFontSize > 12 && GUIStyle::getInstance().getMinimumLabelSize(bannerText, bannerFontSize).x > bannerWidth)
            --bannerFontSize;
        activeModLabel.setTextFontSize(bannerFontSize);
        activeModLabel.setText(bannerText);
        modVersionLabel.setText("v" + std::string(VERSION));
    } catch (const std::exception& e) {
        SDL_Log("MainMenu: setText failed: %s", e.what());
    }
}

MainMenu::~MainMenu() = default;

int MainMenu::showMenu()
{
    int menuResult = -1;
    try {
        musicPlayer->changeMusic(MUSIC_MENU);

        // Start version check in background (only once)
        if(!bVersionCheckStarted) {
            bVersionCheckStarted = true;

#ifndef __EMSCRIPTEN__
            pVersionChecker = std::make_unique<VersionChecker>(settings.network.metaServer);
            pVersionChecker->setOnVersionCheckComplete([this](const VersionInfo& info) {
                if(info.updateAvailable && !bUpdateDialogShown) {
                    latestVersion = info.latestVersion;
                    downloadURL = info.downloadURL;
                    // Show dialog in update() when safe (not during callback)
                }
            });
            pVersionChecker->checkForUpdates();
#endif
        }

        menuResult = MenuBase::showMenu();
    } catch(const std::exception& e) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
            "MainMenu::showMenu failed: %s — returning to caller with code -1", e.what());
    } catch(...) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
            "MainMenu::showMenu failed: unknown exception — returning to caller with code -1");
    }
    return menuResult;
}

void MainMenu::update()
{
    // Mod can be switched from any sub-menu (ModMenu, CustomGameMenu,
    // CustomGamePlayers); refresh the watermark on every tick so it
    // tracks the live ModManager state when control returns here.
    refreshModVersionLabel();
    refreshContextButtons();

    // Process version check results
#ifndef __EMSCRIPTEN__
    if(pVersionChecker) {
        pVersionChecker->update();
    }
#endif

    // Show update dialog if new version available and not already shown
    if(!latestVersion.empty() && !bUpdateDialogShown && !pChildWindow) {
        bUpdateDialogShown = true;

        std::string message = _("A new version of Dune City is available!");
        message += "\n\n";
        message += _("Current: ");
        message += VERSION;
        message += "\n";
        message += _("Latest: ");
        message += latestVersion;
        message += "\n\n";
        message += _("Would you like to visit the download page?");

        openWindow(QstBox::create(message, _("Download"), _("Later"), QSTBOX_BUTTON1));
    }

    // First-launch "Enable city-sim mod?" prompt. Runs after the update
    // dialog so we don't stack two QstBoxes on top of each other.
    showFirstLaunchCityPromptIfNeeded();
}

void MainMenu::onChildWindowClose(Window* pChildWindow)
{
    QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);
    if (pQstBox == nullptr) return;

    if (bFirstLaunchPromptOpen) {
        // This QstBox was the first-launch "Enable city-sim mod?" prompt.
        bFirstLaunchPromptOpen = false;
        writeFirstLaunchMarker(); // record the user's decision either way

        if (pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
            ModManager& mm = ModManager::instance();
            if (mm.setActiveMod("dunecity")) {
                // Reinitialize so all subsystems pick up the new mod's
                // ObjectData.ini, QuantBot Config.ini, and game options.
                quit(MENU_QUIT_REINITIALIZE);
            }
        }
        return;
    }

    if (pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
        // User clicked "Download" - open the download URL
        if (!downloadURL.empty()) {
            SDL_OpenURL(downloadURL.c_str());
        }
    }
}

void MainMenu::onModes() const
{
    ModesMenu().showMenu();
}

void MainMenu::onDune2REditor() const
{
    if(ModManager::instance().isInitialized()
       && ModManager::instance().getActiveModName() == "Dune2R") {
        Dune2REditorMenu editor;
        editor.showMenu();
    }
}

void MainMenu::refreshContextButtons()
{
    dune2rEditorButton.setEnabled(false);
    howToPlayButton.setEnabled(false);
    aboutButton.setEnabled(false);
    displayButton.setEnabled(false);
    dune2rEditorButton.setVisible(false);
    howToPlayButton.setVisible(false);
    aboutButton.setVisible(false);
    displayButton.setVisible(false);
    continueButton.setVisible(canContinue);
    continueButton.setEnabled(canContinue);
    std::vector<TextButton*> buttons;
    if(canContinue) buttons.push_back(&continueButton);
    for(auto* button : {&campaignButton,&customButton,&onlineButton,&loadButton,&optionsButton,&modesButton,&quitButton}) buttons.push_back(button);
    const int width = enlargedStartMenus ? 320 : 280;
    const int x = (getSize().x-width)/2;
    const int top = std::max(158, (getSize().y-310)/2);
    planetPicture.setFitToSize(true);
    windowWidget.setWidgetGeometry(&planetPicture, Point((getSize().x-176)/2,top-146), Point(176,100));
    activeModLabel.setTextFontSize(16);
    windowWidget.setWidgetGeometry(&activeModLabel, Point(x-30,top-40), Point(width+60,30));
    buttonBorder.setVisible(false);
    const int gap = 5;
    const int height = std::min(36, (getSize().y-top-20)/static_cast<int>(buttons.size())-gap);
    for(size_t i=0; i<buttons.size(); ++i) {
        windowWidget.setWidgetGeometry(buttons[i], Point(x,top+static_cast<int>(i)*(height+gap)), Point(width,height));
    }
}

void MainMenu::onOptions() {
    OptionsMenu  optionsMenu;
    int ret = optionsMenu.showMenu();

    if(ret == MENU_QUIT_REINITIALIZE) {
        quit(MENU_QUIT_REINITIALIZE);
    }
}

void MainMenu::onDisplay() {
    if(DisplayMenu().showMenu() == MENU_QUIT_REINITIALIZE) quit(MENU_QUIT_REINITIALIZE);
}

void MainMenu::onAbout() const
{
    AboutMenu myAbout;
    myAbout.showMenu();
}

void MainMenu::onHowToPlay() const
{
    HowToPlayMenu menu;
    menu.showMenu();
}

void MainMenu::onQuit() {
    quit();
}

void MainMenu::showFirstLaunchCityPromptIfNeeded()
{
    if (bFirstLaunchPromptChecked) return;
    bFirstLaunchPromptChecked = true;

    // Don't compete with the version-update dialog.
    if (bUpdateDialogShown || pChildWindow != nullptr) {
        // Reschedule on next tick by un-flagging.
        bFirstLaunchPromptChecked = false;
        return;
    }

    ModManager& mm = ModManager::instance();
    if (!mm.isInitialized()) return;

    // Already on a city-sim mod -> nothing to prompt.
    if (mm.isCityModeActive()) {
        writeFirstLaunchMarker();
        return;
    }

    // Already shown previously -> respect the user's choice.
    if (firstLaunchMarkerExists()) return;

    // Need the dunecity mod to exist before we can offer to activate it.
    if (!mm.modExists("dunecity")) return;

    bFirstLaunchPromptOpen = true;

    std::string message = _("Welcome to Dune City!");
    message += "\n\n";
    message += _("Build a city on Arrakis with districts, roads,\npower and public services.");
    message += "\n\n";
    message += _("Enable Dune City now?\nYou can change this later in MODS.");

    auto* prompt = QstBox::create(message, _("Enable now"), _("Later"), QSTBOX_BUTTON1);
    openWindow(prompt);
}


