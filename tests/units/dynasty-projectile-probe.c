#define main original_route_main
#include "dynasty-route-harness.c"
#undef main
int main(int argc,char**argv) {
 assert(argc==2 || argc==3);load_emc(argv[1]);
 memcpy(g_table_unitInfo,g_table_unitInfo_original,sizeof(g_table_unitInfo));
 memcpy(g_table_structureInfo,g_table_structureInfo_original,sizeof(g_table_structureInfo));
 enhancement_true_game_speed_adjustment=true;enhancement_true_unit_movement_speed=false;
 int types[]={UNIT_MISSILE_ROCKET,UNIT_MISSILE_DEVIATOR,UNIT_MISSILE_TURRET,UNIT_MISSILE_TROOPER,UNIT_MISSILE_HOUSE};
 int targets[]={UNIT_TANK,UNIT_ORNITHOPTER,UNIT_CARRYALL};
 if(argc==3) {
  FILE*f=fopen(argv[2],"w");assert(f);fprintf(f,"type,air,range,heading,frame,x,y,orientation,fuse\n");
  for(int k=0;k<5;k++)for(int air=0;air<2;air++)for(int range=1;range<=20;range+=(range==1?2:range==3?5:range==8?4:8))for(int heading=0;heading<256;heading+=64) {
   reset();House*h=House_Allocate(HOUSE_HARKONNEN);h->unitCountMax=100;
   tile32 start=Tile_UnpackTile(25+15*64),end=Tile_UnpackTile((25+range)+15*64);
   Unit*target=Unit_Create(UNIT_INDEX_INVALID,air?UNIT_ORNITHOPTER:UNIT_TANK,HOUSE_HARKONNEN,end,0);assert(target);
   Unit_SetSpeed(target,0);target->o.script.script=NULL;
   Unit*b=Unit_CreateBullet(start,types[k],HOUSE_ATREIDES,0,Tools_Index_Encode(target->o.index,IT_UNIT));assert(b);
   b->currentDestination=end;Unit_SetOrientation(b,heading,true,0);
   for(int frame=0;frame<300;frame++) {
    while(g_timerGame*50 < (unsigned)(frame+1)*48) {GameLoop_Unit();g_timerGame++;}
    if(!b->o.flags.s.used) {fprintf(f,"%d,%d,%d,%d,%d,-1,-1,-1,-1\n",k,air,range,heading,frame);break;}
    fprintf(f,"%d,%d,%d,%d,%d,%u,%u,%u,%u\n",k,air,range,heading,frame,b->o.position.x,b->o.position.y,(uint8)b->orientation[0].current,b->fireDelay);
   }
  }
  fclose(f);return 0;
 }
 puts("type,target,case,speed_tiles_s,initial_fuse,turn_deg_s,lifetime_s,target_damage,end_distance_tiles");
 for(unsigned k=0;k<5;k++) for(unsigned t=0;t<3;t++) for(int scenario=0;scenario<3;scenario++) {
  reset();House*h=House_Allocate(HOUSE_HARKONNEN);h->unitCountMax=100;
  tile32 start=Tile_UnpackTile(10+10*64),end=Tile_UnpackTile(18+10*64);
  Unit*target=Unit_Create(UNIT_INDEX_INVALID,targets[t],HOUSE_HARKONNEN,end,0);assert(target);
  Unit_SetSpeed(target,0);target->o.script.script=NULL;
  Unit*b=Unit_CreateBullet(start,types[k],HOUSE_ATREIDES,40,Tools_Index_Encode(target->o.index,IT_UNIT));assert(b);
  // Isolate steering/fuse behavior from random scatter for the trajectory test.
  b->currentDestination=end;
  const unsigned hp=target->o.hitpoints, fuse=b->fireDelay;
  const double speed=b->speed*(b->speedPerTick==255?1:b->speedPerTick/256.0)*20.0/256;
  const double rotation=g_table_unitInfo[types[k]].turningSpeed*4*15*360.0/256;
  if(scenario==1)Unit_SetOrientation(b,b->orientation[0].current+128,true,0);
  int ticks=0;double distance=8;
  for(;ticks<1200;ticks++) {
   if(scenario==2 && t!=0) {target->o.position.y+=48;if(target->o.position.y>(10+6)*256)target->o.position.y=10*256;}
   distance=Tile_GetDistance(b->o.position,target->o.position)/256.0;
   GameLoop_Unit();g_timerGame++;
   if(!b->o.flags.s.used) {++ticks;break;}
  }
  printf("%s,%s,%d,%.6f,%u,%.6f,%.6f,%d,%.6f\n",g_table_unitInfo[types[k]].o.name,g_table_unitInfo[targets[t]].o.name,scenario,speed,fuse,rotation,ticks/60.0,(int)hp-target->o.hitpoints,distance);
 }
 for(int offset=0;offset<3;offset++) {
  reset();House*h=House_Allocate(HOUSE_HARKONNEN);h->unitCountMax=100;
  tile32 end=Tile_UnpackTile(18+10*64);
  Unit*target=Unit_Create(UNIT_INDEX_INVALID,UNIT_ORNITHOPTER,HOUSE_HARKONNEN,end,0);assert(target);
  int before=target->o.hitpoints;int offsets[]={0,96,160};end.x+=offsets[offset];
  Map_MakeExplosion(EXPLOSION_IMPACT_EXPLODE,end,8,0);
  printf("Blast,Ornithopter,%d,0,0,0,0,%d,%.6f\n",offsets[offset]/4,before-target->o.hitpoints,offsets[offset]/256.0);
 }
}
