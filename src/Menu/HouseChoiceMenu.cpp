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

#include <Menu/HouseChoiceMenu.h>
#include <sand.h>
#include <Menu/PlaySetup.h>
#include <Menu/SinglePlayerSkirmishMenu.h>
#include <mod/ModManager.h>
#include <Network/OnlineModPolicy.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <GUI/Spacer.h>
#include <GUI/MsgBox.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <Menu/HouseChoiceInfoMenu.h>
#include <SoundPlayer.h>


namespace {
const int houseOrder[] = {
    HOUSE_ATREIDES,
    HOUSE_ORDOS,
    HOUSE_HARKONNEN,
    HOUSE_MERCENARY,
    HOUSE_FREMEN,
    HOUSE_SARDAUKAR,
    HOUSE_NEUTRAL,
    HOUSE_REBELS,
    HOUSE_CUSTOM
};

constexpr int kVisibleHouseButtons = 3;
int getHouseChoiceCount() {
    const int capacity = sizeof(houseOrder) / sizeof(houseOrder[0]);
    return isHouseAvailable(HOUSE_CUSTOM) ? capacity : capacity - 1;
}

int getMaxHouseScrollPos() {
    return getHouseChoiceCount() - kVisibleHouseButtons;
}

const char* const kSupportPlayerClasses[] = {
    "",
    "qBotSupportEasy",
    "qBotSupportMedium",
    "qBotSupportHard",
    "qBotSupportBrutal",
    "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend"
};

constexpr int kSupportOptionCount = sizeof(kSupportPlayerClasses) / sizeof(kSupportPlayerClasses[0]);

const char* const kEnemyAIClasses[] = {
    "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend", "CampaignAIPlayer"
};

constexpr int kEnemyAIOptionCount = sizeof(kEnemyAIClasses) / sizeof(kEnemyAIClasses[0]);
}

// Static member definitions
int HouseChoiceMenu::s_house = HOUSE_ATREIDES;
bool HouseChoiceMenu::s_online = true;
bool HouseChoiceMenu::s_singleMission = false;
bool HouseChoiceMenu::s_publicGame = true;
int HouseChoiceMenu::s_startLevel = 1;
int HouseChoiceMenu::s_supportBotIndex = 0;
int HouseChoiceMenu::s_enemyAIIndex = 0;
int HouseChoiceMenu::s_campaignSkin = 0;
SettingsClass::GameOptionsClass HouseChoiceMenu::s_currentGameOptions;

