#include <mod/Workshop.h>
#include <INIMap/MapMetadata.h>
#include <stdexcept>
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
        title.setText(revision ? "Save to metaserver" : "Load from metaserver");
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

class MetaserverMenu final : public MenuBase {
    StaticContainer layout;
    Label title, status, details;
    DropDownBox mod, size, players;
    ListBox list;
    TextButton action, refresh, back;
    Client client;
    std::string kind, loadedPath;
    std::vector<Revision> entries;
    std::vector<size_t> visible;
    std::vector<std::string> mods{""};
    bool waiting=false, online;
    void selected() {
        const int i=list.getSelectedIndex();
        const bool valid=i>=0 && i<static_cast<int>(visible.size());
        action.setEnabled(valid && !waiting);
        if(!valid) { details.setText(""); return; }
        const auto& r=entries[visible[i]];
        details.setText("Version: "+std::to_string(r.version)+(kind=="map"
            ? "  |  Mod: "+r.mapMod+"\n"+std::to_string(r.mapWidth)+" x "+std::to_string(r.mapHeight)+"  |  Max players: "+std::to_string(r.mapPlayers)
            : "  |  Base: "+r.base));
    }
    void filter() {
        list.clearAllEntries(); visible.clear();
        for(size_t i=0;i<entries.size();++i) {
            const auto& r=entries[i];
            MapMetadata m; m.mod=r.mapMod; m.width=r.mapWidth; m.height=r.mapHeight; m.players=r.mapPlayers;
            const int mi=mod.getSelectedIndex();
            if(kind=="map" && !m.matches(mi>=0 ? mods[mi] : "",size.getSelectedIndex(),players.getSelectedIndex())) continue;
            visible.push_back(i); list.addEntry(r.name);
        }
        if(!visible.empty()) list.setSelectedItem(0);
        selected();
    }
    void load() {
        client.cancel(); entries.clear(); waiting=online;
        if(online) {
            if(kind=="map") client.browseMaps(); else client.browse("mod");
            status.setText("Loading from metaserver...");
        } else status.setText("Metaserver is unavailable in this offline preview.");
        filter();
    }
    void perform() {
        const int i=list.getSelectedIndex(); if(i<0 || i>=static_cast<int>(visible.size())) return;
        try {
            auto r=entries[visible[i]];
            if(!downloadWithProgress(r.hash)) { status.setText("Loading was not completed. You can retry."); return; }
            r=store().get(r.hash);
            if(kind=="mod") { installMod(r); quit(); }
            else {
                const auto path=installMap(r);
                if(!r.modHash.empty()) installMod(store().get(r.modHash));
                loadedPath=path; quit();
            }
        } catch(const std::exception& e) { status.setText(e.what()); }
    }
public:
    explicit MetaserverMenu(const std::string& type, bool network=true) : kind(type),online(network) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground))); setWindowWidget(&layout);
        const int w=std::min(850,getSize().x-32), h=std::min(640,getSize().y-24);
        const int x=(getSize().x-w)/2,y=(getSize().y-h)/2;
        title.setText(kind=="map" ? "Load map from metaserver" : "Load mod from metaserver");
        title.setTextFontSize(22); title.setAlignment(Alignment_HCenter);
        layout.addWidget(&title,Point(x,y),Point(w,40));
        mod.addEntry("Any mod"); mod.setSelectedItem(0);
        for(const char* s:{"Any size","Up to 64","65 to 128","129 to 192","193 to 256","Over 256"}) size.addEntry(s);
        size.setSelectedItem(0); players.addEntry("Any players");
        for(int n=1;n<=12;++n) players.addEntry(std::to_string(n)+" players");
        players.setSelectedItem(0);
        for(auto* box:{&mod,&size,&players}) { box->setVisible(kind=="map"); box->setOnSelectionChange([this](bool){filter();}); }
        layout.addWidget(&mod,Point(x,y+46),Point(w/3-8,26));
        layout.addWidget(&size,Point(x+w/3,y+46),Point(w/3-8,26));
        layout.addWidget(&players,Point(x+2*w/3,y+46),Point(w-2*w/3,26));
        const int top=kind=="map"?82:46;
        list.setOnSelectionChange([this](bool){selected();});
        layout.addWidget(&list,Point(x,y+top),Point(w,h-top-142));
        details.setTextFontSize(12); status.setTextFontSize(12);
        layout.addWidget(&details,Point(x,y+h-135),Point(w,48));
        layout.addWidget(&status,Point(x,y+h-85),Point(w,40));
        action.setText("Load"); action.setOnClick([this](){perform();});
        refresh.setText("Refresh"); refresh.setOnClick([this](){load();});
        back.setText("Back"); back.setOnClick([this](){quit();});
        layout.addWidget(&action,Point(x,y+h-36),Point(w/3-8,32));
        layout.addWidget(&refresh,Point(x+w/3,y+h-36),Point(w/3-8,32));
        layout.addWidget(&back,Point(x+2*w/3,y+h-36),Point(w-2*w/3,32));
        load(); back.setActive();
    }
    void update() override {
        if(!waiting) return;
        client.update(); if(client.status()==Client::Status::Busy) return;
        if(client.status()!=Client::Status::Succeeded) { waiting=false; status.setText(client.message()); selected(); return; }
        for(const auto& r:client.items()) {
            auto previous=std::find_if(entries.begin(),entries.end(),[&](const Revision& old){return old.id==r.id;});
            if(previous==entries.end()) entries.push_back(r);
            else if(r.version>previous->version) *previous=r;
            if(kind=="map" && !r.mapMod.empty() && std::find(mods.begin(),mods.end(),r.mapMod)==mods.end()) {
                mods.push_back(r.mapMod); mod.addEntry(r.mapMod);
            }
        }
        const auto next=client.nextPage();
        if(next && entries.size()<10000) { if(kind=="map") client.browseMaps(next); else client.browse("mod",next); }
        else { waiting=false; status.setText(entries.empty()?"No maps or mods available.":"Choose an item to load."); }
        filter();
    }
    std::string runMap() { showMenu(); return loadedPath; }
};
}

bool publishWithProgress(const Revision& revision,bool promoted) { return ProgressMenu(&revision,"",promoted).run(); }
bool downloadWithProgress(const std::string& hash) { return ProgressMenu(nullptr,hash,false).run(); }
void shareRevision(const Revision& revision) { publishWithProgress(revision,true); }
std::unique_ptr<MenuBase> createMetaserverMenu(const std::string& kind, bool online) { return std::make_unique<MetaserverMenu>(kind,online); }
void openMetaserverMods() { createMetaserverMenu("mod",true)->showMenu(); }
std::string loadMapFromMetaserver() { return MetaserverMenu("map").runMap(); }
}
