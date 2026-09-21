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

#include <GUI/dune/InGameSettingsMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>
#include <Game.h>
#include <main.h>
#include <House.h>
#include <SoundPlayer.h>
#include <ScreenBorder.h>

#include <GUI/Spacer.h>
#include <misc/MenuPalette.h>
#include <misc/WebRuntime.h>


InGameSettingsMenu::InGameSettingsMenu() : Window(0,0,0,0) {
    const int width=440,height=352;
    setCurrentPosition((getRendererWidth()-width)/2,(getRendererHeight()-height)/2,width,height);
    setWindowWidget(&windowWidget);
    title.setText(_("Game settings"));title.setTextFontSize(24);title.setAlignment(Alignment_HCenter);
    title.setTextColor(MenuTheme::text,COLOR_TRANSPARENT);
    windowWidget.addWidget(&title,Point(20,12),Point(400,36));
    auto addRow=[&](Label& label,TextButton& minus,ProgressBar& bar,TextButton& plus,
                   const char* text,int y,auto decrease,auto increase) {
        label.setText(_(text));label.setTextFontSize(18);label.setAlignment(Alignment_Left);
        windowWidget.addWidget(&label,Point(24,y),Point(392,26));
        minus.setText("-");minus.setOnClick(decrease);
        windowWidget.addWidget(&minus,Point(24,y+30),Point(36,32));
        plus.setText("+");plus.setOnClick(increase);
        windowWidget.addWidget(&plus,Point(380,y+30),Point(36,32));
        bar.setColor(MenuTheme::accent);
        windowWidget.addWidget(&bar,Point(72,y+38),Point(296,16));
    };
    addRow(gameSpeedLabel,gameSpeedMinus,gameSpeedBar,gameSpeedPlus,"Game speed",52,
        std::bind(&InGameSettingsMenu::onGameSpeedMinus,this),std::bind(&InGameSettingsMenu::onGameSpeedPlus,this));
    addRow(volumeLabel,volumeMinus,volumeBar,volumePlus,"Sound volume",122,
        std::bind(&InGameSettingsMenu::onVolumeMinus,this),std::bind(&InGameSettingsMenu::onVolumePlus,this));
    addRow(scrollSpeedLabel,scrollSpeedMinus,scrollSpeedBar,scrollSpeedPlus,"Scroll speed",192,
        std::bind(&InGameSettingsMenu::onScrollSpeedMinus,this),std::bind(&InGameSettingsMenu::onScrollSpeedPlus,this));
    playCreditsSFXCheckbox.setText(_("Credits sound"));
    playCreditsSFXCheckbox.setChecked(settings.audio.playCreditsSFX);
    windowWidget.addWidget(&playCreditsSFXCheckbox,Point(24,260),Point(392,28));
    okButton.setText(_("Apply"));okButton.setOnClick(std::bind(&InGameSettingsMenu::onOK,this));
    cancelButton.setText(_("Cancel"));cancelButton.setOnClick(std::bind(&InGameSettingsMenu::onCancel,this));
    windowWidget.addWidget(&okButton,Point(24,300),Point(190,36));
    windowWidget.addWidget(&cancelButton,Point(226,300),Point(190,36));

    init();
}

InGameSettingsMenu::~InGameSettingsMenu() = default;

void InGameSettingsMenu::init() {
    newGamespeed = currentGame->getGameSpeed();
    const bool mayChangeSpeed=currentGame->canChangeGameSettings();
    gameSpeedMinus.setEnabled(mayChangeSpeed);gameSpeedPlus.setEnabled(mayChangeSpeed);
    gameSpeedLabel.setText(mayChangeSpeed ? _("Game speed") : _("Game speed (host only)"));
    gameSpeedBar.setProgress(100.0 - ((newGamespeed-GAMESPEED_MIN)*100.0)/(GAMESPEED_MAX - GAMESPEED_MIN));

    previousVolume = volume = soundPlayer->getSfxVolume();
    volumeBar.setProgress((100.0*volume)/MIX_MAX_VOLUME);

    scrollSpeed = settings.general.scrollSpeed;
    scrollSpeedBar.setProgress(scrollSpeed);
}

bool InGameSettingsMenu::handleKeyPress(SDL_KeyboardEvent& key) {
    switch( key.keysym.sym ) {
        case SDLK_RETURN:
            if(SDL_GetModState() & KMOD_ALT) {
                toogleFullscreen();
            }
            break;

        case SDLK_TAB:
            if(SDL_GetModState() & KMOD_ALT) {
                SDL_MinimizeWindow(window);
            }
            break;

        default:
            break;
    }

    return Window::handleKeyPress(key);
}

void InGameSettingsMenu::onCancel() {
    soundPlayer->setSfxVolume(previousVolume);
    musicPlayer->setMusicVolume(previousVolume);

    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void InGameSettingsMenu::onOK() {
    settings.general.scrollSpeed = scrollSpeed;
    settings.audio.sfxVolume = soundPlayer->getSfxVolume();
    settings.audio.musicVolume = musicPlayer->getMusicVolume();
    settings.audio.playCreditsSFX = playCreditsSFXCheckbox.isChecked();
    if (currentGame->canChangeGameSettings() && newGamespeed != currentGame->getGameSpeed())
        currentGame->requestGameSpeed(newGamespeed);

    INIFile myINIFile(getConfigFilepath());
    myINIFile.setIntValue("General","Scroll Speed", settings.general.scrollSpeed);
    myINIFile.setIntValue("Audio","Music Volume", settings.audio.musicVolume);
    myINIFile.setIntValue("Audio","SFX Volume", settings.audio.sfxVolume);
    myINIFile.setBoolValue("Audio","Play Credits SFX", settings.audio.playCreditsSFX);
    myINIFile.saveChangesTo(getConfigFilepath());
    WebRuntime::syncPersistentFiles();

    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void InGameSettingsMenu::onGameSpeedPlus() {
    if (!currentGame->canChangeGameSettings()) return;
    if(newGamespeed > GAMESPEED_MIN)
        newGamespeed -= 1;

    gameSpeedBar.setProgress(100 - ((newGamespeed-GAMESPEED_MIN)*100)/(GAMESPEED_MAX - GAMESPEED_MIN));
}

void InGameSettingsMenu::onGameSpeedMinus() {
    if (!currentGame->canChangeGameSettings()) return;
    if(newGamespeed < GAMESPEED_MAX)
        newGamespeed += 1;

    gameSpeedBar.setProgress(100 - ((newGamespeed-GAMESPEED_MIN)*100)/(GAMESPEED_MAX - GAMESPEED_MIN));
}

void InGameSettingsMenu::onVolumePlus() {
    if(volume <= MIX_MAX_VOLUME - 4) {
        volume += 4;
        volumeBar.setProgress((100*volume)/MIX_MAX_VOLUME);
        soundPlayer->setSfxVolume(volume);
        musicPlayer->setMusicVolume(volume);
    }
}

void InGameSettingsMenu::onVolumeMinus() {
    if(volume >= 4) {
        volume -= 4;
        volumeBar.setProgress((100*volume)/MIX_MAX_VOLUME);
        soundPlayer->setSfxVolume(volume);
        musicPlayer->setMusicVolume(volume);
    }
}

void InGameSettingsMenu::onScrollSpeedPlus() {
    scrollSpeed = std::min(scrollSpeed+4, 100);
    scrollSpeedBar.setProgress(scrollSpeed);
}

void InGameSettingsMenu::onScrollSpeedMinus() {
    scrollSpeed = std::max(scrollSpeed-4, 1);
    scrollSpeedBar.setProgress(scrollSpeed);
}