HouseChoiceMenu::HouseChoiceMenu(bool online, bool keepRules, bool showLobby) : MenuBase(), showLobby(showLobby)
{
    OnlineModPolicy::restoreApprovedSelection();
    s_online = online;
    currentHouseChoiceScrollPos = 0;
    if(!keepRules) {
        s_currentGameOptions = effectiveGameOptions;
        s_campaignSkin = settings.general.duneCityCampaignSkin == 1 ? 1 : 0;
    }

    // set up window
    int xpos = std::max(0,(getRendererWidth() - 640)/2);
    int ypos = std::max(0,(getRendererHeight() - 480)/2);

    setCurrentPosition(xpos,ypos,640,480);

    setTransparentBackground(true);

    setWindowWidget(&windowWidget);


    titleLabel.setText(_("Campaign"));
    titleLabel.setTextColor(COLOR_WHITE);
    titleLabel.setTextFontSize(20);
    titleLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&titleLabel, Point(48,0), Point(174,26));
    onlineDescription.setTextFontSize(10);
    onlineDescription.setTextColor(COLOR_WHITE);
    onlineDescription.setAlignment(Alignment_Left);
    windowWidget.addWidget(&onlineDescription, Point(232,0), Point(360,28));
    connectionDropDown.addEntry(_("Offline"), 0);
    connectionDropDown.addEntry(_("Online co-op"), 1);
    connectionDropDown.setSelectedItem(s_online ? 1 : 0);
    connectionDropDown.setOnSelectionChange([this](bool interactive) {
        s_online = connectionDropDown.getSelectedIndex() == 1;
        // Remember the player's own choice; a programmatic online flow keeps its preference.
        if(interactive) rememberPlayMode(PlayModeScope::Campaign, s_online);
        updateConnection();
    });
    windowWidget.addWidget(&connectionDropDown, Point(48,30), Point(174,24));
    journeyDropDown.addEntry(_("Full campaign"), 0);
    journeyDropDown.addEntry(_("Single mission"), 1);
    journeyDropDown.setSelectedItem(s_singleMission ? 1 : 0);
    journeyDropDown.setOnSelectionChange([this](bool interactive) { if(interactive) { s_singleMission = journeyDropDown.getSelectedIndex() == 1; populateLevels(); updateConnection(); } });
    windowWidget.addWidget(&journeyDropDown, Point(232,30), Point(174,24));
    visibilityDropDown.addEntry(_("Private - invite code"), 0);
    visibilityDropDown.addEntry(_("Public - anyone"), 1);
    visibilityDropDown.setSelectedItem(s_publicGame ? 1 : 0);
    visibilityDropDown.setOnSelectionChange([this](bool) { s_publicGame = visibilityDropDown.getSelectedIndex() == 1; });
    windowWidget.addWidget(&visibilityDropDown, Point(416,30), Point(176,24));
    selectedHouseLabel.setTextColor(COLOR_WHITE);
    selectedHouseLabel.setTextFontSize(12);
    selectedHouseLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&selectedHouseLabel, Point(232,250), Point(176,40));

    // set up buttons
    house1Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 0));
    windowWidget.addWidget(&house1Button, Point(40,60),    Point(168,182));

    house2Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 1));
    windowWidget.addWidget(&house2Button, Point(235,60),   Point(168,182));

    house3Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 2));
    windowWidget.addWidget(&house3Button, Point(430,60),   Point(168,182));

    SDL_Texture *pArrowLeft = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftLarge);
    SDL_Texture *pArrowLeftHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftHighlightLarge);
    houseLeftButton.setTextures(pArrowLeftHighlight, pArrowLeftHighlight, pArrowLeftHighlight);
    houseLeftButton.setOnClick(std::bind(&HouseChoiceMenu::onHouseLeft, this));
    houseLeftButton.setVisible(true);
    windowWidget.addWidget( &houseLeftButton, Point(320 - getWidth(pArrowLeft) - 85, 250), getTextureSize(pArrowLeft));

    SDL_Texture *pArrowRight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightLarge);
    SDL_Texture *pArrowRightHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightHighlightLarge);
    houseRightButton.setTextures(pArrowRightHighlight, pArrowRightHighlight, pArrowRightHighlight);
    houseRightButton.setOnClick(std::bind(&HouseChoiceMenu::onHouseRight, this));
    houseRightButton.setVisible(true);
    windowWidget.addWidget( &houseRightButton, Point(320 + 85, 250), getTextureSize(pArrowRight));

    auto label = [this](const char* text, int x, int y) {
        auto* item = Label::create(_(text));
        item->setTextFontSize(12);
        item->setTextColor(COLOR_WHITE);
        item->setAlignment(Alignment_Left);
        windowWidget.addWidget(item, Point(x, y), Point(256, 18));
    };
    label("Start from", 48, 294);
    populateLevels();
    startLevelDropDown.setOnSelectionChange([this](bool) { s_startLevel = startLevelDropDown.getSelectedEntryIntData(); });
    windowWidget.addWidget(&startLevelDropDown, Point(48, 315), Point(256, 22));
    label("DuneCity skin", 48, 339);
    campaignSkinDropDown.addEntry(_("SimCity"), 0);
    campaignSkinDropDown.addEntry(_("Dune2"), 1);
    campaignSkinDropDown.setSelectedItem(s_campaignSkin);
    campaignSkinDropDown.setOnSelectionChange([this](bool interactive) {
        if(interactive) s_campaignSkin = campaignSkinDropDown.getSelectedEntryIntData() == 1 ? 1 : 0;
    });
    campaignSkinDropDown.setEnabled(ModManager::instance().isCityModeActive());
    windowWidget.addWidget(&campaignSkinDropDown, Point(160, 337), Point(144, 24));

    label("Campaign mod", 48, 365);
    availableMods = ModManager::instance().listModChoices();
    int activeIndex = 0;
    for(size_t i = 0; i < availableMods.size(); ++i) {
        const auto& mod = availableMods[i];
        modDropDown.addEntry(mod.selectionLabel(), static_cast<int>(i));
        if(mod.matchesSelectionName(ModManager::instance().getActiveModName())) activeIndex = static_cast<int>(i);
    }
    modDropDown.setSelectedItem(activeIndex);
    modDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onModSelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&modDropDown, Point(48, 386), Point(256, 22));
    modDescription.setTextFontSize(11);
    modDescription.setTextColor(COLOR_WHITE);
    windowWidget.addWidget(&modDescription, Point(48, 411), Point(256, 38));
    updateModDescription();

    label("AI partner", 336, 294);
    supportBotDropDown.addEntry(_("None"), 0);
    supportBotDropDown.addEntry(_("AI Support (Easy)"), 1);
    supportBotDropDown.addEntry(_("AI Support (Medium)"), 2);
    supportBotDropDown.addEntry(_("AI Support (Hard)"), 3);
    supportBotDropDown.addEntry(_("AI Support (Brutal)"), 4);
    supportBotDropDown.addEntry(_("QuantBot Easy"), 5);
    supportBotDropDown.addEntry(_("QuantBot Medium"), 6);
    supportBotDropDown.addEntry(_("QuantBot Hard"), 7);
    supportBotDropDown.addEntry(_("QuantBot Brutal"), 8);
    supportBotDropDown.addEntry(_("QuantBot Defend"), 9);
    supportBotDropDown.setSelectedItem(s_supportBotIndex);
    supportBotDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onSupportBotSelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&supportBotDropDown, Point(336, 315), Point(256, 22));
    supportDescription.setTextFontSize(10);
    supportDescription.setTextColor(COLOR_WHITE);
    supportDescription.setAlignment(Alignment_Left);
    windowWidget.addWidget(&supportDescription, Point(336, 339), Point(256, 30));
    onSupportBotSelectionChanged(false);

    label("Enemy AI", 336, 377);
    enemyAIDropDown.addEntry(_("QuantBot Easy"), 0);
    enemyAIDropDown.addEntry(_("QuantBot Medium"), 1);
    enemyAIDropDown.addEntry(_("QuantBot Hard"), 2);
    enemyAIDropDown.addEntry(_("QuantBot Brutal"), 3);
    enemyAIDropDown.addEntry(_("QuantBot Defend"), 4);
    enemyAIDropDown.addEntry(_("Campaign AI"), 5);
    enemyAIDropDown.setSelectedItem(s_enemyAIIndex);
    enemyAIDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onEnemyAISelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&enemyAIDropDown, Point(336, 398), Point(256, 22));
    enemyDescription.setTextFontSize(10);
    enemyDescription.setTextColor(COLOR_WHITE);
    enemyDescription.setAlignment(Alignment_Left);
    windowWidget.addWidget(&enemyDescription, Point(336, 423), Point(256, 30));
    onEnemyAISelectionChanged(false);

    gameOptionsButton.setText(_("Game Rules"));
    gameOptionsButton.setOnClick(std::bind(&HouseChoiceMenu::onGameOptions, this));
    windowWidget.addWidget(&gameOptionsButton, Point(184, 455), Point(128, 24));
    hostCoopButton.setOnClick([this]() {
        // Launch from the widgets, not only their last change notification.
        onSupportBotSelectionChanged(false);
        onEnemyAISelectionChanged(false);
        s_startLevel = startLevelDropDown.getSelectedEntryIntData();
        quit(s_house);
    });
    windowWidget.addWidget(&hostCoopButton, Point(448,455), Point(144,24));
    backButton.setText(_("Back"));
    backButton.setOnClick([this] { quit(); });
    windowWidget.addWidget(&backButton, Point(48,455), Point(128,24));
    loadButton.setText(_("Load Save"));
    loadButton.setOnClick([]() { showGameLibrary(); });
    windowWidget.addWidget(&loadButton, Point(320,455), Point(120,24));
    updateConnection();
    updateHouseChoice();
}

