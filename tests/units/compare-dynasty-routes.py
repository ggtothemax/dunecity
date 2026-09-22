#!/usr/bin/env python3
"""Compare complete Dynasty and DuneCity routes using real movement and script code.

Requires the macOS Ninja game build, original DUNE.PAK in its Resources,
and the pinned clean Dynasty checkout. No GUI or profile changes. Reference
presentation/network callbacks are stubbed, not movement/pathfinding/the VM.
"""
import argparse
import collections
import csv
import hashlib
import os
from pathlib import Path
import statistics
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dynasty-dir',type=Path,required=True)
p.add_argument('--build-dir',type=Path,default=ROOT/'build')
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args()
dd=a.dynasty_dir.resolve(); build=a.build_dir.resolve(); out=a.output_dir.resolve()
out.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','-C',str(dd),'rev-parse','HEAD'],text=True).strip()=='4469449c75f51388ad2725297a95f09a6c601905'
assert not subprocess.check_output(['git','-C',str(dd),'status','--porcelain','--','src','include'],text=True).strip()
SOURCES = ['src/unit.c', 'src/structure.c', 'src/map.c', 'src/tile.c', 'src/house.c', 'src/object.c', 'src/animation.c', 'src/explosion.c', 'src/team.c', 'src/scenario.c', 'src/ai.c', 'src/enhancement.c', 'src/file.c', 'src/ini.c', 'src/string.c', 'src/buildqueue.c', 'src/binheap.c', 'src/pool/pool_house.c', 'src/pool/pool_structure.c', 'src/pool/pool_team.c', 'src/pool/pool_unit.c', 'src/script/script.c', 'src/script/general.c', 'src/script/structure.c', 'src/script/team.c', 'src/script/unit.c', 'src/table/animation.c', 'src/table/explosion.c', 'src/table/fileinfo.c', 'src/table/houseanimation.c', 'src/table/houseinfo.c', 'src/table/landscapeinfo.c', 'src/table/locale.c', 'src/table/selectiontype.c', 'src/table/sound.c', 'src/table/structureinfo.c', 'src/table/teamaction.c', 'src/table/tilediff.c', 'src/table/unitinfo.c', 'src/table/widget.c', 'src/table/widgetinfo.c', 'src/table/windowdesc.c', 'src/tools/coord.c', 'src/tools/encoded_index.c', 'src/tools/orientation.c', 'src/tools/random_general.c', 'src/tools/random_lcg.c', 'src/tools/random_starport.c', 'src/tools/random_xorshift.c', 'src/os/endian.c', 'src/codec/format40.c', 'src/codec/format80.c']
objdir=out/'objects';objdir.mkdir(exist_ok=True)
shim=out/'include/allegro5';shim.mkdir(parents=True,exist_ok=True)
(shim/'allegro.h').write_text('#include <stdbool.h>\nbool al_make_directory(const char *path);\n')
(out/'include/buildcfg.h').write_text('#define DUNE_DYNASTY_STR "Dune Dynasty"\n#define DUNE_DYNASTY_VERSION "route-test"\n')
# Compile original sources. The Darwin macro shim only repairs an obsolete SDK
# snprintf declaration; it does not change the simulator.
flags=['-std=gnu11','-O1','-g','-fsanitize=address,undefined','-D__DARWIN_LDBL_COMPAT(x)=','-I'+str(dd/'include'),'-I'+str(out/'include')]
objects=[]
with (out/'build.log').open('w') as log:
 for source in SOURCES:
  obj=objdir/(source.replace('/','_')+'.o');objects.append(str(obj))
  subprocess.run(['cc',*flags,'-c',str(dd/source),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
 for source in ['dynasty-route-harness.c','dynasty-route-stubs.c']:
  obj=objdir/(source+'.o');objects.append(str(obj))
  subprocess.run(['cc',*flags,'-iquote',str(dd/'src'),'-c',str(ROOT/'tests/units'/source),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
 binary=out/'dynasty-routes'
 subprocess.run(['cc','-fsanitize=address,undefined','-Wl,-dead_strip','-o',str(binary),*objects,'-lm'],stdout=log,stderr=subprocess.STDOUT,check=True)
# Extract the user's existing game script, never download or commit game data.
archive=build/'bin/dunecity.app/Contents/Resources/DUNE.PAK'
data=archive.read_bytes();pos=0;entries=[]
while True:
 offset=struct.unpack_from('<I',data,pos)[0];pos+=4
 if offset==0:break
 end=data.index(b'\0',pos);name=data[pos:end].decode('ascii');pos=end+1
 entries.append((name,offset))
for i,(name,offset) in enumerate(entries):
 if name.upper()=='UNIT.EMC':
  end=entries[i+1][1] if i+1<len(entries) else len(data)
  script=data[offset:end];break
else:raise RuntimeError('UNIT.EMC missing from DUNE.PAK')
(out/'UNIT.EMC').write_bytes(script)
(out/'script-sha256.txt').write_text(hashlib.sha256(script).hexdigest()+'\n')
env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1',ASAN_OPTIONS='detect_leaks=0')
for scenario in [1,0]:
 with (out/f'dynasty-{scenario}.csv').open('w') as csvout, (out/f'dynasty-{scenario}.log').open('w') as log:
  subprocess.run([str(binary),str(out/'UNIT.EMC'),str(scenario)],stdout=csvout,stderr=log,check=True,env=env,timeout=120)
assert (out/'dynasty-0.csv').read_bytes()==(out/'dynasty-1.csv').read_bytes(), 'Produced and scenario unit timings differ'
subprocess.run(['python3',str(ROOT/'tests/units/run-unit-route-probe.py'),'--build-dir',str(build),'--output-dir',str(out/'city')],check=True)
def results(path):
 groups=collections.defaultdict(list)
 with path.open() as f:
  for r in csv.DictReader(f):groups[(r['unit'],int(r['tiles']),int(r['start_heading']))].append(float(r['seconds']))
 return groups
city=results(out/'city/vanilla.csv');dynasty=results(out/'dynasty-1.csv')
with (out/'comparison.csv').open('w') as f:
 w=csv.writer(f);w.writerow(['unit','tiles','initial_turn_degrees','dynasty_seconds','city_seconds','time_difference_percent'])
 for key,values in city.items():
  d=statistics.mean(dynasty[key]);c=statistics.mean(values)
  w.writerow([*key,d,c,(c/d-1)*100])
print('Comparison written to',out/'comparison.csv')
