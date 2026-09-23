#ifndef DUNECITY_MAP_METADATA_H
#define DUNECITY_MAP_METADATA_H
#include <FileClasses/INIFile.h>
#include <algorithm>
#include <cctype>
#include <string>

// BASIC.Version is the map FORMAT. MapVersion/Workshop.Version describe a revision.
struct MapMetadata {
    std::string name, mod;
    int width=0, height=0, players=0, version=0;
    static std::string canonicalMod(std::string value) {
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return std::tolower(c);});
        if(value=="?") return "";
        if(value=="dune city") return "dunecity";
        if(value=="dune legacy") return "vanilla";
        return value;
    }
    static MapMetadata read(const INIFile& ini, const std::string& fallbackName) {
        MapMetadata m;
        m.name=ini.getStringValue("BASIC","Name",fallbackName);
        m.mod=canonicalMod(ini.getStringValue("BASIC","Mod",""));
        m.version=std::max(0,ini.getIntValue("BASIC","MapVersion",0));
        if(ini.hasKey("MAP","Seed")) {
            const int scale=ini.getIntValue("BASIC","MapScale",-1);
            m.width=m.height=scale==0?62:scale==1?32:scale==2?21:64;
        } else {
            m.width=ini.getIntValue("MAP","SizeX",0);m.height=ini.getIntValue("MAP","SizeY",0);
        }
        for(const char* name:{"Harkonnen","Atreides","Ordos","Fremen","Sardaukar","Mercenary","Rebels","Custom","Wildspade","Kleshmersh","Tharpique"})
            if(ini.hasSection(name))++m.players;
        for(int i=1;i<=12;++i)if(ini.hasSection("Player"+std::to_string(i)))++m.players;
        return m;
    }
    bool matches(const std::string& filterMod, int sizeBand, int maxPlayers) const {
        const int extent=std::max(width,height);
        const int band=extent<=64?1:extent<=128?2:extent<=192?3:extent<=256?4:5;
        return (filterMod.empty() || mod==filterMod || (filterMod=="unknown"&&mod.empty()))
            && (!sizeBand || band==sizeBand) && (!maxPlayers || players==maxPlayers);
    }
};
#endif