HouseChoiceMenu::~HouseChoiceMenu() = default;

void HouseChoiceMenu::onChildWindowClose(Window* pChildWindow) {
    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        s_currentGameOptions = pGameOptionsWindow->getGameOptions();
        // Rules stay local unless Remember is selected in the rules dialog.
    }
}

void HouseChoiceMenu::onGameOptions() {
    openWindow(GameOptionsWindow::create(s_currentGameOptions));
}

void HouseChoiceMenu::onSupportBotSelectionChanged(bool /*interactive*/) {
    int entry = supportBotDropDown.getSelectedEntryIntData();
    s_supportBotIndex = (entry >= 0 && entry < kSupportOptionCount) ? entry : 0;
    const char* descriptions[] = {
        "You control economy, building and combat.",
        "AI builds and manages your economy.\nYou command combat units.",
        "AI builds and manages your economy.\nYou command combat units.",
        "AI builds and manages your economy.\nYou command combat units.",
        "AI builds and manages your economy.\nYou command combat units.",
        "Full control: cautious attacks, home reserves.\nBuilds economy and covers power demand.",
        "Full control: balanced attacks and repairs.\nBuilds economy and covers power demand.",
        "Full control: larger armies and air raids.\nKeeps a small home reserve.",
        "Full control: strongest army and air raids.\nCommits most troops to combat.",
        "Builds your economy and defends your base."
    };
    supportDescription.setText(_(descriptions[s_supportBotIndex]));
}

