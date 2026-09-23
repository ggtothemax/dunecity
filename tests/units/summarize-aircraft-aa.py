#!/usr/bin/env python3
"""Break the aircraft anti-air encounters down by distance, weapon and validity.

Reads encounters.csv from run-aircraft-aa-probe.py. Every number printed here is a
count or a mean of recorded encounters; nothing is extrapolated. The three mods run
the same fixture with the same seeds, so per-mod rows are replays of one matrix and
are pooled only where the printout says so.
"""
import argparse
import collections
import csv
from pathlib import Path

CYCLE_S = 0.016  # GAMESPEED_DEFAULT = 16 ms per game cycle (include/Definitions.h:97)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', type=Path, required=True, help='encounters.csv from run-aircraft-aa-probe.py')
parser.add_argument('--flags', type=Path, help='flags.csv written next to the results')
args = parser.parse_args()
rows = list(csv.DictReader(args.results.open()))
mods = sorted({r['mod'] for r in rows})
matrix = [r for r in rows if r['diagnostic'] == '0']
diagnostics = [r for r in rows if r['diagnostic'] == '1']
print('rows: %d (matrix %d, diagnostic %d)   mods: %s' % (len(rows), len(matrix), len(diagnostics), mods))
for mod in mods:
    sub = [r for r in matrix if r['mod'] == mod]
    print('  %-9s matrix encounters=%d kills=%d escaped=%d timeouts=%d'
          % (mod, len(sub), sum(int(r['killed']) for r in sub),
             sum(1 for r in sub if r['outcome'] == 'escaped'),
             sum(1 for r in sub if r['outcome'] == 'timeout')))

print('\nper scenario / turret count (all mods pooled; identical replays):')
print('%-12s%-12s%3s%6s%7s%9s%9s%8s%9s%9s%9s' %
      ('aircraft', 'scenario', 'T', 'n', 'kills', 'escaped', 'timeout', 'shots', 'rockets', 'cannon', 'loss'))
groups = collections.defaultdict(list)
for row in matrix:
    groups[(row['aircraft'], row['scenario'], row['turrets'])].append(row)
for key in sorted(groups):
    g = groups[key]
    print('%-12s%-12s%3s%6d%7d%9d%9d%8.2f%9d%9d%9.1f' % (
        key[0], key[1], key[2], len(g),
        sum(int(r['killed']) for r in g),
        sum(1 for r in g if r['outcome'] == 'escaped'),
        sum(1 for r in g if r['outcome'] == 'timeout'),
        sum(int(r['shots_total']) for r in g) / len(g),
        sum(int(r['shots_rocket']) for r in g),
        sum(int(r['shots_cannon']) for r in g),
        sum(int(r['health_loss_capped']) for r in g) / len(g)))

print('\nby distance (tiles between the turret column and the aircraft start):')
print('%-12s%-12s%3s%5s%5s%7s%8s%8s%8s%8s%8s%9s' %
      ('aircraft', 'scenario', 'T', 'dist', 'n', 'kills', 'shots', 'loss', 'acq_s', 'aimed_s', 'minD', 'path'))
groups = collections.defaultdict(list)
for row in matrix:
    groups[(row['aircraft'], row['scenario'], row['turrets'], int(row['distance']))].append(row)
for key in sorted(groups):
    g = groups[key]
    n = len(g)
    acq = [int(r['acquire_tick']) for r in g if int(r['acquire_tick']) >= 0]
    print('%-12s%-12s%3s%5s%5d%7d%8.2f%8.2f%8.2f%8.2f%8.2f%9.1f' % (
        key[0], key[1], key[2], key[3], n,
        sum(int(r['killed']) for r in g),
        sum(int(r['shots_total']) for r in g) / n,
        sum(int(r['health_loss_capped']) for r in g) / n,
        (sum(acq) * CYCLE_S / len(acq)) if acq else -1,
        sum(int(r['aimed_ticks']) for r in g) * CYCLE_S / n,
        min(float(r['min_distance_tiles']) for r in g),
        sum(float(r['path_tiles']) for r in g) / n))

