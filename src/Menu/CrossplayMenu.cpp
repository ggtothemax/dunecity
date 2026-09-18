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

#include <Menu/CrossplayMenu.h>
#include <mod/ModManager.h>
#include <Menu/PlaySetup.h>
#include <Menu/SinglePlayerMenu.h>
#include <Menu/MultiPlayerMenu.h>

#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/SinglePlayerSkirmishMenu.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>

#include <GUI/MsgBox.h>

#include <Network/DirectPeerConnection.h>
#include <Network/DirectRoomTransport.h>
#include <Network/RoomSessionTransport.h>
#include <Network/NetworkManager.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <config.h>
#include <globals.h>
#include <main.h>
#include <misc/FileSystem.h>
#include <misc/WebRuntime.h>
#include <players/QuantBotConfig.h>

#include <algorithm>

namespace {

/// A run of exactly 16 lowercase hex characters, which is what the content checksums are.
bool isChecksumToken(const std::string& value) {
    if(value.size() != 16) {
        return false;
    }
    return RoomRelay::isLowercaseHex(value);
}

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if(first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

std::string CrossplayMenu::contentFingerprint() {
    // The relay compares this between the host and anybody joining, so a mismatched install is
    // reported before a socket is opened. It is the same material the lobby exchanges in its own
    // config check, which stays the authority.
    const std::string quantBot = getQuantBotConfig().getConfigHash();
    const std::string objectData = getObjectDataHash();
    if(!isChecksumToken(quantBot) || !isChecksumToken(objectData)) {
        // Something could not be hashed locally. An empty fingerprint is not a wildcard and must
        // never be sent as one: callers treat it as "this install cannot be checked" and refuse
        // to go online, because two installs that both failed to hash themselves would otherwise
        // match each other and neither would have verified anything.
        return std::string();
    }
    return quantBot + objectData;
}

CrossplayMenu::CrossplayMenu() : MenuBase() {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
    setWindowWidget(&windowWidget);
    captionLabel.setText(_("Join Online"));
    captionLabel.setTextFontSize(22);
    captionLabel.setAlignment(Alignment_HCenter);
    playerNameLabel.setText(_("Player name"));
    playerNameTextBox.setText(settings.general.playerName);
    playerNameTextBox.setMaximumTextLength(20);
    confirmNameButton.setText(_("Confirm name for chat"));
    confirmNameButton.setOnClick([this]() { confirmChatName(); });
    playerNameTextBox.setOnReturn([this]() { if(showChat) confirmChatName(); });
    modeFilter.addEntry(_("All games"), 0);
    modeFilter.addEntry(_("Campaign co-op"), 1);
    modeFilter.addEntry(_("Custom games"), 2);
    modeFilter.setSelectedItem(0);
    modeFilter.setOnSelectionChange([this](bool interactive) { if(interactive) refreshDirectory(); });
    availableMods = ModManager::instance().listMods();
    for(size_t i = 0; i < availableMods.size(); ++i) {
        modFilter.addEntry(availableMods[i].displayName, static_cast<int>(i));
        if(availableMods[i].name == ModManager::instance().getActiveModName()) modFilter.setSelectedItem(i);
    }
    modFilter.setOnSelectionChange([this](bool interactive) {
        const int index = modFilter.getSelectedIndex();
        if(!interactive || stage != Stage::Choosing || index < 0 || index >= static_cast<int>(availableMods.size())) return;
        if(ModManager::instance().setActiveMod(availableMods[index].name)) {
            effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
            directory.cancel(); directoryPending = false;
            allPublicGames.clear(); refreshDirectory(); refreshPublicGames();
        }
    });
    refreshGamesButton.setText(_("Refresh"));
    refreshGamesButton.setOnClick([this]() { refreshPublicGames(); });
    moreGamesButton.setText(_("More"));
    moreGamesButton.setOnClick([this]() { refreshPublicGames(nextDirectoryPage); });
    publicGameList.setOnSelectionChange([this](bool) { refreshControls(); });
    publicGameList.setOnDoubleClick([this]() { joinPublicGame(); });
    joinPublicButton.setText(_("Join selected game"));
    joinPublicButton.setOnClick([this]() { joinPublicGame(); });
    joinLabel.setText(_("Invite code"));
    joinCodeTextBox.setMaximumTextLength(16);
    joinCodeTextBox.setOnReturn([this]() { onJoin(); });
    joinButton.setText(_("Join with code"));
    joinButton.setOnClick([this]() { onJoin(); });
    hostCustomGameButton.setText(_("Create Custom Game"));
    hostCustomGameButton.setOnClick([this]() { onHostCustomGame(); });
    hostCoopButton.setText(_("Create Campaign"));
    hostCoopButton.setOnClick([this]() { onHostCampaignCoop(); });
    visibilityChoice.addEntry(_("Private - invite code"));
    visibilityChoice.addEntry(_("Public - anyone can join"));
    visibilityChoice.setSelectedItem(0);
    chatToggle.setText(_("Public chat"));
    chatToggle.setOnClick([this]() { showChat = !showChat; layoutControls(); refreshControls(); });
    chatLabel.setText(_("Confirm your name to enter public chat."));
    chatInput.setMaximumTextLength(120);
    chatInput.setOnReturn([this]() { sendLobbyChat(); });
    chatSendButton.setText(_("Send"));
    chatSendButton.setOnClick([this]() { sendLobbyChat(); });
#ifdef __EMSCRIPTEN__
    otherConnections.setText(_("Find Match"));
#else
    otherConnections.setText(_("LAN / direct connection"));
#endif
    otherConnections.setOnClick([]() { MultiPlayerMenu().showMenu(); });
    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { onBack(); });
    statusLabel.setTextFontSize(12);
    preparedSummary.setTextFontSize(14);
    showPrivateJoin = true;
    layoutControls();
    if(settings.network.activeDirectEndpoint().empty() || !isDirectPeerConnectionAvailable()) {
        setStatus(_("Online play is unavailable in this build."));
        stage = Stage::Finished;
    } else setStatus(_("Join a game, enter an invite code, or create your own."));
    refreshControls();
    if(stage == Stage::Choosing) refreshPublicGames();
}

CrossplayMenu::CrossplayMenu(const GameInitSettings& game, bool publicGame, const ChangeEventList& players)
    : CrossplayMenu() {
    directory.cancel();
    directoryPending = false;
    preparedGame = std::make_unique<GameInitSettings>(game);
    preparedPlayers = players;
    hostingCoop = isCoopGameType(game.getGameType());
    visibilityChoice.setSelectedItem(publicGame ? 1 : 0);
    autoHostRequested = stage == Stage::Choosing;
    captionLabel.setText(hostingCoop ? _("Campaign co-op") : _("Custom Game"));
    preparedSummary.setText(game.getModName() + "\n" + game.getFilename()
        + "\n" + (publicGame ? _("Public game") : _("Private game - invite by code")));
    layoutControls();
    refreshControls();
}

void CrossplayMenu::layoutControls() {
    windowWidget.removeAllChildWidgets();
    const int x = 20, w = getSize().x - 40;
    const int h = getSize().y;
    auto place = [this](Widget* widget, int x, int y, int width, int height) {
        windowWidget.addWidget(widget, Point(x,y), Point(width,height));
    };
    place(&captionLabel,x,12,w,30);
    place(&playerNameLabel,x,49,95,24);
    place(&playerNameTextBox,x+100,49,200,26);
    if(preparedGame) {
        place(&preparedSummary,x+20,110,w-40,150);
        place(&refreshGamesButton,x+20,280,160,30);
        refreshGamesButton.setText(_("Retry connection"));
        refreshGamesButton.setOnClick([this]() { if(stage == Stage::Choosing) beginAdmission(true); });
    } else {
        place(&chatToggle,x+w-145,49,145,26);
        if(showChat) {
            place(&confirmNameButton,x,88,220,28);
            place(&chatLabel,x,122,w,22);
            place(&chatHistory,x,149,w,h-297);
            place(&chatInput,x,h-140,w-95,28);
            place(&chatSendButton,x+w-90,h-140,90,28);
        } else {
            place(&modeFilter,x,88,180,28);
            place(&modFilter,x+190,88,w-370,28);
            place(&refreshGamesButton,x+w-170,88,90,28);
            place(&moreGamesButton,x+w-75,88,75,28);
            place(&directoryLabel,x,119,w,22);
            place(&publicGameList,x,145,w,h-326);
            place(&joinPublicButton,x+w-190,h-176,190,30);
            place(&joinLabel,x,h-137,90,26);
            place(&joinCodeTextBox,x+95,h-137,w-245,28);
            place(&joinButton,x+w-145,h-137,145,28);
            place(&hostCoopButton,x,h-99,(w-10)/2,28);
            place(&hostCustomGameButton,x+(w+10)/2,h-99,(w-10)/2,28);
        }
    }
    place(&statusLabel,x,h-65,w,26);
    place(&backButton,x,h-34,95,28);
    // In the browser this opens the matchmaking lobby (Find Match), natively
    // the LAN/direct connection screen; the button is the same MultiPlayerMenu
    // route either way.
    if(!preparedGame) place(&otherConnections,x+w-215,h-34,215,28);
}

void CrossplayMenu::refreshDirectory() {
    std::string selectedRoom;
    const int selected = publicGameList.getSelectedIndex();
    if(selected >= 0 && selected < static_cast<int>(publicGames.size())) selectedRoom = publicGames[selected].roomCode;
    publicGameList.clearAllEntries();
    publicGames.clear();
    const int filter = modeFilter.getSelectedIndex();
    for(const auto& game : allPublicGames) {
        if((filter == 1 && game.mode != "coop") || (filter == 2 && game.mode != "custom")) continue;
        publicGames.push_back(game);
        publicGameList.addEntry(game.hostName + " - " + (game.mode == "coop" ? _("Campaign co-op") : _("Custom game"))
            + " - " + std::to_string(game.players) + "/" + std::to_string(game.maxPeers));
        if(game.roomCode == selectedRoom) publicGameList.setSelectedItem(static_cast<int>(publicGames.size())-1);
    }
    directoryLabel.setText(publicGames.empty() ? _("No matching games. Create one or join with a code.") : _("Available games"));
    refreshControls();
}

CrossplayMenu::~CrossplayMenu() {
    directory.cancel();
    chat.cancel();
    visibilityUpdate.cancel();
    admission.cancel();
    visibilityUpdate.cancel();
    visibilityPending = false;
    if(pNetworkManager != nullptr && pNetworkManager->isRelaySession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager.reset();
    }
}

void CrossplayMenu::setStatus(const std::string& message) {
    statusText = message;
    statusLabel.setText(message);
}

void CrossplayMenu::refreshControls() {
    const bool idle = (stage == Stage::Choosing);
    modFilter.setEnabled(idle);
    modeFilter.setEnabled(idle);
    const bool busy = (stage == Stage::Requesting) || (stage == Stage::Connecting);

    hostCustomGameButton.setEnabled(idle);
    hostCoopButton.setEnabled(idle);
    const bool invite = idle && showPrivateJoin;
    joinLabel.setVisible(invite);
    joinButton.setVisible(invite);
    joinCodeTextBox.setVisible(invite);
    privateInviteButton.setVisible(idle);
    privateInviteButton.setText(showPrivateJoin ? _("Hide private invite") : _("Join with invite code"));
    joinButton.setEnabled(invite);
    joinCodeTextBox.setEnabled(invite);
    playerNameTextBox.setEnabled(idle && chatSession.empty() && !chatPending);
    confirmNameButton.setEnabled(idle || (chatSession.empty() && !chatPending
        && (stage == Stage::HostReady || stage == Stage::ClientWaiting)));
    confirmNameButton.setText(chatSession.empty() ? _("Confirm name for chat") : _("Change name"));
    chatInput.setEnabled(!chatSession.empty());
    chatSendButton.setEnabled(!chatSession.empty() && !chatPending);
    visibilityChoice.setEnabled(idle || (stage == Stage::HostReady
        && !visibilityPending && !grantedRoom.controlToken.empty()));
    // A background directory refresh must not steal selection/keyboard focus.
    // Joining a cached listing is safe: admission validates that it is still open.
    publicGameList.setEnabled(idle);
    refreshGamesButton.setEnabled(idle && !directoryPending);
    moreGamesButton.setEnabled(idle && !directoryPending && nextDirectoryPage > 0);
    const int selected = publicGameList.getSelectedIndex();
    joinPublicButton.setText(_("Join Game"));
    joinPublicButton.setEnabled(idle && selected >= 0
        && static_cast<std::size_t>(selected) < publicGames.size());

    // Once in a room as the host, the two host buttons become "what do you want to play".
    if(stage == Stage::HostReady) {
        hostCustomGameButton.setEnabled(!hostingCoop && !visibilityPending);
        hostCoopButton.setEnabled(hostingCoop && !visibilityPending);
        hostCustomGameButton.setText(_("Choose a Map"));
        hostCoopButton.setText(_("Choose a Campaign Mission"));
    }

    const bool showCode = !publicRoom && !roomCode.empty() && stage == Stage::HostReady;
    roomCodeLabel.setText(showCode ? (_("Game code: ") + roomCode) : std::string());
    copyCodeButton.setVisible(showCode);
    copyCodeButton.setEnabled(showCode);
    copyCodeButton.setText(_("Copy code"));

    backButton.setEnabled(true);
    backButton.setText(busy ? _("Cancel") : _("Back"));
    if(preparedGame) refreshGamesButton.setEnabled(idle);
}

void CrossplayMenu::refreshPublicGames(unsigned offset) {
    // Returning from Create Campaign/Custom may have changed the active mod.
    for(size_t i=0; i<availableMods.size(); ++i)
        if(availableMods[i].name == ModManager::instance().getActiveModName()) modFilter.setSelectedItem(i);
    if((stage != Stage::Choosing && stage != Stage::HostReady && stage != Stage::ClientWaiting)
       || directoryPending) return;
    nextDirectoryRefresh = SDL_GetTicks() + 15000;
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        directoryLabel.setText(_("Cannot check game content"));
        return;
    }
    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.contentHash = fingerprint;
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.listing = true;
    request.listOffset = offset;
    directoryPending = true;
    directoryLabel.setText(_("Finding public games..."));
    directory.begin(request);
    nextDirectoryRefresh = SDL_GetTicks() + 15000;
    refreshControls();
}

