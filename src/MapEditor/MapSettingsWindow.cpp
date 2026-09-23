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

#include <MapEditor/MapSettingsWindow.h>

#include <MapEditor/MapInfo.h>
#include <MapEditor/MapEditorOperation.h>

#include <GUI/Spacer.h>
#include <GUI/QstBox.h>
#include <GUI/MsgBox.h>

#include <INIMap/MapMetadata.h>

#include <globals.h>

#include <sand.h>

#include <MapEditor/MapEditor.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <mod/ModManager.h>

MapSettingsWindow::MapSettingsWindow(MapEditor* pMapEditor, HOUSETYPE currentHouse)
 : Window(0,0,0,0), pMapEditor(pMapEditor), house(currentHouse) {

    color = getHouseInterfaceColor(house);

    MapInfo& mapInfo = pMapEditor->getMapInfo();

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_NewMapWindow);
    setBackground(pBackground);

    setCurrentPosition(calcAlignedDrawingRect(pBackground, HAlign::Center, VAlign::Center));

    setWindowWidget(&mainHBox);

    mainHBox.addWidget(HSpacer::create(16));
    mainHBox.addWidget(&mainVBox);
    mainHBox.addWidget(HSpacer::create(16));

    titleLabel.setTextColor(COLOR_LIGHTYELLOW, COLOR_TRANSPARENT);
    titleLabel.setAlignment((Alignment_Enum) (Alignment_HCenter | Alignment_VCenter));
    titleLabel.setText(_("Map Settings"));
    mainVBox.addWidget(&titleLabel);

    mainVBox.addWidget(VSpacer::create(8));

    centralScroll.setContent(&centralVBox);
    mainVBox.addWidget(&centralScroll, 360);

    centralVBox.addWidget(&pictureHBox, 38);

    pictureHBox.addWidget(&winPictureVBox);
    winPictureLabel.setText(_("Picture on winning") + ":");
    winPictureLabel.setTextColor(color);
    winPictureVBox.addWidget(&winPictureLabel);
    winPictureVBox.addWidget(VSpacer::create(3));
    winPictureDropDownBox.setColor(color);

    availableWinPictures.emplace_back("WIN1.WSA");
    availableWinPictures.emplace_back("WIN2.WSA");

    for(size_t i = 0; i < availableWinPictures.size(); ++i) {
        winPictureDropDownBox.addEntry(availableWinPictures[i], i);
        if(availableWinPictures[i] == mapInfo.winPicture) {
            winPictureDropDownBox.setSelectedItem(i);
        }
    }

    if(winPictureDropDownBox.getSelectedIndex() < 0) {
        availableWinPictures.push_back(mapInfo.winPicture);
        winPictureDropDownBox.addEntry(mapInfo.winPicture, availableWinPictures.size()-1);
    }

    winPictureVBox.addWidget(&winPictureDropDownBox);

    pictureHBox.addWidget(HSpacer::create(8));

    pictureHBox.addWidget(&losePictureVBox);
    losePictureLabel.setText(_("Picture on losing") + ":");
    losePictureLabel.setTextColor(color);
    losePictureVBox.addWidget(&losePictureLabel);
    losePictureVBox.addWidget(VSpacer::create(3));
    losePictureDropDownBox.setColor(color);

    availableLosePictures.emplace_back("LOSTBILD.WSA");
    availableLosePictures.emplace_back("LOSTVEHC.WSA");

    for(size_t i = 0; i < availableLosePictures.size(); ++i) {
        losePictureDropDownBox.addEntry(availableLosePictures[i], i);
        if(availableLosePictures[i] == mapInfo.losePicture) {
            losePictureDropDownBox.setSelectedItem(i);
        }
    }

    if(losePictureDropDownBox.getSelectedIndex() < 0) {
        availableLosePictures.push_back(mapInfo.losePicture);
        losePictureDropDownBox.addEntry(mapInfo.losePicture, availableLosePictures.size()-1);
    }

    losePictureVBox.addWidget(&losePictureDropDownBox);

    pictureHBox.addWidget(HSpacer::create(8));

    pictureHBox.addWidget(&briefingPictureVBox);
    briefingPictureLabel.setText(_("Picture for briefing") + ":");
    briefingPictureLabel.setTextColor(color);
    briefingPictureVBox.addWidget(&briefingPictureLabel);
    briefingPictureVBox.addWidget(VSpacer::create(3));
    briefingPictureDropDownBox.setColor(color);

    availableBriefingPictures.emplace_back("HARVEST.WSA");
    availableBriefingPictures.emplace_back("HEADQRTS.WSA");
    availableBriefingPictures.emplace_back("QUAD.WSA");
    availableBriefingPictures.emplace_back("LTANK.WSA");
    availableBriefingPictures.emplace_back("REPAIR.WSA");
    availableBriefingPictures.emplace_back("HVYFTRY.WSA");
    availableBriefingPictures.emplace_back("IX.WSA");
    availableBriefingPictures.emplace_back("PALACE.WSA");
    availableBriefingPictures.emplace_back("SARDUKAR.WSA");

    for(size_t i = 0; i < availableBriefingPictures.size(); ++i) {
        briefingPictureDropDownBox.addEntry(availableBriefingPictures[i], i);
        if(availableBriefingPictures[i] == mapInfo.briefPicture) {
            briefingPictureDropDownBox.setSelectedItem(i);
        }
    }

    if(briefingPictureDropDownBox.getSelectedIndex() < 0) {
        availableBriefingPictures.push_back(mapInfo.briefPicture);
        briefingPictureDropDownBox.addEntry(mapInfo.briefPicture, availableBriefingPictures.size()-1);
    }

    briefingPictureVBox.addWidget(&briefingPictureDropDownBox);

    centralVBox.addWidget(VSpacer::create(15));

    gameFinishingConditionsLabel.setText(_("Conditions for finishing the game") + ":");
    gameFinishingConditionsLabel.setTextColor(color);
    centralVBox.addWidget(&gameFinishingConditionsLabel);

    centralVBox.addWidget(&winFlags1HBox);
    winFlagsTimeoutCheckbox.setText(_("Timeout reached") + ":");
    winFlagsTimeoutCheckbox.setTextColor(color);
    winFlagsTimeoutCheckbox.setChecked(mapInfo.winFlags & WINLOSEFLAGS_TIMEOUT);
    winFlags1HBox.addWidget(&winFlagsTimeoutCheckbox, 0.0);
    winFlagsTimeoutTextBox.setColor(house, color);
    winFlagsTimeoutTextBox.setMinMax(0,999);
    winFlagsTimeoutTextBox.setValue(mapInfo.timeout);
    winFlags1HBox.addWidget(&winFlagsTimeoutTextBox, 65);

    winFlags1HBox.addWidget(Spacer::create());

    winFlagsSpiceQuotaCheckbox.setText(_("Spice quota reached"));
    winFlagsSpiceQuotaCheckbox.setTextColor(color);
    winFlagsSpiceQuotaCheckbox.setChecked(mapInfo.winFlags & WINLOSEFLAGS_QUOTA);
    winFlags1HBox.addWidget(&winFlagsSpiceQuotaCheckbox, 284);


    centralVBox.addWidget(&winFlags2HBox);
    winFlagsPlayerNoObjectsLeftCheckbox.setText(_("Player has no units/structures left"));
    winFlagsPlayerNoObjectsLeftCheckbox.setTextColor(color);
    winFlagsPlayerNoObjectsLeftCheckbox.setChecked(mapInfo.winFlags & WINLOSEFLAGS_HUMAN_HAS_BUILDINGS);
    winFlags2HBox.addWidget(&winFlagsPlayerNoObjectsLeftCheckbox, 284);

    winFlags2HBox.addWidget(Spacer::create());

    winFlagsAIPlayerNoObjectsLeftCheckbox.setText(_("Others have no units/structures left"));
    winFlagsAIPlayerNoObjectsLeftCheckbox.setTextColor(color);
    winFlagsAIPlayerNoObjectsLeftCheckbox.setChecked(mapInfo.winFlags & WINLOSEFLAGS_AI_NO_BUILDINGS);
    winFlags2HBox.addWidget(&winFlagsAIPlayerNoObjectsLeftCheckbox, 284);


    centralVBox.addWidget(VSpacer::create(15));

    gameWinningConditionsLabel.setText(_("Conditions for winning the game (in case it is finished)") + ":");
    gameWinningConditionsLabel.setTextColor(color);
    centralVBox.addWidget(&gameWinningConditionsLabel);

    centralVBox.addWidget(&loseFlags1HBox);
    loseFlagsTimeoutCheckbox.setText(_("Timeout reached"));
    loseFlagsTimeoutCheckbox.setTextColor(color);
    loseFlagsTimeoutCheckbox.setChecked(mapInfo.loseFlags & WINLOSEFLAGS_TIMEOUT);
    loseFlags1HBox.addWidget(&loseFlagsTimeoutCheckbox, 0.0);

    loseFlags1HBox.addWidget(Spacer::create());

    loseFlagsSpiceQuotaCheckbox.setText(_("Spice quota reached"));
    loseFlagsSpiceQuotaCheckbox.setTextColor(color);
    loseFlagsSpiceQuotaCheckbox.setChecked(mapInfo.loseFlags & WINLOSEFLAGS_QUOTA);
    loseFlags1HBox.addWidget(&loseFlagsSpiceQuotaCheckbox, 284);


    centralVBox.addWidget(&loseFlags2HBox);
    loseFlagsPlayerHasObjectsLeftCheckbox.setText(_("Player has units/structures left"));
    loseFlagsPlayerHasObjectsLeftCheckbox.setTextColor(color);
    loseFlagsPlayerHasObjectsLeftCheckbox.setChecked(mapInfo.loseFlags & WINLOSEFLAGS_HUMAN_HAS_BUILDINGS);
    loseFlags2HBox.addWidget(&loseFlagsPlayerHasObjectsLeftCheckbox, 284);

    loseFlags2HBox.addWidget(Spacer::create());

    loseFlagsAIPlayerNoObjectsLeftCheckbox.setText(_("Others have no units/structures left"));
    loseFlagsAIPlayerNoObjectsLeftCheckbox.setTextColor(color);
    loseFlagsAIPlayerNoObjectsLeftCheckbox.setChecked(mapInfo.loseFlags & WINLOSEFLAGS_AI_NO_BUILDINGS);
    loseFlags2HBox.addWidget(&loseFlagsAIPlayerNoObjectsLeftCheckbox, 284);

    centralVBox.addWidget(VSpacer::create(15));

    centralVBox.addWidget(&techLevelHBox);
    techLevelLabel.setText(_("Tech Level") + ":");
    techLevelLabel.setTextColor(color);
    techLevelHBox.addWidget(&techLevelLabel, 95);

    techLevelDropDownBox.setColor(color);
    techLevelDropDownBox.setNumVisibleEntries(8);
    techLevelDropDownBox.addEntry(_("Level 1:  Concrete, Windtrap, Refinery"),1);
    techLevelDropDownBox.addEntry(_("Level 2:  Radar, Barracks/Light Factory/WOR, Silo, Soldier/Trooper, Trike"),2);
    techLevelDropDownBox.addEntry(_("Level 3:  Light Factory, Quad"),3);
    techLevelDropDownBox.addEntry(_("Level 4:  2x2 Concrete, Wall, Heavy Factory, Tank, Harvester, MCV"),4);
    techLevelDropDownBox.addEntry(_("Level 5:  Gun Turret, Scoutpost, Hightech Factory, Repair Yard, Launcher, Carryall"),5);
    techLevelDropDownBox.addEntry(_("Level 6:  Starport, Rocket Turret, Siege Tank, Advanced Windtrap"),6);
    techLevelDropDownBox.addEntry(_("Level 7:  House IX, Sonic Tank/Deviator/Devastator, Ornithopter"),7);
    techLevelDropDownBox.addEntry(_("Level 8:  Palace, Fremen, Saboteur"),8);
    if(ModManager::instance().isTornieContentActive()) {
        techLevelDropDownBox.addEntry(_("Level 9:  Tech Center, Love Factory, Chaos Factory"),9);
    } else {
        techLevelDropDownBox.addEntry(_("Level 9:  Tech Center"),9);
    }
    techLevelDropDownBox.setSelectedItem(mapInfo.techLevel > 0 ? mapInfo.techLevel-1 : 7);
    techLevelDropDownBox.setVisible( (pMapEditor->getMapVersion() >= 2) );
    techLevelHBox.addWidget(&techLevelDropDownBox);

    centralVBox.addWidget(VSpacer::create(15));

    centralVBox.addWidget(&mapTypeHBox);
    mapTypeLabel.setText(_("Map Type") + ":");
    mapTypeLabel.setTextColor(color);
    mapTypeHBox.addWidget(&mapTypeLabel, 95);

    mapTypeDropDownBox.setColor(color);
    mapTypeDropDownBox.setNumVisibleEntries(3);

    availableMapTypes.emplace_back(MapMetadata::ModVanilla);
    availableMapTypes.emplace_back(MapMetadata::ModTornie);
    availableMapTypes.emplace_back(MapMetadata::ModDuneCity);

    const std::string currentMapType = pMapEditor->getMapType();
    for(size_t i = 0; i < availableMapTypes.size(); ++i) {
        mapTypeDropDownBox.addEntry(MapMetadata::modLabel(availableMapTypes[i]), i);
        if(availableMapTypes[i] == currentMapType) {
            mapTypeDropDownBox.setSelectedItem(i);
        }
    }

    mapTypeDropDownBox.setOnSelectionChange(std::bind(&MapSettingsWindow::onMapTypeChange, this, std::placeholders::_1));
    mapTypeHBox.addWidget(&mapTypeDropDownBox);
    mapTypeHBox.addWidget(HSpacer::create(8));
    convertButton.setText(_("Convert"));
    convertButton.setTextColor(color);
    convertButton.setOnClick([this](){ onConvert(); });
    mapTypeHBox.addWidget(&convertButton,100);

    mapTypeNoteLabel.setTextColor(color);
    mapTypeNoteLabel.setTextFontSize(12);
    centralVBox.addWidget(&mapTypeNoteLabel, 48);
    updateMapTypeNote();

    centralVBox.addWidget(VSpacer::create(15));

    authorLabel.setText(_("Author:"));
    authorLabel.setTextColor(color);
    authorHBox.addWidget(&authorLabel, 95);
    authorTextBox.setText(mapInfo.author);
    authorTextBox.setTextColor(color);
    authorHBox.addWidget(&authorTextBox);
    authorHBox.addWidget(HSpacer::create(140));
    centralVBox.addWidget(&authorHBox);

    centralVBox.addWidget(VSpacer::create(10));

    licenseLabel.setText(_("License:"));
    licenseLabel.setTextColor(color);
    licenseHBox.addWidget(&licenseLabel, 95);
    licenseTextBox.setText(mapInfo.license);
    licenseTextBox.setTextColor(color);
    licenseHBox.addWidget(&licenseTextBox);
    licenseHBox.addWidget(HSpacer::create(140));
    centralVBox.addWidget(&licenseHBox);

    centralVBox.addWidget(Spacer::create(), 100.0);

    mainVBox.addWidget(VSpacer::create(5));

    mainVBox.addWidget(&buttonHBox);

    cancelButton.setText(_("Cancel"));
    cancelButton.setTextColor(color);
    cancelButton.setOnClick(std::bind(&MapSettingsWindow::onCancel, this));

    buttonHBox.addWidget(&cancelButton);

    buttonHBox.addWidget(HSpacer::create(8));

    buttonHBox.addWidget(Spacer::create());

    buttonHBox.addWidget(HSpacer::create(8));

    okButton.setText(_("OK"));
    okButton.setTextColor(color);
    okButton.setOnClick(std::bind(&MapSettingsWindow::onOK, this));

    buttonHBox.addWidget(&okButton);

    mainVBox.addWidget(VSpacer::create(10));

}

