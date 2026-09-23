#ifndef CITYSTATTEXT_H
#define CITYSTATTEXT_H

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// The sidebar object interface is a fixed, narrow column (SIDEBARWIDTH - 25).
// City-sim stat lines such as " Pollution: Very Heavy (emits)" are wider than
// that column, so they used to run past the sidebar's right edge - and, in the
// repair yard, push the repair-unit icon off-screen with them, because the
// text column claimed its full minimum width inside the row.
//
// These helpers shorten a stat line until it fits the column it is drawn in.
// The measuring function is supplied by the caller (the real GUI style at
// runtime, a deterministic stub in tests), so this header stays free of SDL and
// engine dependencies.
namespace CityStatText {

/// Well-known long words, longest first so prefixes never shadow a longer key.
inline const std::vector<std::pair<const char*, const char*>>& abbreviations() {
    static const std::vector<std::pair<const char*, const char*>> table = {
        {"Local pollution", "Local poll."},
        {"Middle Class",    "Mid. Class"},
        {"Lower Class",     "Low. Class"},
        {"Very Heavy",      "V.Heavy"},
        {"Residential",     "Res."},
        {"Commercial",      "Com."},
        {"Industrial",      "Ind."},
        {"Dangerous",       "Danger."},
        {"Pollution",       "Poll."},
        {"Moderate",        "Mod."},
        {"(emits)",         "(e)"},
    };
    return table;
}

/// Replace every known long word by its short form.
inline std::string abbreviate(std::string text) {
    for(const auto& entry : abbreviations()) {
        const std::string from = entry.first, to = entry.second;
        for(std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
    }
    return text;
}

/// Drop a trailing "(...)" qualifier, keeping the value itself.
inline std::string dropParenthetical(const std::string& text) {
    const auto open = text.rfind('(');
    if(open == std::string::npos || text.find(')', open) == std::string::npos) return text;
    std::string shortened = text.substr(0, open);
    const auto last = shortened.find_last_not_of(" \t");
    return last == std::string::npos ? shortened : shortened.substr(0, last + 1);
}

/// Cut trailing bytes without splitting a UTF-8 sequence.
inline std::string truncateBytes(const std::string& text, std::size_t length) {
    if(length >= text.size()) return text;
    while(length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
    return text.substr(0, length);
}

/**
    Returns the longest readable form of \a text that measures no wider than
    \a availableWidth: the text itself, then an abbreviated form, then that form
    without its trailing qualifier, and finally an ellipsis-truncated form.
    A non-positive width (unknown column) returns the text unchanged.
    \param  measure  callable returning the drawn width of a candidate
*/
template<typename Measure>
std::string fit(const std::string& text, int availableWidth, Measure measure) {
    if(availableWidth <= 0 || text.empty() || measure(text) <= availableWidth) return text;
    const std::string shortened = abbreviate(text);
    if(measure(shortened) <= availableWidth) return shortened;
    const std::string bare = dropParenthetical(shortened);
    if(measure(bare) <= availableWidth) return bare;
    // Nothing fits: keep as many leading characters as the column allows.
    static const std::string ellipsis = "\xE2\x80\xA6";
    for(std::size_t length = bare.size(); length > 0; --length) {
        const std::string candidate = truncateBytes(bare, length - 1) + ellipsis;
        if(measure(candidate) <= availableWidth) return candidate;
    }
    return ellipsis;
}

} // namespace CityStatText

#endif // CITYSTATTEXT_H
