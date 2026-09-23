/* Firing timestamps from original unit/structure loops, actual EMC scripts.
 * Stationary non-retaliating targets receive diagnostic HP so combat continues.
 */
#define DYNASTY_REFERENCE_HELPERS_ONLY
#include "dynasty-aircraft-reference.c"

int main(int argc, char **argv) {
    assert(argc == 4);
    load_emc(argv[1]);
    ScriptInfo *saved = g_scriptUnit;
    g_scriptUnit = g_scriptStructure;
    load_emc(argv[2]);
    g_scriptUnit = saved;
    g_scriptStructure->functions = g_scriptFunctionsStructure;
    memcpy(g_table_unitInfo, g_table_unitInfo_original, sizeof(g_table_unitInfo));
    memcpy(g_table_structureInfo, g_table_structureInfo_original, sizeof(g_table_structureInfo));
    enhancement_true_game_speed_adjustment = true;
    enhancement_true_unit_movement_speed = false;
    FILE *trace = fopen(argv[3], "w");
    assert(trace);
    fputs("shooter,health_percent,target,seed,shot,tick,seconds,projectile,source,shooter_delay,burst_flip\n", trace);
    puts("shooter,health_percent,target,seed,shots,seconds");
    for (int kind = 0; kind < 3; kind++)
    for (int health = 0; health < (kind == 2 ? 3 : 1); health++)
    for (int air = 0; air < 2; air++)
    for (int seed = 1; seed <= 3; seed++) {
        const char *name = kind == 0 ? "rocket_turret" : kind == 1 ? "turret_cannon" : "launcher";
        const int percent = health == 0 ? 100 : health == 1 ? 50 : 33;
        const int distance = kind == 1 ? 2 : 8;
        reset();
        for (int a = 0; a < HOUSE_NEUTRAL; a++) for (int b = 0; b < HOUSE_NEUTRAL; b++)
            g_table_houseAlliance[a][b] = a == b ? HOUSEALLIANCE_ALLIES : HOUSEALLIANCE_ENEMIES;
        Tools_Random_Seed(seed); Tools_RandomLCG_Seed(seed);
        g_tickStructureDegrade = g_tickStructureStructure = g_tickStructureScript = g_tickStructurePalace = 0;
        House *h = House_Allocate(HOUSE_HARKONNEN); h->unitCountMax = 100; h->flags.human = true;
        Unit *launcher = NULL;
        if (kind == 2) {
            launcher = Unit_Create(UNIT_INDEX_INVALID, UNIT_LAUNCHER, HOUSE_ATREIDES,
                Tile_UnpackTile(20 + 15 * 64), 64);
            assert(launcher); launcher->o.hitpoints = percent;
        } else place_turret(20, 15);
        Unit *target = Unit_Create(UNIT_INDEX_INVALID, air ? UNIT_CARRYALL : UNIT_TANK,
            HOUSE_HARKONNEN, Tile_UnpackTile(20 + distance + 15 * 64), 0);
        assert(target); target->o.seenByHouses = 0x3f;
        target->o.script.script = NULL; Unit_SetSpeed(target, 0); target->o.hitpoints = 60000;
        if (launcher) {
            Unit_Server_SetAction(launcher, ACTION_ATTACK);
            Unit_SetTarget(launcher, Tools_Index_Encode(target->o.index, IT_UNIT));
        }
        int shots = 0;
        unsigned char seen[1000] = {0}; uint16 fuse[1000] = {0};
        for (int tick = 0; tick < 7200; tick++) {
            GameLoop_Unit(); GameLoop_Structure(); g_timerGame++;
            assert(target->o.flags.s.used && target->o.hitpoints > 0);
            unsigned char current[1000] = {0}; PoolFindStruct f;
            for (Unit *b = Unit_FindFirst(&f, HOUSE_ATREIDES, UNIT_INVALID); b; b = Unit_FindNext(&f)) {
                if (b->o.type != UNIT_MISSILE_TURRET && b->o.type != UNIT_MISSILE_ROCKET && b->o.type != UNIT_BULLET) continue;
                assert(b->o.index < 1000);
                if (!seen[b->o.index] || b->fireDelay > fuse[b->o.index]) {
                    shots++;
                    fprintf(trace, "%s,%d,%s,%d,%d,%d,%.9f,%u,%u,%u,%u\n", name, percent,
                        air ? "air" : "ground", seed, shots, tick, tick / 60.0,
                        b->o.type, b->originEncoded, launcher ? launcher->fireDelay : 0,
                        launcher ? launcher->o.flags.s.fireTwiceFlip : 0);
                }
                current[b->o.index] = 1; fuse[b->o.index] = b->fireDelay;
            }
            memcpy(seen, current, sizeof(seen));
        }
        printf("%s,%d,%s,%d,%d,120\n", name, percent, air ? "air" : "ground", seed, shots);
    }
    fclose(trace);
}