void MapSettingsWindow::onCancel() {
    // Leaving this window never touches the map, not even a chosen map type.
    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}


std::string MapSettingsWindow::getSelectedMapType() const {
    const int index = mapTypeDropDownBox.getSelectedIndex();

    if((index < 0) || (index >= (int) availableMapTypes.size())) {
        return pMapEditor->getMapType();
    }

    return availableMapTypes[index];
}


void MapSettingsWindow::onMapTypeChange(bool bInteractive) {
    updateMapTypeNote();
}


void MapSettingsWindow::updateMapTypeNote() {
    const auto plan=pMapEditor->planMapTypeConversion(getSelectedMapType());
    std::string note="Category: "+MapMetadata::modLabel(pMapEditor->getMapType())+". Convert changes the editing palette.\n";
    note+="Removes "+std::to_string(plan.removedStructureIDs.size())+" buildings, "+std::to_string(plan.removedUnitIDs.size())+" units. Saved category: "+MapMetadata::modLabel(plan.resultingType)+".";
    if(!plan.reachesTarget()) note+="\nCategory follows buildings or a sparse city map's name.";
    mapTypeNoteLabel.setText(note);
}


void MapSettingsWindow::onChildWindowClose(Window* pChildWindow) {
    QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);

    if(pQstBox == nullptr) {
        return;
    }

    if(pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
        applyChanges(true);
    }

    // Declining keeps the map and this window exactly as they were.
}


