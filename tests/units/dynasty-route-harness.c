/* Production Dynasty GameLoop_Unit + actual UNIT.EMC, isolated route matrix. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "types.h"
#include "unit.h"
#include "structure.h"
#include "house.h"
#include "map.h"
#include "scenario.h"
#include "sprites.h"
#include "opendune.h"
#include "enhancement.h"
#include "script/script.h"
#include "pool/pool_unit.h"
#include "pool/pool_house.h"
#include "pool/pool_structure.h"
#include "pool/pool_team.h"
#include "tools/coord.h"
#include "tools/encoded_index.h"
#include "tools/random_general.h"
#include "timer/timer.h"
#include "animation.h"
#include "explosion.h"

static unsigned be32(const unsigned char *p) { return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | p[2]<<8 | p[3]; }
static void load_emc(const char *path) {
 FILE *f=fopen(path,"rb"); assert(f); fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);
 unsigned char *buf=malloc(len);assert(fread(buf,1,len,f)==(size_t)len);fclose(f);
 assert(!memcmp(buf,"FORM",4));
 g_scriptUnit->functions=g_scriptFunctionsUnit;
 for(size_t pos=12;pos+8<=(size_t)len;) {
  unsigned n=be32(buf+pos+4);assert(pos+8+n<=(size_t)len);
  unsigned char *copy=malloc(n);memcpy(copy,buf+pos+8,n);
  if(!memcmp(buf+pos,"ORDR",4)) {
   g_scriptUnit->offsets=(uint16*)copy;g_scriptUnit->offsetsCount=n/2;
   for(unsigned i=0;i<n/2;i++)g_scriptUnit->offsets[i]=(buf[pos+8+i*2]<<8)|buf[pos+9+i*2];
  } else if(!memcmp(buf+pos,"DATA",4)) {g_scriptUnit->start=(uint16*)copy;g_scriptUnit->startCount=n/2;}
  else if(!memcmp(buf+pos,"TEXT",4))g_scriptUnit->text=(uint16*)copy;
  else free(copy);
  pos+=8+n+(n&1);
 }
 assert(g_scriptUnit->start && g_scriptUnit->offsets);free(buf);
}
static void reset(void) {
 Unit_Init();Structure_Init();Team_Init();House_Init();Animation_Init();Explosion_Init();
 memset(g_map,0,sizeof(Tile)*64*64);memset(g_mapVisible,0,sizeof(FogOfWarTile)*64*64);
 g_landscapeSpriteID=127;g_builtSlabSpriteID=126;g_bloomSpriteID=208;g_wallSpriteID=300;
 static uint16 icons[65536];g_iconMap=icons;
 for(int i=0;i<4096;i++) {g_map[i].groundSpriteID=127;g_mapVisible[i].groundSpriteID=127;g_mapVisible[i].isUnveiled=0x3f;}
 g_scenario.mapScale=0;g_playerHouseID=HOUSE_ATREIDES;g_playerHouse=House_Allocate(HOUSE_ATREIDES);
 g_playerHouse->flags.human=true;g_playerHouse->unitCountMax=100;
 g_validateStrictIfZero=0;g_debugScenario=false;g_debugGame=false;
 g_timerGame=0;g_tickUnitMovement=0;g_tickUnitRotation=0;g_tickUnitBlinking=0;
 g_tickUnitMoveIndicator=0;g_tickUnitUnknown4=0;g_tickUnitScript=0;g_tickUnitUnknown5=0;g_tickUnitDeviation=0;
 Tools_Random_Seed(67);
 assert(Map_GetLandscapeType(10+10*64)==LST_NORMAL_SAND);
}
int main(int argc,char **argv) {
 assert(argc>=2);load_emc(argv[1]);
 memcpy(g_table_unitInfo,g_table_unitInfo_original,sizeof(g_table_unitInfo));
 memcpy(g_table_structureInfo,g_table_structureInfo_original,sizeof(g_table_structureInfo));
 enhancement_true_game_speed_adjustment=true;enhancement_true_unit_movement_speed=false;
 int types[]={UNIT_TANK,UNIT_TRIKE,UNIT_RAIDER_TRIKE,UNIT_QUAD,UNIT_HARVESTER,UNIT_SOLDIER,UNIT_TROOPER,
              UNIT_DEVASTATOR,UNIT_LAUNCHER,UNIT_SIEGE_TANK,UNIT_MCV,UNIT_DEVIATOR,UNIT_SONIC_TANK,UNIT_SABOTEUR};
 const char* cases[]={"sand","diagonal","rock","dunes","spice","damaged","loaded","mountain","corner","slab"};
 puts("unit,tiles,start_heading,phase,seconds,ticks,moving_ticks,stationary_ticks,scenario");
 for(int scenario=0;scenario<10;scenario++) for(unsigned a=0;a<sizeof(types)/sizeof(types[0]);a++) for(int length=8;length<=16;length+=8)
 for(int heading=0;heading<=90;heading+=90) for(int phase=0;phase<60;phase++) {
  if(scenario!=0 && length!=8) continue;
  if(scenario==6 && types[a]!=UNIT_HARVESTER) continue;
  if(scenario==7 && types[a]!=UNIT_SOLDIER && types[a]!=UNIT_TROOPER && types[a]!=UNIT_SABOTEUR) continue;
  reset();
  const unsigned sprite=scenario==2?143:scenario==3?159:scenario==4?176:scenario==7?160:scenario==9?126:127;
  for(int i=0;i<4096;i++) {g_map[i].groundSpriteID=sprite;g_mapVisible[i].groundSpriteID=sprite;}
  for(;g_timerGame<phase;g_timerGame++)GameLoop_Unit();
  const int endPacked=10+length+(10+(scenario==1?length:0))*64;
  const int cornerPacked=14+14*64;
  tile32 start=Tile_UnpackTile(10+10*64),end=Tile_UnpackTile(endPacked);
  bool cornerPending=scenario==8;
  if(cornerPending) end=Tile_UnpackTile(14+10*64);
  Unit *u=Unit_Create(UNIT_INDEX_INVALID,types[a],HOUSE_ATREIDES,start,(heading==0?64:0)+(scenario==1?32:0));assert(u);
  u->o.flags.s.byScenario=argc<3 || atoi(argv[2])!=0;u->o.flags.s.degrades=false;
  if(scenario==5) u->o.hitpoints=g_table_unitInfo[types[a]].o.hitpoints/3;
  if(scenario==6) u->amount=100;
  Unit_Server_SetAction(u,ACTION_MOVE);Unit_SetDestination(u,Tools_Index_Encode(cornerPending?14+10*64:endPacked,IT_TILE));
  unsigned frames=0,movingFrames=0;
  for(;frames<20000;frames++) {
   tile32 old=u->o.position;
   GameLoop_Unit();g_timerGame++;
   if(old.x!=u->o.position.x || old.y!=u->o.position.y)movingFrames++;
   if(u->o.position.x==end.x && u->o.position.y==end.y && !Unit_IsMoving(u)) {
    if(cornerPending) {cornerPending=false;end=Tile_UnpackTile(cornerPacked);Unit_Server_SetAction(u,ACTION_MOVE);Unit_SetDestination(u,Tools_Index_Encode(cornerPacked,IT_TILE));}
    else {frames++;break;}
   }
  }
  if(frames>=20000) {fprintf(stderr,"FAILED %s %d phase%d pos%u,%u speed%u target%u action%u script%td delay%u\n",g_table_unitInfo[types[a]].o.name,length,phase,u->o.position.x,u->o.position.y,u->speed,u->targetMove,u->actionID,u->o.script.script-u->o.script.scriptInfo->start,u->o.script.delay);return 2;}
  printf("%s,%d,%d,%d,%.9f,%u,%u,%u,%s\n",g_table_unitInfo[types[a]].o.name,length,heading,phase,frames/60.0,frames,movingFrames,frames-movingFrames,cases[scenario]);
 }
 return 0;
}
