#!/usr/bin/env python3
"""Replay a recorded match with production engine objects in an isolated profile."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import shutil

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--replay', type=Path, required=True)
parser.add_argument('--cycles', type=int, default=40000)
parser.add_argument('--trace-throws', action='store_true', help='Print the original stack of simulation exceptions')
parser.add_argument('--workshop-from', type=Path, help='Copy the recorded match revision cache into the isolated profile')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3',str(root/'scripts/check-build-deps.py'),str(build)],check=True,cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()
main = (root/'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle,'int menuResult = runReplayProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "'+str(root/'tests/replay/replay-probe.inc')+'"\n' + main[position:]
source, obj = out/'replay-main.cpp', out/'replay-main.o'
source.write_text(main)
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include','-MT','-MF'):
    if option in compile_command:
        position = compile_command.index(option)
        del compile_command[position:position+2]
for option in ('-MD','-MMD'):
    if option in compile_command: compile_command.remove(option)
compile_command[compile_command.index('-o')+1] = str(obj)
compile_command[compile_command.index('-c')+1] = str(source)
compile_command.append('-fno-access-control')
if args.trace_throws: compile_command.append('-DREPLAY_TRACE_THROWS')
app = out/'replay-probe.app/Contents'
(app/'MacOS').mkdir(parents=True, exist_ok=True)
if not (app/'Resources').exists(): (app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/replay-probe'
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out/'build.log').open('w') as log:
    subprocess.run(compile_command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run(link,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
profile = out/'profile'
profile.mkdir(exist_ok=True)
if args.workshop_from:
    shutil.copytree(args.workshop_from/'revisions',profile/'workshop/revisions',dirs_exist_ok=True)
(profile/'Dune City.ini').write_text('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\nFullscreen = false\n[General]\nPlay Intro = false\n')
env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', REPLAY_PROBE_FILE=str(args.replay.resolve()), REPLAY_PROBE_CYCLES=str(args.cycles))
command = [str(binary), '--window', '--showlog']
with (out/'run.log').open('w') as log:
    result = subprocess.run(command, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=300)
results = [line for line in (out/'run.log').read_text().splitlines() if 'REPLAY_PROBE_' in line]
print('\n'.join(results))
if (result.returncode or not any('REPLAY_PROBE_PASS:' in line for line in results)):
    raise RuntimeError('Replay failed; see '+str(out/'run.log'))