void CrossplayMenu::joinPublicGame() {
    const int index = publicGameList.getSelectedIndex();
    if(stage != Stage::Choosing || directoryPending || index < 0
       || static_cast<std::size_t>(index) >= publicGames.size()) return;
    beginAdmission(false, true);
}

AdmissionRequest CrossplayMenu::lobbyRequest() const {
    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.contentHash = contentFingerprint();
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    return request;
}

void CrossplayMenu::changeVisibility() {
    if(stage == Stage::Choosing) { publicRoom = visibilityChoice.getSelectedIndex() == 1; return; }
    if(stage != Stage::HostReady || visibilityPending || grantedRoom.controlToken.empty()) return;
    auto request = lobbyRequest();
    request.operation = AdmissionOperation::Visibility;
    request.roomCode = roomCode;
    request.controlToken = grantedRoom.controlToken;
    request.publicRoom = visibilityChoice.getSelectedIndex() == 1;
    visibilityPending = true;
    setStatus(_("Updating game visibility..."));
    visibilityUpdate.begin(request);
    refreshControls();
}

void CrossplayMenu::confirmChatName() {
    if(stage == Stage::Choosing && !chatSession.empty()) {
        chat.cancel();
        chatPending = false;
        chatSession.clear();
        chatLabel.setText(_("Enter a name above, then confirm it to chat."));
        refreshControls();
        return;
    }
    if(chatPending || !chatSession.empty() || !validateAndSavePlayerName()) return;
    auto request = lobbyRequest();
    if(request.contentHash.empty()) { chatLabel.setText(_("Cannot check game content.")); return; }
    request.operation = AdmissionOperation::ChatEnter;
    request.displayName = playerNameTextBox.getText();
    chatAction = request.operation;
    chatPending = true;
    chatLabel.setText(_("Confirming your name..."));
    chat.begin(request);
    refreshControls();
}