void MapSettingsWindow::onOK() { applyChanges(); }

void MapSettingsWindow::onConvert() {
    const auto plan=pMapEditor->planMapTypeConversion(getSelectedMapType());
    if(!plan.isLossless() || !plan.reachesTarget()) {
        std::string question="Convert to "+MapMetadata::modLabel(plan.target)+"?\n";
        question+="Remove: "+std::to_string(plan.removedStructureIDs.size())+" buildings, "+std::to_string(plan.removedUnitIDs.size())+" units.\n";
        question+=std::to_string(plan.removedReinforcementIndices.size())+" reinforcements, "+std::to_string(plan.removedChoamItemIDs.size())+" starport entries.\n";
        question+="Terrain, spice and compatible objects stay.";
        if(!plan.reachesTarget()) {
            question+="\nAfter saving: "+MapMetadata::modLabel(plan.resultingType)+".";
            question+=plan.resultingType==MapMetadata::ModDuneCity
                ? "\nIts city name keeps this starter map in DuneCity.\nUse Save As with a non-city name to change that category."
                : "\nAdd buildings from the selected type to change its category.";
        }
        auto* box=QstBox::create(question,_("Convert"),_("Cancel"),QSTBOX_BUTTON2);
        box->setTextColor(color); openWindow(box); return;
    }
    applyChanges(true);
}


