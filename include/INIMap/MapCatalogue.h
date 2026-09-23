#ifndef DUNECITY_MAP_CATALOGUE_H
#define DUNECITY_MAP_CATALOGUE_H
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// "All Maps" concatenates the four local map directories (SP install, SP user,
// MP install, MP user). The same map can legitimately live in more than one of
// them: a metaserver download is installed into the user profile under its own
// display name, the map editor saves an edited bundled map into the user
// profile, and annotating a map with Mod/MapVersion leaves the untouched
// original beside it. All of those copies show the same chooser name, because
// the name is BASIC.Name (or the file stem), so the list showed the map twice.
//
// Deduplication here is deliberately conservative: two entries collapse only
// when they show the same name AND carry the same playable content. Content is
// compared after removing only catalogue metadata, so a copy that only
// gained Name/Mod/MapVersion keys still matches its original, while a genuinely
// different map that happens to share a name - or a revision whose terrain,
// structures or units differ - keeps its own row.
namespace MapCatalogue {

/// Fold a map INI down to its playable content, retaining BASIC game rules,
/// without comments, blank lines or surrounding whitespace. Returns "" when the
/// text holds no content at all, which disables merging for that entry.
inline std::string contentKey(const std::string& iniText) {
    std::uint64_t hash = 1469598103934665603ull; // FNV-1a offset basis
    std::size_t hashedLines = 0, hashedBytes = 0;
    bool inBasic = false, inWorkshop = false;
    std::size_t start = 0;
    while(start <= iniText.size()) {
        std::size_t end = iniText.find('\n', start);
        if(end == std::string::npos) end = iniText.size();
        std::string line = iniText.substr(start, end - start);
        start = end + 1;
        const auto first = line.find_first_not_of(" \t\r");
        if(first == std::string::npos) continue;
        const auto last = line.find_last_not_of(" \t\r");
        line = line.substr(first, last - first + 1);
        if(line.empty() || line[0] == ';' || line[0] == '#') continue;
        if(line.front() == '[') {
            std::string section = line.substr(1, line.find(']') == std::string::npos ? std::string::npos
                                                                                     : line.find(']') - 1);
            std::transform(section.begin(), section.end(), section.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            inBasic = (section == "basic");
            inWorkshop = (section == "workshop");
            if(inBasic || inWorkshop) continue;
        }
        if(inWorkshop) continue;
        if(inBasic) {
            const auto equals=line.find('=');
            std::string key=line.substr(0,equals);
            const auto lastKey=key.find_last_not_of(" \t");
            if(lastKey!=std::string::npos)key.resize(lastKey+1);
            std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return std::tolower(c);});
            // Mod tags are inferred from buildings; the chooser separately
            // includes the effective category/dependency in its grouping key.
            if(key=="name" || key=="author" || key=="license" || key=="mod" || key=="mapversion")continue;
            line="[BASIC]"+line; // Do not conflate BASIC rules with another section.
        }
        for(unsigned char c : line) { hash ^= c; hash *= 1099511628211ull; }
        hash ^= '\n'; hash *= 1099511628211ull;
        ++hashedLines;
        hashedBytes += line.size();
    }
    if(hashedLines == 0) return "";
    std::string key;
    for(int shift = 60; shift >= 0; shift -= 4) key += "0123456789abcdef"[(hash >> shift) & 0xF];
    return key + ":" + std::to_string(hashedBytes);
}

/// One catalogue row as the chooser sees it, reduced to what identifies a copy.
struct Copy {
    std::string displayName;      ///< the text shown in the map list
    std::string contentKey;       ///< contentKey() of the file; "" never merges
    int version = 0;              ///< BASIC.MapVersion or the workshop revision
    std::string dependency;       ///< effective category/mod dependency; variants stay distinct
};

/// A surviving row: the copy to show and the newest revision number seen for it.
struct Kept { std::size_t index; int version; };

/// The copies to show, in input order. Equivalent copies collapse onto the one
/// the catalogue scanned first; scan order is fixed (SP install, SP user, MP
/// install, MP user), so the surviving path is the stable, predictable one -
/// the installed map rather than a download or editor save of it in the user
/// profile - and callers that hand the chooser a map list still find the map
/// they selected. The row still reports the highest revision number of the
/// group, because BASIC.MapVersion and a workshop revision count the same map
/// on different scales and neither is a reason to hide a path.
inline std::vector<Kept> keptCopies(const std::vector<Copy>& copies) {
    auto foldedName = [](std::string value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if(first == std::string::npos) return std::string();
        value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return value;
    };
    std::unordered_map<std::string, std::size_t> firstOfGroup;
    std::vector<bool> keep(copies.size(), true);
    std::vector<int> version(copies.size(), 0);
    for(std::size_t i = 0; i < copies.size(); ++i) {
        version[i] = copies[i].version;
        if(copies[i].contentKey.empty()) continue;
        const std::string group = foldedName(copies[i].displayName) + '\0' + copies[i].dependency + '\0' + copies[i].contentKey;
        const auto existing = firstOfGroup.find(group);
        if(existing == firstOfGroup.end()) { firstOfGroup.emplace(group, i); continue; }
        keep[i] = false;
        version[existing->second] = std::max(version[existing->second], copies[i].version);
    }
    std::vector<Kept> kept;
    for(std::size_t i = 0; i < copies.size(); ++i) if(keep[i]) kept.push_back({i, version[i]});
    return kept;
}

} // namespace MapCatalogue
#endif // DUNECITY_MAP_CATALOGUE_H
