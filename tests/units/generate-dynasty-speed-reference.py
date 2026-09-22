#!/usr/bin/env python3
"""Regenerate the movement oracle from the pinned Dynasty source, without running its game.

Uses verbatim Unit_SetSpeed, source-parsed unit/terrain tables and the default
normal-speed accumulator. ASan/UBSan cover the extracted C execution. Output
is a nominal coordinate rate; scripts, direction rounding and tile arrival
are intentionally outside this reference.
"""
import argparse
from pathlib import Path
import re, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dynasty-dir',type=Path,required=True)
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
root=args.dynasty_dir.resolve()
out=args.output_dir.resolve()
out.mkdir(parents=True,exist_ok=True)
commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
assert commit=='4469449c75f51388ad2725297a95f09a6c601905', 'Unexpected Dynasty reference commit'
assert not subprocess.check_output(['git','-C',str(root),'status','--porcelain','--','src/unit.c','src/table/unitinfo.c','src/table/landscapeinfo.c'],text=True).strip(), 'Modified reference sources'
s=(root/'src/unit.c').read_text()
def function(name):
 a=s.index('void '+name+'('); b=s.index('\n}',a)+2; return s[a:b]
table=(root/'src/table/unitinfo.c').read_text()
rows=[]
# IDs are the local game's stable item IDs; factors and rotation come from Dynasty.
ids={"'Thopter":35,'Frigate':30,'Devastator':28,'Deviator':29,'Harvester':31,'Launcher':33,'MCV':34,'Quad':36,'Raider Trike':43,'Saboteur':37,'Sandworm':38,'Siege Tank':39,'Soldier':32,'Sonic Tank':40,'Tank':41,'Trike':42,'Trooper':44}
for chunk in re.split(r'\t\{ /\* \d+ \*/',table)[1:]:
 name=re.search(r'/\* name +\*/ "([^"]+)"',chunk)[1]
 if name not in ids:continue
 val=lambda key: re.search(r'/\* '+key+r' +\*/ (\w+)',chunk)[1]
 types={'MOVEMENT_FOOT':0,'MOVEMENT_TRACKED':1,'MOVEMENT_HARVESTER':2,'MOVEMENT_WHEELED':3,'MOVEMENT_WINGER':4,'MOVEMENT_SLITHER':5}
 rows.append((ids[name],int(val('movingSpeedFactor')),int(val('turningSpeed')),types[val('movementType')]))
assert len(rows)==17,rows
land=(root/'src/table/landscapeinfo.c').read_text()
terrains=[]
for name in ['LST_CONCRETE_SLAB','LST_NORMAL_SAND','LST_ENTIRELY_ROCK','LST_ENTIRELY_DUNE','LST_SPICE','LST_THICK_SPICE','LST_ENTIRELY_MOUNTAIN']:
 a=land.index('/ '+name+' */'); nums=re.search(r'movementSpeed +\*/ \{ ([\d, ]+) \}',land[a:])[1];terrains.append(nums)
code='''#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef uint16_t uint16;
#define UNIT_HARVESTER 31
typedef struct { struct { int type; } o; int amount,speed,speedPerTick,speedRemainder,movingSpeed; } Unit;
typedef struct { int movingSpeedFactor; } Info;
Info g_table_unitInfo[64];
bool enhancement_true_unit_movement_speed=false;
// Normal60Hz with Dynasty default true_game_speed: function returns normal unchanged.
uint16 Tools_AdjustToGameSpeed(uint16 normal,uint16 minimum,uint16 maximum,bool inverse){return normal;}
'''+function('Unit_SetSpeed')+'\n'
code+='int rows[][4]={'+','.join('{'+','.join(map(str,row))+'}' for row in rows)+'};\n'
code+='int terrain[][6]={'+','.join('{'+v+'}' for v in terrains)+'};\n'
code+='''int main(void) {
 puts("unit,terrain,damaged,loaded,tiles_per_second,turn_degrees_per_second");
 for(int r=0;r<17;r++) for(int t=0;t<7;t++) for(int damaged=0;damaged<2;damaged++) for(int load=0;load<2;load++) {
  int id=rows[r][0],kind=rows[r][3]; if(load && id!=31)continue;
  if(terrain[t][kind]==0) continue;
  g_table_unitInfo[id].movingSpeedFactor=rows[r][1];
  Unit u={0};u.o.type=id;u.amount=load*100;
  int throttle=terrain[t][kind];
  if(damaged && kind!=4)throttle-=throttle/4;
  Unit_SetSpeed(&u,throttle);
  // Exercise Dynasty's accumulator for 256 movement ticks, eliminating phase error.
  int total=0;
  for(int tick=0;tick<256;tick++) {
   int speed=u.speedRemainder+(u.speedPerTick==255 ? 256 : u.speedPerTick);
   if(speed>255)total+=u.speed;
   u.speedRemainder=speed&255;
  }
  printf("%d,%d,%d,%d,%.9f,%.9f\\n",id,(int[]){0,1,2,3,5,6,4}[t],damaged,load,total/256.0*20/256,rows[r][2]*4.0/256*360*15);
 }
}
'''
(out/'dynasty-unit-speed-reference.c').write_text(code)
subprocess.run(['cc','-fsanitize=address,undefined','-g',str(out/'dynasty-unit-speed-reference.c'),'-o',str(out/'reference')],check=True)
with (out/'dynasty-unit-speeds.csv').open('w') as f:subprocess.run([str(out/'reference')],stdout=f,check=True)
print(rows)