void CrossplayMenu::sendLobbyChat() {
    if(chatSession.empty() || chatPending) return;
    const auto text = trimmed(chatInput.getText());
    if(text.empty()) return;
    if(text.size() > 120) { chatLabel.setText(_("Message is too long. Please shorten it.")); return; }
    auto request = lobbyRequest();
    request.operation = AdmissionOperation::ChatSay;
    request.chatSession = chatSession;
    request.chatText = text;
    chatAction = request.operation;
    chatPending = true;
    chat.begin(request);
    refreshControls();
}

void CrossplayMenu::updateLobbyChat() {
    chat.update();
    if(chatPending && chat.status() != RoomAdmissionClient::Status::InProgress) {
        chatPending = false;
        if(chat.status() == RoomAdmissionClient::Status::Succeeded) {
            const auto& response = chat.response();
            if(chatAction == AdmissionOperation::ChatEnter) {
                chatSession = response.chatSession;
                chatCursor = response.chatCursor;
            } else if(chatAction == AdmissionOperation::ChatSay) {
                chatInput.setText("");
            } else if(chatAction == AdmissionOperation::ChatPoll) {
                if(response.chatCursor < chatCursor) {
                    chatSession.clear();
                    chatLabel.setText(_("Chat restarted. Confirm your name again."));
                } else {
                    if(response.chatGap) chatLines.push_back(_("Older lobby messages have expired."));
                    for(const auto& message : response.messages) {
                        if(message.id > chatCursor) chatLines.push_back(message.name + ": " + message.text);
                    }
                    chatCursor = response.chatCursor;
                    if(chatLines.size() > 60) chatLines.erase(chatLines.begin(), chatLines.end() - 60);
                    if(!response.messages.empty() || response.chatGap) {
                        std::string text;
                        for(const auto& line : chatLines) text += line + "\n";
                        chatHistory.setText(text);
                        chatHistory.scrollToEnd();
                    }
                }
            }
            if(!chatSession.empty()) chatLabel.setText(_("Public lobby chat - ") + playerNameTextBox.getText());
            nextChatPoll = SDL_GetTicks() + (chatAction == AdmissionOperation::ChatPoll ? 5000 : 0);
        } else {
            chatLabel.setText(chat.errorMessage());
            if(chat.response().errorCode == "session_expired") chatSession.clear();
            nextChatPoll = SDL_GetTicks() + 15000;
        }
        chat.cancel();
        refreshControls();
    }
    if(!chatPending && !chatSession.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), nextChatPoll)) {
        auto request = lobbyRequest();
        request.operation = AdmissionOperation::ChatPoll;
        request.chatSession = chatSession;
        request.chatCursor = chatCursor;
        chatAction = request.operation;
        chatPending = true;
        chat.begin(request);
        refreshControls();
    }
}

