#!/usr/bin/env python3
"""Audit original Dynasty projectiles using a previously built route reference.

First run compare-dynasty-routes.py; pass its output as --reference-build-dir.
The original source object files, SDK shim and extracted UNIT.EMC are reused.
"""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dynasty-dir', type=Path, required=True)
p.add_argument('--reference-build-dir', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
dd = a.dynasty_dir.resolve()
r = a.reference_build_dir.resolve()
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', str(dd), 'rev-parse', 'HEAD'], text=True).strip() == '4469449c75f51388ad2725297a95f09a6c601905'
assert not subprocess.check_output(['git', '-C', str(dd), 'status', '--porcelain', '--', 'src', 'include'], text=True).strip()
flags = ['-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-D__DARWIN_LDBL_COMPAT(x)=',
         '-I'+str(dd/'include'), '-I'+str(r/'include'), '-iquote', str(dd/'src')]
obj = out/'projectiles.o'
binary = out/'dynasty-projectiles'
objects = sorted(str(o) for o in (r/'objects').glob('*.o') if o.name != 'dynasty-route-harness.c.o')
assert objects and (r/'UNIT.EMC').is_file(), 'Build the route reference first'
with (out/'build.log').open('w') as log:
    subprocess.run(['cc', *flags, '-c', str(root/'tests/units/dynasty-projectile-probe.c'), '-o', str(obj)],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['cc', '-fsanitize=address,undefined', '-Wl,-dead_strip', '-o', str(binary),
                    str(obj), *objects, '-lm'], stdout=log, stderr=subprocess.STDOUT, check=True)
with (out/'projectiles.csv').open('w') as csvout, (out/'run.log').open('w') as log:
    subprocess.run([str(binary), str(r/'UNIT.EMC')], stdout=csvout, stderr=log, check=True, timeout=120,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
print('Original Dynasty projectile audit passed ASan/UBSan:', out/'projectiles.csv')
