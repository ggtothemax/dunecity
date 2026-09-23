// Community map collection: which launches contribute a custom map to the
// metaserver, with which mod, and what a repeated, edited or offline start does.
// These drive the real Workshop store; only the outbox queue is a test sink.
#include <catch2/catch_test_macros.hpp>
#include <Network/MapCollection.h>
#include <fstream>
#include <filesystem>

namespace {
namespace fs = std::filesystem;

// Mirrors how a launch pins content: the mod package is captured from the active
// mod folder, the scenario from the single original map file the player selected.
struct Fixture {
    fs::path root = fs::temp_directory_path()/("map-collection-test-"+Workshop::newID());
    Workshop::Store store{root/"cache"};
    Fixture() { fs::create_directories(root); }
    ~Fixture() { std::error_code error; fs::remove_all(root,error); }

    Workshop::Revision mod(const std::string& name, const std::string& rules) {
        const auto source = root/("mod-"+name);
        write(source/"mod.ini","[Mod]\nDisplay Name = "+name+"\n");
        write(source/"ObjectData.ini",rules);
        return store.capture("mod",Workshop::hashBytes(name).substr(0,32),name,"vanilla","",source);
    }
    // saveMapData() stages exactly one map.ini and derives the item name from the
    // file stem, never from the player's local directory.
    Workshop::Revision map(const std::string& id, const std::string& stem,
                           const std::string& scenario, const std::string& modHash) {
        const auto stage = root/(".map-"+Workshop::newID());
        write(stage/"map.ini",scenario);
        auto revision = store.capture("map",id,stem,"",modHash,stage);
        fs::remove_all(stage);
        return revision;
    }
    void write(const fs::path& path, const std::string& data) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::binary); out << data;
    }
};

struct Outbox {
    std::vector<Workshop::Revision> queued;
    bool offline = false;
    MapCollection::Queue sink() {
        return [this](const Workshop::Revision& revision) {
            if(offline) throw std::runtime_error("Could not queue content sharing.");
            for(const auto& existing : queued) if(existing.hash == revision.hash) return; // outbox is keyed by hash
            queued.push_back(revision);
        };
    }
};

const std::string kScenario = "[BASIC]\nAuthor = Fremen\n[MAP]\nSeed = 42\n";
}

TEST_CASE("A started offline custom match collects its map on the vanilla mod","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto map = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);

    const auto decision = MapCollection::plan(fixture.store,GameType::CustomGame,false,false,map.hash);
    REQUIRE(decision.collect);
    REQUIRE(decision.mapHash == map.hash);
    REQUIRE(decision.modHash == vanilla.hash);

    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 1);
    // A map stays a map with a mod dependency; the mod is not republished as content.
    REQUIRE(outbox.queued.front().kind == "map");
    REQUIRE(outbox.queued.front().modHash == vanilla.hash);
    REQUIRE(fixture.store.get(outbox.queued.front().modHash).kind == "mod");
    // Only the original scenario file travels: no save state and no local path.
    REQUIRE(outbox.queued.front().files.size() == 1);
    REQUIRE(outbox.queued.front().files.front().path == "map.ini");
    REQUIRE(outbox.queued.front().name == "Fremen Pass");
    REQUIRE(outbox.queued.front().manifest.find(fixture.root.string()) == std::string::npos);
}

TEST_CASE("An explicitly selected mod is attached and kept apart from vanilla","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto tornie = fixture.mod("Tornie","[Tank]\nPrice = 400\n");
    // The same scenario bytes started on two different mods are two distinct items.
    const auto onVanilla = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);
    const auto onTornie = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,tornie.hash);
    REQUIRE(onVanilla.hash != onTornie.hash);

    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,onTornie.hash,outbox.sink()));
    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,onVanilla.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 2);
    REQUIRE(outbox.queued[0].modHash == tornie.hash);
    REQUIRE(outbox.queued[1].modHash == vanilla.hash);
    REQUIRE(outbox.queued[0].name == outbox.queued[1].name);
    REQUIRE(outbox.queued[0].id == outbox.queued[1].id);
}

TEST_CASE("A map the metaserver already has is not collected again","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto map = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);
    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));

    // Starting the same map again before the upload finishes queues no second copy.
    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 1);

    fixture.store.setSharedVersion(map.hash,4);
    const auto decision = MapCollection::plan(fixture.store,GameType::CustomGame,false,false,map.hash);
    REQUIRE_FALSE(decision.collect);
    REQUIRE(decision.reason == "the metaserver already has this map");
    REQUIRE_FALSE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 1);
}