bool CrossplayMenu::validateAndSavePlayerName() {
    const std::string name = trimmed(playerNameTextBox.getText());
    if(name.empty() || !RoomRelay::isAcceptableDisplayName(name)) {
        openWindow(MsgBox::create(_("Please enter a player name.")));
        return false;
    }

    playerNameTextBox.setText(name);
    if(name != settings.general.playerName) {
        settings.general.playerName = name;
        INIFile configFile(getConfigFilepath());
        configFile.setStringValue("General", "Player Name", settings.general.playerName);
        configFile.saveChangesTo(getConfigFilepath());
    }
    return true;
}

void CrossplayMenu::onHostCustomGame() {
    if(!validateAndSavePlayerName()) return;
    playCustomGame(true);
    refreshPublicGames();
}
void CrossplayMenu::onHostCampaignCoop() {
    if(!validateAndSavePlayerName()) return;
    SinglePlayerMenu::playCampaign(true);
    refreshPublicGames();
}

void CrossplayMenu::onJoin() {
    std::string normalized;
    if(!RoomRelay::normalizeRoomCode(joinCodeTextBox.getText(), normalized)) {
        openWindow(MsgBox::create(_("That game code is not valid. Codes look like ABCD-EFGH-JKMN.")));
        return;
    }
    joinCodeTextBox.setText(normalized);
    beginAdmission(false);
}

