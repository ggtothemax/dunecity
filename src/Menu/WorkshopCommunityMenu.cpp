#include <mod/Workshop.h>
#include <mod/WorkshopClient.h>
#include <mod/ModManager.h>
#include <Menu/MenuBase.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <GUI/StaticContainer.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>
#include <GUI/ListBox.h>
#include <globals.h>
#include <misc/fnkdat.h>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace Workshop {
namespace {
class ProgressMenu final : public MenuBase {
    StaticContainer layout;
    Label title, detail;
    TextButton close;
    Client client;
    bool finished = false, success = false;
public:
    ProgressMenu(const Revision* revision, const std::string& hash, bool promoted) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
        setWindowWidget(&layout);
        const int width = std::min(620, getSize().x-32);
        const int x = (getSize().x-width)/2, y = (getSize().y-180)/2;
        title.setText(revision ? "Share saved version" : "Download version");
        title.setAlignment(Alignment_HCenter); title.setTextFontSize(22);
        detail.setTextFontSize(13); detail.setAlignment(Alignment_HCenter);
        layout.addWidget(&title, Point(x,y), Point(width,40));
        layout.addWidget(&detail, Point(x,y+50), Point(width,80));
        close.setText("Cancel");
        close.setOnClick([this]() { client.cancel(); quit(); });
        layout.addWidget(&close, Point(x+width/2-80,y+140), Point(160,32));
        close.setActive();
        if(revision) client.publish(*revision,promoted);
        else client.download(hash);
        detail.setText(client.message());
    }
    void update() override {
        if(finished) return;
        client.update(); detail.setText(client.message());
        if(client.status() == Client::Status::Succeeded) { success = true; finished = true; quit(); }
        else if(client.status() == Client::Status::Failed) { finished = true; close.setText("Back"); }
    }
    bool run() { showMenu(); return success; }
};

class CommunityMenu final : public MenuBase {
    StaticContainer layout;
    Label title, help, sourceLabel, filterLabel, modLabel, pinnedModLabel, status, details;
    DropDownBox source, kind, mapMod;
    ListBox list;
    TextButton action, refresh, more, back;
    Client client;
    std::vector<Revision> entries;
    std::vector<std::string> paths, modNames;
    bool waiting = false, allowNetwork;
    int panelX = 0, panelY = 0, panelWidth = 0, panelHeight = 0;

    int sourceIndex() const { return source.getSelectedIndex(); }
    std::string filter() const { return kind.getSelectedIndex()==1 ? "map" : kind.getSelectedIndex()==2 ? "mod" : ""; }

