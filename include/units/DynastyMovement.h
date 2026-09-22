#pragma once

#include <data.h>
#include <DataTypes.h>

// Normal-speed Dynasty rates (4469449c), before direction-table rounding.
// Terrain throttle and damage/load are applied BEFORE Unit_SetSpeed quantizes.
namespace DynastyMovement {
inline int factor(int item) {
    switch(item) {
        case Unit_Soldier: return 8;
        case Unit_Trooper: return 15;
        case Unit_Saboteur: return 40;
        case Unit_Devastator: return 10;
        case Unit_Tank: return 25;
        case Unit_SiegeTank: case Unit_Harvester: case Unit_MCV: return 20;
        case Unit_Launcher: case Unit_Deviator: case Unit_SonicTank: return 30;
        case Unit_Trike: return 45;
        case Unit_RaiderTrike: return 60;
        case Unit_Quad: return 40;
        case Unit_Sandworm: return 35;
        default: return 0; // Mod-only units retain their configurable Legacy rules.
    }
}
inline int throttle(int item, TERRAINTYPE terrain) {
    if(terrain == Terrain_Slab) return 255;
    if(item == Unit_Sandworm) return 192;
    const bool foot = item == Unit_Soldier || item == Unit_Trooper || item == Unit_Saboteur;
    if(terrain == Terrain_Mountain) return foot ? 64 : 0;
    if(foot) return 112;
    const bool wheeled = item == Unit_Trike || item == Unit_RaiderTrike || item == Unit_Quad;
    if(terrain == Terrain_Rock) return wheeled ? 112 : 160;
    const bool sand = terrain == Terrain_Sand || terrain == Terrain_SpiceBloom
        || terrain == Terrain_GreenSpiceBloom || terrain == Terrain_RedSpiceBloom
        || terrain == Terrain_SpecialBloom;
    return sand && !wheeled ? 112 : 160;
}
// Below 16, Dynasty moves 16 units intermittently. Return its average step.
inline int step(int item, int throttleValue, bool damaged, int cargoPercent) {
    if(damaged) throttleValue -= throttleValue / 4;
    if(item == Unit_Harvester) throttleValue = (255-cargoPercent)*throttleValue/256;
    int speed = factor(item)*throttleValue/256;
    return speed >= 16 ? (speed/16)*16 : speed;
}
}
