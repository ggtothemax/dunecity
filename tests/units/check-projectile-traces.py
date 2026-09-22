#!/usr/bin/env python3
"""Compare production projectile traces against unmodified Dynasty (normal speed).

Run run-dynasty-projectile-probe.py and run-unit-route-probe.py --projectile-trace
first. Turret/air trajectories intentionally have an additional swept intercept;
projectile-audit.inc checks its positive, near-miss and target-loss cases.
"""
import argparse
import csv
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dynasty', type=Path, required=True)
p.add_argument('--current-dir', type=Path, required=True)
a = p.parse_args()
with a.dynasty.open() as f:
    reference = {tuple(row[:5]): row[5:] for row in list(csv.reader(f))[1:]}
for mod in ('vanilla', 'dunecity', 'Dune2R'):
    with (a.current_dir/(mod+'.csv')).open() as f:
        current = list(csv.reader(f))[1:]
    cases = set()
    matched = 0
    for row in current:
        if row[:2] == ['2', '1']:  # Intentional turret/air physical intercept.
            continue
        key = tuple(row[:5])
        cases.add(key[:4])
        expected = reference.get(key)
        if row[5:] != expected:
            # The current fixture map is 51x31; Dynasty's pool map is 64x64.
            off_map = expected and (int(expected[0]) >= 51*256 or int(expected[1]) >= 31*256)
            assert row[5:] == ['-1']*4 and off_map, (mod, key, row[5:], expected)
        else:
            matched += 1
    assert len(cases) == 180, (mod, 'incomplete scenario coverage', len(cases))
    print(f'{mod}: {matched} exact position/heading/fuse/lifetime samples across 180 cases')