void CrossplayMenu::onBack() {
    teardownSession(std::string());
    quit();
}

void CrossplayMenu::beginAdmission(bool hosting, bool publicJoin) {
    const auto previousName = settings.general.playerName;
    if(!validateAndSavePlayerName()) {
        return;
    }
    if(hosting && preparedGame && previousName != settings.general.playerName) {
        auto houses = preparedGame->getHouseInfoList();
        preparedGame->clearHouseInfo();
        for(auto& house : houses) {
            for(auto& player : house.playerInfoList)
                if(player.playerName == previousName && player.playerClass == HUMANPLAYERCLASS)
                    player.playerName = settings.general.playerName;
            preparedGame->addHouseInfo(house);
        }
        for(auto& event : preparedPlayers.changeEventList)
            if(event.eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer && event.newStringValue == previousName)
                event.newStringValue = settings.general.playerName;
    }
    if(settings.network.activeDirectEndpoint().empty()) {
        setStatus(_("Online play has not been set up in this copy of the game."));
        return;
    }

    // Fail closed. Going online without being able to describe our own content would ask the
    // game service to match us against a fingerprint we never computed, and would leave the
    // lobby with nothing to compare either.
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        setStatus(_("This copy of the game could not check its own content files, "
                    "so it cannot play online. Reinstalling the game usually fixes this."));
        return;
    }

    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    request.contentHash = fingerprint;
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.hosting = hosting;
    request.publicRoom = visibilityChoice.getSelectedIndex() == 1;
    if(hosting) {
        // Co-op is a two-player arrangement; a custom game uses the lobby's own limit.
        request.mode = hostingCoop ? "coop" : "custom";
        request.maxPeers = hostingCoop ? 2 : 4;
    } else {
        request.publicOnly = publicJoin;
        request.roomCode = publicJoin ? publicGames[publicGameList.getSelectedIndex()].roomCode
                                      : joinCodeTextBox.getText();
    }

    pendingHosting = hosting;
    directory.cancel();
    directoryPending = false;
    stage = Stage::Requesting;
    setStatus(hosting ? _("Creating a game...") : _("Looking for that game..."));
    refreshControls();

    admission.begin(request);
}