print('\nshot bookkeeping (projectile spawns vs weaponTimer resets vs combatStats):')
for aircraft in ('Ornithopter', 'Carryall'):
    sub = [r for r in matrix if r['aircraft'] == aircraft]
    if not sub:
        continue
    shots = sum(int(r['shots_total']) for r in sub)
    timer = sum(int(r['shots_weapontimer']) for r in sub)
    stats = sum(int(r['cs_fires_on_orni']) for r in sub)
    loss = sum(int(r['health_loss_capped']) for r in sub)
    print('%-12s n=%d spawns=%d (rocket %d, cannon %d) weaponTimer=%d combatStats_fires=%d '
          'kills=%d never_acquired=%d capped_health_loss=%d loss_per_spawn=%.2f'
          % (aircraft, len(sub), shots,
             sum(int(r['shots_rocket']) for r in sub), sum(int(r['shots_cannon']) for r in sub),
             timer, stats, sum(int(r['killed']) for r in sub),
             sum(1 for r in sub if int(r['acquire_tick']) < 0), loss,
             loss / shots if shots else 0))

print('\ndamage events attributed to a single ended projectile (unattributed events excluded):')
for aircraft in ('Ornithopter', 'Carryall'):
    sub = [r for r in rows if r['aircraft'] == aircraft]
    events = sum(int(r['damage_events']) for r in sub)
    rocket = sum(int(r['rocket_damage_events']) for r in sub)
    cannon = sum(int(r['cannon_damage_events']) for r in sub)
    print('%-12s damage_events=%d rocket=%d cannon=%d unattributed=%d max_single_drop=%d'
          % (aircraft, events, rocket, cannon, events - rocket - cannon,
             max([int(r['max_drop']) for r in sub], default=0)))

print('\nmotion evidence (idle_orbit is orbiting flight, never stationary):')
for scenario in ('idle_orbit', 'attack', 'cross'):
    sub = [r for r in matrix if r['scenario'] == scenario]
    if not sub:
        continue
    print('%-12s n=%d min_path=%.2f mean_path=%.2f max_net_displacement=%.2f tiles over mean %.2f s'
          % (scenario, len(sub), min(float(r['path_tiles']) for r in sub),
             sum(float(r['path_tiles']) for r in sub) / len(sub),
             max(float(r['net_disp_tiles']) for r in sub),
             sum(int(r['ticks']) for r in sub) * CYCLE_S / len(sub)))

cross = [r for r in matrix if r['scenario'] == 'cross']
print('\ncross validity: n=%d escaped=%d killed=%d timeout=%d traversed_turret_line=%d exit_plane_crossed=%d'
      % (len(cross), sum(1 for r in cross if r['outcome'] == 'escaped'),
         sum(1 for r in cross if r['outcome'] == 'killed'),
         sum(1 for r in cross if r['outcome'] == 'timeout'),
         sum(1 for r in cross if r['crossed_turret_line'] == '1'),
         sum(1 for r in cross if r['exit_crossed'] == '1')))

print('\npinned diagnostic (fixture writes the position back every cycle):')
print('%-12s%-8s%7s%9s%9s%9s%9s%9s' % ('aircraft', 'mod', 'dist', 'max_disp', 'killed', 'shots', 'rockets', 'cannon'))
for r in diagnostics:
    print('%-12s%-8s%7s%9s%9s%9s%9s%9s' % (r['aircraft'], r['mod'], r['distance'], r['max_disp_tiles'],
                                           r['killed'], r['shots_total'], r['shots_rocket'], r['shots_cannon']))
if diagnostics:
    print('pinned kills: %d/%d, sampled displacement above zero: %d'
          % (sum(int(r['killed']) for r in diagnostics), len(diagnostics),
             sum(1 for r in diagnostics if float(r['max_disp_tiles']) > 0)))

powered = {(r['rockets_need_power'], r['produced_power'], r['required_power']) for r in rows}
print('\npower configuration (need_power, produced, required): %s' % sorted(powered))

if args.flags and args.flags.exists():
    print('\nrecorded flags and measured projectile values:')
    for row in csv.DictReader(args.flags.open()):
        print('  %-9s %-42s %s' % (row['mod'], row['key'], row['value']))