void HouseChoiceMenu::onEnemyAISelectionChanged(bool /*interactive*/) {
    int entry = enemyAIDropDown.getSelectedEntryIntData();
    s_enemyAIIndex = (entry >= 0 && entry < kEnemyAIOptionCount) ? entry : 0;
    const char* descriptions[] = {
        "Small waves, one enemy house at a time.\nLong recovery breaks; supplies its power.",
        "Larger waves, one enemy house at a time.\nModerate breaks; supplies its power.",
        "Two houses can attack together.\nFlanking, air raids and shorter breaks.",
        "All enemy houses can attack together.\nLargest waves and shortest breaks.",
        "Builds and defends without main assaults.",
        "Uses the original campaign AI behaviour."
    };
    enemyDescription.setText(_(descriptions[s_enemyAIIndex]));
}

void HouseChoiceMenu::onHouseButton(int button) {
    int selectedHouse = houseOrder[currentHouseChoiceScrollPos+button];

    const HOUSETYPE selectedIdentity =
        getHouseFactionIdentity(static_cast<HOUSETYPE>(selectedHouse));
    switch(selectedIdentity) {
        case HOUSE_HARKONNEN:   soundPlayer->playVoice(HouseHarkonnen, selectedHouse); break;
        case HOUSE_ATREIDES:    soundPlayer->playVoice(HouseAtreides, selectedHouse);  break;
        case HOUSE_ORDOS:       soundPlayer->playVoice(HouseOrdos, selectedHouse);     break;
        case HOUSE_FREMEN:      soundPlayer->playVoice(HouseAtreides, selectedHouse);  break;
        case HOUSE_SARDAUKAR:   soundPlayer->playVoice(HouseHarkonnen, selectedHouse); break;
        case HOUSE_MERCENARY:   soundPlayer->playVoice(HouseOrdos, selectedHouse);     break;
        case HOUSE_NEUTRAL:
        case HOUSE_WILDSPADE:
            soundPlayer->playVoice(HouseAtreides, selectedHouse);
            break;
        case HOUSE_REBELS:
        case HOUSE_KLESHMERSH:
            soundPlayer->playVoice(HouseHarkonnen, selectedHouse);
            break;
        case HOUSE_CUSTOM:
        case HOUSE_THARPIQUE: {
            const HOUSETYPE fallbackHouse =
                getHouseFallbackHouse(static_cast<HOUSETYPE>(selectedHouse));
            switch(fallbackHouse) {
                case HOUSE_ATREIDES:
                case HOUSE_FREMEN:
                case HOUSE_NEUTRAL:
                    soundPlayer->playVoice(HouseAtreides, selectedHouse);
                    break;
                case HOUSE_ORDOS:
                case HOUSE_MERCENARY:
                    soundPlayer->playVoice(HouseOrdos, selectedHouse);
                    break;
                default:
                    soundPlayer->playVoice(HouseHarkonnen, selectedHouse);
                    break;
            }
        } break;
        default:
            break;
    }

    s_house = selectedHouse;
    updateHouseChoice();
}