void CrossplayMenu::openDirectSession() {
    // The fingerprint is recomputed rather than remembered: admission and the handshake must
    // describe the same install, and anything that changed in between has to be caught here.
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        setStatus(_("This copy of the game could not check its own content files, "
                    "so it cannot play online."));
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    // Direct only. The address comes from this installation's settings, never from the admission
    // answer: a service that could hand out a gameplay endpoint could move the match back onto a
    // server, which is the whole thing this transport exists to stop. grantedRoom.socketUrl is
    // deliberately ignored.
    DirectRoomTransport::Config config;
    config.signalingBaseUrl = settings.network.activeDirectEndpoint();
    config.grant       = grantedRoom.grant;
    config.roomCode    = grantedRoom.roomCode;
    config.displayName = settings.general.playerName;
    config.appVersion  = VERSIONSTRING;
    config.contentHash = fingerprint;
    config.gameProtocolVersion = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    config.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
#ifdef __EMSCRIPTEN__
    config.runtime = "browser";
#else
    config.runtime = "native";
#endif

    try {
        pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
    } catch(const std::exception& error) {
        setStatus(error.what());
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    std::string failure;
    if(!pNetworkManager->startDirectSession(config, failure)) {
        pNetworkManager.reset();
        setStatus(failure);
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    pNetworkManager->setOnReceiveGameInfo(
        std::bind(&CrossplayMenu::onReceiveGameInfo, this,
                  std::placeholders::_1, std::placeholders::_2));
    pNetworkManager->setOnPeerDisconnected(
        std::bind(&CrossplayMenu::onPeerDisconnected, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    roomCode = grantedRoom.roomCode;
    SDL_Log("Online lobby: admission granted (%s); opening direct session", pendingHosting ? "host" : "guest");
    publicRoom = grantedRoom.visibility == "public";
    pNetworkManager->setPublicRelayRoom(publicRoom);
    stage = Stage::Connecting;
    setStatus(_("Connecting..."));
    refreshControls();
}

void CrossplayMenu::teardownSession(std::string reason) {
    // Own the reason before releasing the relay whose status may contain it.
    pendingGameInfo.reset();
    pendingLobbyChanges = ChangeEventList();
    pendingDisconnectReason.clear();
    admission.cancel();
    visibilityUpdate.cancel();
    visibilityPending = false;
    if(pNetworkManager != nullptr && pNetworkManager->isRelaySession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager->disconnect();
        pNetworkManager.reset();
    }

    roomCode.clear();
    grantedRoom = AdmissionResponse();
    stage = Stage::Choosing;
    if(!reason.empty()) setStatus(reason);
    hostCustomGameButton.setText(_("Create Custom Game"));
    hostCoopButton.setText(_("Create Campaign Co-op"));
    refreshControls();
}

void CrossplayMenu::update() {
    if(autoHostRequested) { autoHostRequested = false; beginAdmission(true); }
    if(preparedGame && stage == Stage::HostReady) {
        int result;
        { CustomGamePlayers lobby(*preparedGame, true, false, nullptr, &preparedPlayers); result = lobby.showMenu(); }
        teardownSession({});
        quit(result);
        return;
    }
    if(!preparedGame && stage == Stage::Choosing && showChat) updateLobbyChat();
    visibilityUpdate.update();
    if(visibilityPending && visibilityUpdate.status() != RoomAdmissionClient::Status::InProgress) {
        visibilityPending = false;
        if(visibilityUpdate.status() == RoomAdmissionClient::Status::Succeeded) {
            publicRoom = visibilityUpdate.response().visibility == "public";
            if(!visibilityUpdate.response().roomCode.empty()) {
                roomCode = visibilityUpdate.response().roomCode;
                grantedRoom.roomCode = roomCode;
                if(pNetworkManager && pNetworkManager->getRelayClient())
                    pNetworkManager->getRelayClient()->updateInvitationCode(roomCode);
            }
            if(pNetworkManager) pNetworkManager->setPublicRelayRoom(publicRoom);
            setStatus(publicRoom ? _("Your public game is listed. Players can join from the lobby.")
                                 : _("Your private game is unlisted. Share its invitation code."));
        } else {
            setStatus(_("Visibility could not be confirmed. Choose it again to confirm your game and invitation code."));
        }
        visibilityChoice.setSelectedItem(publicRoom ? 1 : 0);
        visibilityUpdate.cancel();
        refreshControls();
    }
    directory.update();
    if(directoryPending && (directory.status() == RoomAdmissionClient::Status::Succeeded
                            || directory.status() == RoomAdmissionClient::Status::Failed)) {
        nextDirectoryPage = 0;
        directoryPending = false;
        if(directory.status() == RoomAdmissionClient::Status::Succeeded) {
            allPublicGames = directory.response().games;
            nextDirectoryPage = directory.response().nextPage;
            refreshDirectory();
        } else {
            allPublicGames.clear();
            refreshDirectory();
            directoryLabel.setText(_("Public list unavailable"));
            setStatus(directory.errorMessage());
        }
        directory.cancel();
        refreshControls();
    }
    if(!preparedGame && (stage == Stage::Choosing || stage == Stage::HostReady || stage == Stage::ClientWaiting)
       && !directoryPending
       && SDL_TICKS_PASSED(SDL_GetTicks(), nextDirectoryRefresh)) refreshPublicGames();
    // Network callbacks run inside NetworkManager::update(). Defer menu loops and
    // manager destruction until that dispatch has returned to MenuBase.
    if(!pendingDisconnectReason.empty()) {
        teardownSession(pendingDisconnectReason);
        return;
    }
    if(pendingGameInfo) {
        auto gameInfo = std::move(pendingGameInfo);
        auto changes = std::move(pendingLobbyChanges);
        enterReceivedLobby(*gameInfo, changes);
        return;
    }
    admission.update();

    if(stage == Stage::Requesting) {
        switch(admission.status()) {
            case RoomAdmissionClient::Status::Succeeded:
                grantedRoom = admission.response();
                admission.cancel();
                openDirectSession();
                break;
            case RoomAdmissionClient::Status::Failed:
                setStatus(admission.errorMessage());
                admission.cancel();
                stage = Stage::Choosing;
                refreshControls();
                break;
            default:
                break;
        }
        return;
    }

    if(pNetworkManager == nullptr || !pNetworkManager->isRelaySession()) {
        return;
    }

    RoomSessionTransport* relay = pNetworkManager->getRelayClient();
    if(relay == nullptr) {
        return;
    }

    if(stage == Stage::Connecting && relay->isJoined()) {
        roomCode = relay->roomCode();
        if(pendingHosting) {
            stage = Stage::HostReady;
            setStatus(publicRoom
                ? _("Your public game is listed. Opening the lobby...")
                : _("Your private game is open. Opening the lobby..."));
        } else {
            stage = Stage::ClientWaiting;
            setStatus(_("Connecting to the host and receiving game setup..."));
        }
        refreshControls();
        if(!preparedGame) refreshPublicGames();
        return;
    }

    if(relay->status() == RoomSessionTransport::Status::Closed
       && (stage == Stage::Connecting || stage == Stage::HostReady
           || stage == Stage::ClientWaiting)) {
        teardownSession(relay->statusMessage().empty()
            ? std::string(_("The connection to the game was lost."))
            : relay->statusMessage());
    }
}

void CrossplayMenu::onReceiveGameInfo(const GameInitSettings& gameInitSettings,
                                      const ChangeEventList& changeEventList) {
    if(pendingHosting || pendingGameInfo || !pendingDisconnectReason.empty()) {
        return;     // a host does not take a lobby from anybody
    }

    pendingGameInfo = std::make_unique<GameInitSettings>(gameInitSettings);
    pendingLobbyChanges = changeEventList;
    SDL_Log("Online lobby: received host setup; queued lobby transition at %u ms", SDL_GetTicks());
}

void CrossplayMenu::enterReceivedLobby(const GameInitSettings& gameInitSettings,
                                       const ChangeEventList& changeEventList) {

    setStatus(_("Joining the game..."));

    auto pCustomGamePlayers = std::make_unique<CustomGamePlayers>(gameInitSettings, false);
    pCustomGamePlayers->onReceiveChangeEventList(changeEventList);
    SDL_Log("Online lobby: showing guest roster at %u ms", SDL_GetTicks());
    const int result = pCustomGamePlayers->showMenu();
    pCustomGamePlayers.reset();

    switch(result) {
        case MENU_QUIT_DEFAULT:
            teardownSession(_("You left the game."));
            break;
        case MENU_QUIT_GAME_FINISHED:
            quit(MENU_QUIT_GAME_FINISHED);
            break;
        default:
            teardownSession(_("The connection to the game was lost."));
            break;
    }
}

void CrossplayMenu::onPeerDisconnected(const std::string& playerName, bool isHost, int cause) {
    if(!isHost) {
        return;
    }

    if(pNetworkManager && pNetworkManager->getRelayClient()) {
        const auto* relay = pNetworkManager->getRelayClient();
        if(relay->status() == RoomSessionTransport::Status::Closed && !relay->statusMessage().empty()) {
            pendingDisconnectReason = relay->statusMessage();
            return;
        }
    }
    std::string message;
    switch(cause) {
        case NETWORKDISCONNECT_TIMEOUT:
            message = _("The connection stopped responding.");
            break;
        case NETWORKDISCONNECT_GAME_FULL:
            message = _("There is no free player slot in this game left!");
            break;
        case NETWORKDISCONNECT_PROTOCOL_MISMATCH:
            message = _("That game was created by a different version of Dune City.");
            break;
        default:
            message = playerName.empty() ? std::string(_("The online game ended."))
                                         : (playerName + _(" left the game."));
            break;
    }
    // A host leaving arrives twice: once as that peer departing, once as the room closing. The
    // first carries the name and is the more useful sentence, so it is the one that is kept.
    if(pendingDisconnectReason.empty()) {
        pendingDisconnectReason = std::move(message);
    }
}
