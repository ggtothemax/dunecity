#!/usr/bin/env python3
"""Measure rocket-turret anti-air performance with the production game loop.

Builds tests/units/aircraft-aa-probe.inc against the existing Ninja Release objects
and runs it headless for vanilla, dunecity and Dune2R with isolated profiles.
Diagnostic only: it changes no gameplay values and writes nothing outside --output-dir.
"""
import argparse
import csv
import os
from collections import defaultdict
from pathlib import Path
import shlex
import subprocess
import tempfile

MODS = ('vanilla', 'dunecity', 'Dune2R')
root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-aircraft-aa-'))
out.mkdir(parents=True, exist_ok=True)

subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runUnitSpeedProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/aircraft-aa-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'aircraft-aa-probe-main.cpp'
source.write_text(main)

cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Test compilation must not overwrite Ninja's production dependency records.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i + 2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'aircraft-aa-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Read production targeting state only in this diagnostic binary.

app = out / 'aircraft-aa-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/aircraft-aa-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

for mod in MODS:
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               UNIT_SPEED_PROBE_MOD=mod, UNIT_SPEED_PROBE_OUT=str(out),
               UNIT_SPEED_PROBE_BASELINE='0')
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1800)
    if 'UNIT_SPEED_PROBE_PASS:' not in logfile.read_text():
        raise RuntimeError('Missing aircraft anti-air result: ' + str(logfile))

rows = []
for mod in MODS:
    with (out / (mod + '.csv')).open() as handle:
        rows.extend(list(csv.DictReader(handle)))
with (out / 'encounters.csv').open('w', newline='') as handle:
    writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)
for suffix in ('tables', 'flags'):
    merged = []
    for mod in MODS:
        text = (out / ('%s-%s.csv' % (mod, suffix))).read_text().splitlines()
        merged.extend(text[1:] if merged else text)
    (out / (suffix + '.csv')).write_text('\n'.join(merged) + '\n')

# Per-mode payload minus the mod column. The three modes run the same fixture with the
# same seeds, so identical payloads are one observation replayed, not three independent ones.
payloads = {mod: [{k: v for k, v in row.items() if k != 'mod'}
                  for row in rows if row['mod'] == mod] for mod in MODS}
identical = all(payloads[mod] == payloads[MODS[0]] for mod in MODS)

CYCLE_S = 0.016  # GAMESPEED_DEFAULT = 16 ms per game cycle (include/Definitions.h:97)
matrix = [r for r in rows if r['diagnostic'] == '0']
diagnostics = [r for r in rows if r['diagnostic'] == '1']

groups = defaultdict(list)
for row in matrix:
    groups[(row['aircraft'], row['scenario'], row['turrets'])].append(row)
summary = out / 'summary.csv'
with summary.open('w', newline='') as handle:
    writer = csv.writer(handle)
    writer.writerow(['aircraft', 'scenario', 'turrets', 'encounters', 'kills', 'kill_rate',
                     'escaped', 'timeouts', 'mean_health_loss_capped', 'mean_shots',
                     'shots_rocket', 'shots_cannon', 'mean_acquire_s', 'never_acquired',
                     'mean_target_s', 'mean_aimed_s', 'health_loss_capped_per_shot',
                     'mean_path_tiles'])
    for key in sorted(groups):
        g = groups[key]
        n = len(g)
        kills = sum(int(r['killed']) for r in g)
        shots = sum(int(r['shots_total']) for r in g)
        loss = sum(int(r['health_loss_capped']) for r in g)
        acquired = [int(r['acquire_tick']) for r in g if int(r['acquire_tick']) >= 0]
        writer.writerow([*key, n, kills, round(kills / n, 3),
                         sum(1 for r in g if r['outcome'] == 'escaped'),
                         sum(1 for r in g if r['outcome'] == 'timeout'),
                         round(loss / n, 2), round(shots / n, 2),
                         sum(int(r['shots_rocket']) for r in g),
                         sum(int(r['shots_cannon']) for r in g),
                         round(sum(acquired) * CYCLE_S / len(acquired), 3) if acquired else '',
                         n - len(acquired),
                         round(sum(int(r['target_ticks']) for r in g) * CYCLE_S / n, 3),
                         round(sum(int(r['aimed_ticks']) for r in g) * CYCLE_S / n, 3),
                         round(loss / shots, 2) if shots else '',
                         round(sum(float(r['path_tiles']) for r in g) / n, 2)])

# Validity gates, reported rather than hidden: every flyby must end in a kill or a real
# exit-plane crossing, and the pinned diagnostic must kill both aircraft types.
cross = [r for r in matrix if r['scenario'] == 'cross']
bad_cross = [r for r in cross if r['outcome'] not in ('killed', 'escaped')]
no_traverse = [r for r in cross if r['crossed_turret_line'] == '0' and r['outcome'] != 'killed']
undead = [r for r in diagnostics if r['killed'] == '0']
moved = [r for r in diagnostics if float(r['max_disp_tiles']) > 0.0]
orbit_moved = all(float(r['path_tiles']) > 0 for r in matrix if r['scenario'] == 'idle_orbit')

subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Aircraft anti-air encounters recorded for all three modes'
      + ('; per-mode payloads identical (same fixture and seeds replayed).' if identical
         else '; MODES DIVERGED.'))
print('matrix encounters=%d  cross=%d escaped=%d killed=%d invalid(timeout)=%d never_traversed=%d'
      % (len(matrix), len(cross), sum(1 for r in cross if r['outcome'] == 'escaped'),
         sum(1 for r in cross if r['outcome'] == 'killed'), len(bad_cross), len(no_traverse)))
print('pinned diagnostics=%d survivors=%d sampled_displacement>0=%d   idle_orbit always moved: %s'
      % (len(diagnostics), len(undead), len(moved), orbit_moved))
print('Results: ' + str(out / 'encounters.csv') + ' and ' + str(summary))

assert len(rows) == 552, 'Incomplete encounter matrix'
assert identical, 'Unexpected cross-mode simulation divergence'
assert not bad_cross and not no_traverse, 'Invalid flyby fixture'
assert all(r['exit_crossed'] == '1' for r in cross if r['outcome'] == 'escaped')
assert not undead and not moved, 'Stationary damage diagnostic failed'
assert orbit_moved, 'Idle flight fixture did not move'
assert all(r['shots_total'] == r['shots_weapontimer'] for r in rows), 'Shot counters disagree'
assert all(int(r['produced_power']) >= int(r['required_power']) for r in rows), 'Insufficient power'