void HouseChoiceMenu::populateLevels() {
    startLevelDropDown.clearAllEntries();
    s_startLevel = std::clamp(s_startLevel, 1, s_singleMission ? 22 : 9);
    for(int i = 1; i <= (s_singleMission ? 22 : 9); ++i)
        startLevelDropDown.addEntry((s_singleMission ? _("Mission ") : _("Level ")) + std::to_string(i), i);
    startLevelDropDown.setSelectedItem(s_startLevel - 1);
}

void HouseChoiceMenu::updateConnection() {
    const bool lobby = showLobby || !OnlineModPolicy::approved();
    onlineDescription.setText(s_online
        ? _("Public: others can watch or ask to join.\nChoose Private for invite-only play.")
        : _("Play the campaign on this computer.\nStart directly with your chosen house and rules."));
    hostCoopButton.setText(s_online && lobby ? _("Create Lobby") : s_singleMission ? _("Start Mission") : _("Start Campaign"));
    if(s_online && !OnlineModPolicy::approved())
        onlineDescription.setText(_("New mods use a pregame lobby.\nPlayers must join before the game starts."));
    visibilityDropDown.setEnabled(s_online);
    visibilityDropDown.setVisible(s_online);
    supportBotDropDown.setEnabled(!s_online || !lobby);
    if(s_online && lobby) supportDescription.setText(_("Choose your partner in the pregame lobby."));
    else onSupportBotSelectionChanged(false);
}

void HouseChoiceMenu::updateHouseChoice() {
    if(!isHouseAvailable(static_cast<HOUSETYPE>(s_house))) s_house = HOUSE_ATREIDES;
    selectedHouseLabel.setText(_("Selected:") + std::string("\n") + getHouseNameByNumber(static_cast<HOUSETYPE>(s_house)));

    // House1 button
    house1Button.setTextures(pGFXManager->getUIGraphic(UI_Herald_ColoredLarge, houseOrder[currentHouseChoiceScrollPos+0]));

    // House2 button
    house2Button.setTextures(pGFXManager->getUIGraphic(UI_Herald_ColoredLarge, houseOrder[currentHouseChoiceScrollPos+1]));

    // House3 button
    house3Button.setTextures(pGFXManager->getUIGraphic(UI_Herald_ColoredLarge, houseOrder[currentHouseChoiceScrollPos+2]));
}

void HouseChoiceMenu::onHouseLeft()
{
    if(currentHouseChoiceScrollPos > 0) {
        currentHouseChoiceScrollPos--;
        updateHouseChoice();
    }
}

void HouseChoiceMenu::onHouseRight()
{
    if(currentHouseChoiceScrollPos < getMaxHouseScrollPos()) {
        currentHouseChoiceScrollPos++;
        updateHouseChoice();
    }
}

void HouseChoiceMenu::updateModDescription() {
    const int index = modDropDown.getSelectedEntryIntData();
    if(index < 0 || index >= static_cast<int>(availableMods.size())) return;
    const auto& mod = availableMods[index];
    const std::string description = mod.description.empty()
        ? _("Use this mod's campaign content and rules.") : mod.description;
    modDescription.setText(description);
}

void HouseChoiceMenu::onModSelectionChanged(bool interactive) {
    if(!interactive) return;
    const int index = modDropDown.getSelectedEntryIntData();
    if(index < 0 || index >= static_cast<int>(availableMods.size())) return;
    auto& manager = ModManager::instance();
    if(availableMods[index].name != manager.getActiveModName()) {
        if(!manager.setActiveMod(availableMods[index].name)) {
            for(size_t i = 0; i < availableMods.size(); ++i)
                if(availableMods[i].name == manager.getActiveModName())
                    modDropDown.setSelectedItem(static_cast<int>(i));
            openWindow(MsgBox::create(_("Could not load that mod. The previous mod is still selected.")));
        } else {
            effectiveGameOptions = manager.loadEffectiveGameOptions(settings.gameOptions);
            s_currentGameOptions = effectiveGameOptions;
        }
    }
    currentHouseChoiceScrollPos = std::min(currentHouseChoiceScrollPos, getMaxHouseScrollPos());
    updateHouseChoice();
    updateModDescription();
    campaignSkinDropDown.setEnabled(manager.isCityModeActive());
    updateConnection();
}