TEST_CASE("A map another player already shared is collected once, by receipt","[mapcollection]") {
    // Local item identity is per-creator, so the metaserver answers the content preflight
    // with its own revision of the same scenario. That receipt retires this local copy
    // without pretending the server assigned a version to this local hash.
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto map = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);
    const auto theirRevision = std::string(64,'1');

    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    fixture.store.setCollected(map.hash,theirRevision);
    REQUIRE(fixture.store.collected(map.hash));
    REQUIRE_FALSE(fixture.store.shared(map.hash));
    REQUIRE(fixture.store.get(map.hash).hash == map.hash); // The local revision is untouched.

    REQUIRE_FALSE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 1);
    REQUIRE_THROWS(fixture.store.setCollected(std::string(64,'2'),theirRevision)); // unknown revision
}

TEST_CASE("Changed scenario rules are collected without replacing the known map","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto original = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);
    fixture.store.setSharedVersion(original.hash,4);

    const auto edited = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario+"[UNITS]\nID000 = Harkonnen,Devastator\n",vanilla.hash);
    REQUIRE(edited.hash != original.hash);
    REQUIRE(edited.id == original.id); // Same item lineage, a newer version of it.
    REQUIRE(edited.version > original.version);

    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,edited.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 1);
    REQUIRE(outbox.queued.front().hash == edited.hash);
    REQUIRE(fixture.store.shared(original.hash));
    REQUIRE_FALSE(fixture.store.shared(edited.hash));
}

TEST_CASE("An unreachable metaserver neither fails the launch nor loses the map","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto map = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);

    outbox.offline = true;
    REQUIRE_NOTHROW(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE_FALSE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE(outbox.queued.empty());

    // The next start retries the same revision.
    outbox.offline = false;
    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
    REQUIRE(outbox.queued.size() == 1);
    REQUIRE(outbox.queued.front().hash == map.hash);
}

TEST_CASE("Only new offline custom matches contribute a map","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    const auto map = fixture.map(std::string(32,'a'),"Fremen Pass",kScenario,vanilla.hash);
    auto excluded = [&](GameType type, bool replay, bool save) {
        return !MapCollection::collect(fixture.store,type,replay,save,map.hash,outbox.sink());
    };
    REQUIRE(excluded(GameType::Campaign,false,false));
    REQUIRE(excluded(GameType::Skirmish,false,false));
    REQUIRE(excluded(GameType::CampaignCoop,false,false));
    REQUIRE(excluded(GameType::SkirmishCoop,false,false));
    REQUIRE(excluded(GameType::CustomMultiplayer,false,false));
    REQUIRE(excluded(GameType::LoadSavegame,false,true));
    REQUIRE(excluded(GameType::LoadMultiplayer,false,true));
    REQUIRE(excluded(GameType::LoadCoop,false,true));
    REQUIRE(excluded(GameType::Invalid,false,false));
    REQUIRE(excluded(GameType::CustomGame,true,false));  // watching a replay
    REQUIRE(excluded(GameType::CustomGame,false,true));  // resuming a saved custom game
    REQUIRE(outbox.queued.empty());
    REQUIRE(MapCollection::collect(fixture.store,GameType::CustomGame,false,false,map.hash,outbox.sink()));
}

TEST_CASE("Nothing but a pinned single-file scenario is offered as a map","[mapcollection]") {
    Fixture fixture; Outbox outbox;
    const auto vanilla = fixture.mod("vanilla","[Tank]\nPrice = 500\n");
    auto rejected = [&](const std::string& hash) {
        return !MapCollection::plan(fixture.store,GameType::CustomGame,false,false,hash).collect;
    };
    REQUIRE(rejected(""));                               // no map revision was pinned
    REQUIRE(rejected(vanilla.hash));                     // a mod package is not a map
    REQUIRE(rejected(std::string(64,'f')));              // unknown revision, no throw

    // A scenario revision can only ever be the single original map file: the store
    // refuses to capture live game state alongside it, and plan() would refuse the
    // resulting revision as well.
    const auto stage = fixture.root/".multi";
    fixture.write(stage/"map.ini",kScenario);
    fixture.write(stage/"autosave.dls","live game state");
    REQUIRE_THROWS(fixture.store.capture("map",std::string(32,'b'),"Bundle","",vanilla.hash,stage));

    // A scenario with no recorded mod cannot claim a dependency.
    REQUIRE(rejected(fixture.map(std::string(32,'c'),"Orphan",kScenario,"").hash));
    REQUIRE(outbox.queued.empty());
}