void MapSettingsWindow::applyChanges(bool convert) {

    MapInfo mapInfo = pMapEditor->getMapInfo();

    mapInfo.winPicture = availableWinPictures[winPictureDropDownBox.getSelectedIndex()];
    mapInfo.losePicture = availableLosePictures[losePictureDropDownBox.getSelectedIndex()];
    mapInfo.briefPicture = availableBriefingPictures[briefingPictureDropDownBox.getSelectedIndex()];

    mapInfo.winFlags = (winFlagsTimeoutCheckbox.isChecked() << 3) | (winFlagsSpiceQuotaCheckbox.isChecked() << 2) | (winFlagsPlayerNoObjectsLeftCheckbox.isChecked() << 1) | winFlagsAIPlayerNoObjectsLeftCheckbox.isChecked();
    mapInfo.loseFlags = (loseFlagsTimeoutCheckbox.isChecked() << 3) | (loseFlagsSpiceQuotaCheckbox.isChecked() << 2) | (loseFlagsPlayerHasObjectsLeftCheckbox.isChecked() << 1) | loseFlagsAIPlayerNoObjectsLeftCheckbox.isChecked();

    mapInfo.timeout = winFlagsTimeoutTextBox.getValue();

    mapInfo.techLevel = techLevelDropDownBox.getSelectedEntryIntData();
    mapInfo.author = authorTextBox.getText();
    mapInfo.license = licenseTextBox.getText();

    const std::string targetType = getSelectedMapType();

    if(convert) {
        // The conversion opens the undo step; the settings below join it, so a
        // single undo restores both the settings and every discarded object.
        try { pMapEditor->applyMapTypeConversion(pMapEditor->planMapTypeConversion(targetType)); }
        catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); return; }
    } else {
        pMapEditor->startOperation();
    }

    MapEditorChangeMapInfo changeMapInfoOperation(mapInfo);

    pMapEditor->addUndoOperation(changeMapInfoOperation.perform(pMapEditor));

    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}
