/* Original Dynasty unit + structure loops and UNIT/BUILD.EMC.
 * Only presentation/transport is stubbed. Stationary/flyby diagnostic units
 * disable their order script; their movement still uses GameLoop_Unit.
 */
#define main original_route_main
#include "dynasty-route-harness.c"
#undef main
#include "pool/pool.h"
#include "net/server.h"
#include "tools/random_lcg.h"

int64_t g_tickScenarioStart;
int64_t g_tickStructureDegrade, g_tickStructureStructure, g_tickStructureScript, g_tickStructurePalace;
void Server_Send_PlaySound(enum HouseFlag houses, enum SoundID soundID) {}
void Server_Send_StatusMessage2(enum HouseFlag houses, uint8 priority, uint16 str1, uint16 str2) {}

/* Place a fixture directly in the original pools. Structure_Place calls
 * Structure_IsUpgradable for non-factories, which indexes upgradeCampaign[3]
 * in this pinned revision (UBSan). Do not patch/suppress that reference error:
 * initialize the turret's ordinary map/script state without upgrade handling.
 */
static Structure *place_turret(int x, int y) {
    Structure *s = Structure_Allocate(STRUCTURE_INDEX_INVALID, STRUCTURE_ROCKET_TURRET);
    assert(s);
    s->o.houseID = s->creatorHouseID = HOUSE_ATREIDES;
    s->o.position.x = x * 256;
    s->o.position.y = y * 256;
    s->o.linkedID = 0xff;
    s->o.hitpoints = s->hitpointsMax = g_table_structureInfo[s->o.type].o.hitpoints;
    s->o.seenByHouses = 0x3f;
    s->state = STRUCTURE_STATE_IDLE;
    s->objectType = 0xffff;
    Script_Reset(&s->o.script, g_scriptStructure);
    Script_Load(&s->o.script, s->o.type);
    g_map[x + y * 64].hasStructure = true;
    g_map[x + y * 64].index = s->o.index + 1;
    // The presentation icon-map stub has base turret sprite zero; orientation zero.
    g_map[x + y * 64].groundSpriteID = 0;
    return s;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    load_emc(argv[1]);
    ScriptInfo *saved = g_scriptUnit;
    g_scriptUnit = g_scriptStructure;
    load_emc(argv[2]);
    g_scriptUnit = saved;
    g_scriptStructure->functions = g_scriptFunctionsStructure;
    FILE *trace = fopen(argv[3], "w");
    assert(trace);
    fputs("scenario,aircraft,turrets,distance,seed,tick,projectile,source\n", trace);
    memcpy(g_table_unitInfo, g_table_unitInfo_original, sizeof(g_table_unitInfo));
    memcpy(g_table_structureInfo, g_table_structureInfo_original, sizeof(g_table_structureInfo));
    enhancement_true_game_speed_adjustment = true;
    enhancement_true_unit_movement_speed = false;
    const char *names[] = {"stationary", "flyby", "attack", "reload_control"};
    puts("scenario,aircraft,turrets,distance,seed,start_hp,end_hp,killed,ticks,seconds,shots,first_shot_tick,damage_events,damage_received,outcome");
    for (int scenario = 0; scenario < 4; scenario++)
    for (int air = 0; air < 2; air++)
    for (int n = 1; n <= 3; n += 2)
    for (int distance = 4; distance <= 12; distance += 4)
    for (int seed = 1; seed <= 3; seed++) {
        if (scenario == 2 && air) continue;
        if (scenario == 3 && (!air || n != 1 || distance != 8 || seed != 1)) continue;
        reset();
        for (int a = 0; a < HOUSE_NEUTRAL; a++)
        for (int b = 0; b < HOUSE_NEUTRAL; b++)
            g_table_houseAlliance[a][b] = a == b ? HOUSEALLIANCE_ALLIES : HOUSEALLIANCE_ENEMIES;
        Tools_Random_Seed(seed);
        Tools_RandomLCG_Seed(seed);
        g_tickStructureDegrade = g_tickStructureStructure = g_tickStructureScript = g_tickStructurePalace = 0;
        House *h = House_Allocate(HOUSE_HARKONNEN);
        h->unitCountMax = 100;
        h->flags.human = true;
        Structure *first = NULL;
        for (int i = 0; i < n; i++) {
            Structure *s = place_turret(20, 15 + i * 2);
            if (!first) first = s;
        }
        Unit *u = Unit_Create(UNIT_INDEX_INVALID, air ? UNIT_CARRYALL : UNIT_ORNITHOPTER,
            HOUSE_HARKONNEN, Tile_UnpackTile(20 + distance + 15 * 64), -64);
        assert(u);
        u->o.seenByHouses = 0x3f;
        if (scenario != 2) {
            u->o.script.script = NULL;
            u->currentDestination = Tile_UnpackTile(1 + 15 * 64);
            Unit_SetSpeed(u, scenario == 1 ? 255 : 0);
        } else {
            Unit_Server_SetAction(u, ACTION_ATTACK);
            Unit_SetTarget(u, Tools_Index_Encode(first->o.index, IT_STRUCTURE));
        }
        const int hp = u->o.hitpoints;
        int ticks = 0, shots = 0, firstShot = -1, events = 0, total = 0, lastHP = hp;
        bool killed = false;
        const char *outcome = "timeout";
        unsigned char seen[1000] = {0};
        uint16 fuse[1000] = {0};
        for (; ticks < 1200; ticks++) {
            GameLoop_Unit();
            GameLoop_Structure();
            g_timerGame++;
            unsigned char current[1000] = {0};
            PoolFindStruct f;
            for (Unit *b = Unit_FindFirst(&f, HOUSE_ATREIDES, UNIT_INVALID); b; b = Unit_FindNext(&f)) {
                if (b->o.type != UNIT_MISSILE_TURRET && b->o.type != UNIT_BULLET) continue;
                assert(b->o.index < 1000);
                // Original pool slots can be reused. A new fuse also detects
                // same-frame replacement of an old missile in its former slot.
                if (!seen[b->o.index] || b->fireDelay > fuse[b->o.index]) {
                    shots++;
                    if (firstShot < 0) firstShot = ticks;
                    fprintf(trace, "%s,%s,%d,%d,%d,%d,%u,%u\n", names[scenario],
                        air ? "Carryall" : "Ornithopter", n, distance, seed, ticks, b->o.type, b->originEncoded);
                }
                current[b->o.index] = 1;
                fuse[b->o.index] = b->fireDelay;
            }
            memcpy(seen, current, sizeof(seen));
            const int liveHP = u->o.flags.s.used ? u->o.hitpoints : 0;
            if (liveHP < lastHP) { events++; total += lastHP - liveHP; }
            lastHP = liveHP;
            if (liveHP <= 0) { killed = true; outcome = "killed"; ticks++; break; }
            if (scenario == 3) { u->o.hitpoints = hp; lastHP = hp; }
            if (scenario == 1 && u->o.position.x <= (20 - distance) * 256 + 128) {
                outcome = "escaped"; ticks++; break;
            }
        }
        assert(scenario != 1 || strcmp(outcome, "timeout") != 0);
        printf("%s,%s,%d,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%s\n", names[scenario],
            air ? "Carryall" : "Ornithopter", n, distance, seed, hp, lastHP, killed,
            ticks, ticks / 60.0, shots, firstShot, events, total, outcome);
    }
    fclose(trace);
    return 0;
}
