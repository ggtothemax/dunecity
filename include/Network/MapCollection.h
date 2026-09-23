#ifndef DUNECITY_MAP_COLLECTION_H
#define DUNECITY_MAP_COLLECTION_H
#include <DataTypes.h>
#include <mod/Workshop.h>
#include <functional>
#include <string>

// Community map collection. Starting a new offline custom match contributes that
// scenario, on the mod it was actually started with, to the metaserver catalogue.
// Collection only records which already-pinned revision to share, so it reuses the
// bounded Workshop outbox: an unreachable metaserver can never delay or fail an
// offline launch, and a later start queues the same revision again.
namespace MapCollection {

// Which started matches contribute. Previews and cancelled setups never reach a
// game start at all. Replays and loaded saves replay content that was chosen
// earlier, campaign and skirmish scenarios ship with the game, and networked
// games are pinned and shared by their host instead of by every participant.
inline bool eligibleMatch(GameType type, bool replay, bool save) {
    return !replay && !save && type == GameType::CustomGame && !isNetworkGameType(type);
}

struct Decision {
    bool collect = false;
    std::string mapHash;   ///< The scenario revision to share.
    std::string modHash;   ///< Its exact selected-mod dependency, shared with it.
    std::string reason;    ///< Why nothing is collected; for diagnostics only.
};

// Decides what a launch contributes. The revision is the one the launch already
// pinned, so identity is the stored content identity: the same scenario bytes on
// the same selected mod are one revision and a repeated start collects nothing,
// while edited rules or a different selected mod are a different revision and are
// collected separately instead of overwriting the first.
inline Decision plan(const Workshop::Store& store, GameType type, bool replay, bool save,
                     const std::string& mapRevisionHash) {
    Decision decision;
    if(!eligibleMatch(type, replay, save)) { decision.reason = "not a new offline custom match"; return decision; }
    if(mapRevisionHash.empty()) { decision.reason = "the launch pinned no map revision"; return decision; }
    Workshop::Revision map;
    try { map = store.get(mapRevisionHash); }
    catch(const std::exception& error) { decision.reason = error.what(); return decision; }
    // A scenario revision is exactly the original map file. Anything else - a mod
    // package, or a multi-file revision - is never offered as a community map.
    if(map.kind != "map" || map.files.size() != 1) { decision.reason = "the pinned revision is not a scenario"; return decision; }
    if(map.modHash.empty()) { decision.reason = "the scenario records no mod"; return decision; }
    // Already offered: either the metaserver assigned this revision a version, or it
    // answered that it already stores this scenario under another player's identity.
    if(store.shared(map.hash) || store.collected(map.hash)) {
        decision.reason = "the metaserver already has this map"; return decision;
    }
    decision.collect = true;
    decision.mapHash = map.hash;
    decision.modHash = map.modHash;
    return decision;
}

using Queue = std::function<void(const Workshop::Revision&)>;

// Queues at most one scenario per start, keyed by its content hash, so repeated
// starts of the same map never flood the outbox. Sharing that revision also carries
// its recorded mod revision as the dependency, and asks the metaserver for an
// existing copy of the same content before uploading anything. Never throws: any
// failure leaves the started match untouched and is retried on a later start.
inline bool collect(Workshop::Store& store, GameType type, bool replay, bool save,
                    const std::string& mapRevisionHash, const Queue& queue) {
    try {
        const auto decision = plan(store, type, replay, save, mapRevisionHash);
        if(!decision.collect) return false;
        queue(store.get(decision.mapHash));
        return true;
    } catch(const std::exception&) { return false; }
      catch(...) { return false; }
}
}
#endif