    // A saved map's dependency belongs to its content revision. A loose legacy
    // map needs an explicit choice, never the hidden last active game mod.
    Revision pinnedMap(int index) const {
        if(sourceIndex()!=3 || index<0 || index>=static_cast<int>(paths.size())) return {};
        const auto& path = paths[index];
        try {
            if(!std::filesystem::is_regular_file(path+".workshop.ini") || std::filesystem::file_size(path)>1024*1024) return {};
            const INIFile metadata(path+".workshop.ini");
            auto revision = store().get(metadata.getStringValue("Workshop", "Hash"));
            std::ifstream input(path,std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
            if(!input.bad() && revision.kind=="map" && !revision.files.empty()
               && hashBytes(bytes)==revision.files[0].hash && !revision.modHash.empty()) return revision;
        } catch(const std::exception&) { }
        return {};
    }

    void selected() {
        const int index = list.getSelectedIndex();
        const bool valid = index>=0 && index<static_cast<int>(entries.size());
        action.setEnabled(valid && !waiting);
        mapMod.setEnabled(sourceIndex()==3);
        mapMod.setVisible(sourceIndex()==3); pinnedModLabel.setVisible(false);
        if(!valid) { details.setText(""); return; }
        const auto& revision = entries[index];
        if(sourceIndex()==3) {
            const auto pinned = pinnedMap(index);
            if(!pinned.hash.empty()) {
                mapMod.setEnabled(false);
                std::string modName = "its saved mod version";
                try { const auto mod = store().get(pinned.modHash); modName = mod.name+" v"+std::to_string(mod.version); } catch(const std::exception&) { }
                mapMod.setVisible(false); pinnedModLabel.setVisible(true);
                pinnedModLabel.setText(modName + " (saved dependency)");
                details.setText("Saved map v"+std::to_string(pinned.version)+" requires "+modName+".\nSharing keeps this exact map and mod version.");
            } else {
                action.setEnabled(mapMod.getSelectedIndex()>0);
                details.setText("Choose the mod this map was made for before sharing.\nThe map and that exact mod version will be saved together.");
            }
        } else {
            details.setText(revision.hash.empty()
                ? "Sharing first saves a numbered version of all rules and assets."
                : "Version "+std::to_string(revision.version)+"  |  Verified content: "+revision.hash.substr(0,12)
                    +(revision.modHash.empty() ? "" : "\nThe required mod version is included automatically."));
        }
    }
    void showEntries() {
        list.clearAllEntries();
        for(const auto& revision : entries)
            list.addEntry((revision.kind=="map" ? "Map: " : "Mod: ")+revision.name
                +(revision.version ? "  v"+std::to_string(revision.version) : ""));
        if(!entries.empty()) list.setSelectedItem(0);
        selected();
    }
    void updateSourceLayout() {
        const bool maps = sourceIndex()==3;
        kind.setEnabled(sourceIndex()<2);
        modLabel.setVisible(maps); mapMod.setVisible(maps); mapMod.setEnabled(maps); pinnedModLabel.setVisible(false);
        const int top = maps ? 155 : 119;
        layout.setWidgetGeometry(&list,Point(panelX,panelY+top),Point(panelWidth,panelHeight-top-146));
    }
    void load(unsigned cursor = 0) {
        client.cancel(); waiting = false; entries.clear(); paths.clear(); more.setEnabled(false);
        updateSourceLayout();
        action.setText(sourceIndex()==0 ? "Download" : sourceIndex()<2 ? "Share Version" : "Save & Share");
        try {
            if(sourceIndex()==0) {
                if(allowNetwork) { client.browse(filter(),cursor); waiting = true; status.setText("Loading community content..."); }
                else status.setText("Community browsing is unavailable in this offline preview.");
            } else if(sourceIndex()==1) {
                entries = store().list(filter());
                status.setText(entries.empty() ? "No saved versions yet. Save a map or mod in Workshop to add one."
                    : "Choose an exact saved version to share. Your saved versions also work offline.");
            } else if(sourceIndex()==2) {
                for(const auto& mod : ModManager::instance().listMods()) {
                    Revision revision; revision.kind = "mod"; revision.name = mod.displayName;
                    entries.push_back(revision); paths.push_back(mod.name);
                }
                status.setText("Share an installed mod, including its rules, campaigns and assets.");
            } else {
                for(const char* directory : {"maps/singleplayer", "maps/multiplayer"}) {
                    char path[FILENAME_MAX];
                    if(fnkdat(directory,path,sizeof(path),FNKDAT_USER|FNKDAT_CREAT)<0) continue;
                    for(const auto& entry : std::filesystem::directory_iterator(path)) {
                        const auto extension = entry.path().extension().string();
                        if(entry.is_regular_file() && (extension==".ini" || extension==".INI")
                           && entry.path().filename().string().find(".workshop.ini")==std::string::npos) {
                            Revision revision; revision.kind = "map"; revision.name = entry.path().stem().string();
                            entries.push_back(revision); paths.push_back(entry.path().string());
                        }
                    }
                }
                status.setText(entries.empty() ? "No local maps yet. Create one in Map Editor, or download a community map."
                    : "Share a local map. Existing saved versions keep their required mod.");
            }
            showEntries();
        } catch(const std::exception& error) { status.setText(error.what()); showEntries(); }
    }
    void perform() {
        const int index = list.getSelectedIndex();
        if(index<0 || index>=static_cast<int>(entries.size())) return;
        try {
            auto revision = entries[index];
            if(sourceIndex()==0) {
                if(!downloadWithProgress(revision.hash)) { status.setText("Download was not completed. You can retry."); return; }
                revision = store().get(revision.hash);
                if(revision.kind=="mod") installMod(revision);
                else { if(!revision.modHash.empty()) installMod(store().get(revision.modHash)); installMap(revision); }
                status.setText("Downloaded "+revision.name+" v"+std::to_string(revision.version)+". Choose it when starting a game.");
            } else {
                if(sourceIndex()==2) revision = saveMod(paths[index]);
                else if(sourceIndex()==3) {
                    revision = pinnedMap(index);
                    if(revision.hash.empty()) {
                        const int chosen = mapMod.getSelectedIndex()-1;
                        if(chosen<0 || chosen>=static_cast<int>(modNames.size())) { status.setText("Choose the required mod first."); return; }
                        revision = saveMap(paths[index],modNames[chosen]);
                    }
                }
                if(publishWithProgress(revision,true)) status.setText("Shared "+revision.name+". Other players can now find and download it.");
                else status.setText("Sharing was not completed. Your saved version is still available locally.");
            }
        } catch(const std::exception& error) { status.setText(error.what()); }
    }
public:
    explicit CommunityMenu(bool online = true) : allowNetwork(online) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground))); setWindowWidget(&layout);
        panelWidth = std::min(850,getSize().x-32); panelHeight = std::min(640,getSize().y-24);
        panelX = (getSize().x-panelWidth)/2; panelY = (getSize().y-panelHeight)/2;
        const int x = panelX, y = panelY, width = panelWidth, height = panelHeight;
        title.setText("Community Maps & Mods"); title.setTextFontSize(22); title.setAlignment(Alignment_HCenter);
        help.setText("Browse, download and share maps and mods with other players."); help.setTextFontSize(13); help.setAlignment(Alignment_HCenter);
        layout.addWidget(&title,Point(x,y),Point(width,34)); layout.addWidget(&help,Point(x,y+36),Point(width,28));
        sourceLabel.setText("Source"); filterLabel.setText("Show"); sourceLabel.setTextFontSize(12); filterLabel.setTextFontSize(12);
        layout.addWidget(&sourceLabel,Point(x,y+68),Point(width*2/3-8,18));
        layout.addWidget(&filterLabel,Point(x+width*2/3,y+68),Point(width/3,18));
        for(const char* label : {"Community downloads", "My saved versions", "Installed mods to share", "My maps to share"}) source.addEntry(label);
        source.setSelectedItem(online ? 0 : 1);
        source.setOnSelectionChange([this](bool interactive) {
            if(!interactive) return;
            kind.setSelectedItem(sourceIndex()==2 ? 2 : sourceIndex()==3 ? 1 : 0); load();
        });
        kind.addEntry("Maps & mods"); kind.addEntry("Maps"); kind.addEntry("Mods"); kind.setSelectedItem(0);
        kind.setOnSelectionChange([this](bool interactive) { if(interactive) load(); });
        layout.addWidget(&source,Point(x,y+87),Point(width*2/3-8,26));
        layout.addWidget(&kind,Point(x+width*2/3,y+87),Point(width/3,26));
        modLabel.setText("Required mod"); modLabel.setTextFontSize(12);
        layout.addWidget(&modLabel,Point(x,y+121),Point(110,26));
        mapMod.addEntry("Choose required mod...");
        for(const auto& mod : ModManager::instance().listMods()) { mapMod.addEntry(mod.displayName); modNames.push_back(mod.name); }
        mapMod.setSelectedItem(0); mapMod.setOnSelectionChange([this](bool) { selected(); });
        layout.addWidget(&mapMod,Point(x+118,y+121),Point(width-118,26));
        pinnedModLabel.setTextFontSize(13);
        layout.addWidget(&pinnedModLabel,Point(x+118,y+121),Point(width-118,26));
        list.setOnSelectionChange([this](bool) { selected(); });
        layout.addWidget(&list,Point(x,y+119),Point(width,height-265));
        details.setTextFontSize(12); layout.addWidget(&details,Point(x,y+height-139),Point(width,48));
        status.setTextFontSize(12); layout.addWidget(&status,Point(x,y+height-88),Point(width,40));
        action.setOnClick([this]() { perform(); }); refresh.setText("Refresh"); refresh.setOnClick([this]() { load(); });
        more.setText("Next page"); more.setOnClick([this]() { load(client.nextPage()); });
        back.setText("Back"); back.setOnClick([this]() { quit(); });
        const int actionWidth = width*2/5;
        layout.addWidget(&action,Point(x,y+height-36),Point(actionWidth-8,32));
        layout.addWidget(&refresh,Point(x+actionWidth,y+height-36),Point((width-actionWidth)/3-8,32));
        layout.addWidget(&more,Point(x+actionWidth+(width-actionWidth)/3,y+height-36),Point((width-actionWidth)/3-8,32));
        layout.addWidget(&back,Point(x+actionWidth+2*(width-actionWidth)/3,y+height-36),Point((width-actionWidth)/3,32));
        load(); source.setActive();
    }
    void update() override {
        if(!waiting) return;
        client.update(); if(client.status()==Client::Status::Busy) return;
        waiting = false;
        if(client.status()==Client::Status::Succeeded) {
            entries = client.items(); showEntries(); more.setEnabled(client.nextPage()!=0);
            status.setText(entries.empty() ? "No community content matches this filter yet." : "Select a version to download with its required content.");
        } else status.setText(client.message()+"\nYour saved versions remain available offline.");
    }
};
}

bool publishWithProgress(const Revision& revision,bool promoted) { return ProgressMenu(&revision,"",promoted).run(); }
bool downloadWithProgress(const std::string& hash) { return ProgressMenu(nullptr,hash,false).run(); }
void shareRevision(const Revision& revision) { publishWithProgress(revision,true); }
// Kept separate from showMenu so callers can embed or inspect the library without
// starting a nested event loop. Offline construction never starts a request.
std::unique_ptr<MenuBase> createCommunityMenu(bool online) { return std::make_unique<CommunityMenu>(online); }
void openCommunityMenu() { createCommunityMenu(true)->showMenu(); }
}
