#!/usr/bin/env python3
"""Audits the code generator's function boundaries against the image's .pdata (exact length of every non-leaf function).

  python3 -I tools/pdata_audit.py [--image out/default_image.bin] [--gen app/generated/default] [--toml out/pdata_fix.toml]

Reports, for the generated functions:
  missing   a .pdata function whose start is not a generated function start (a gap the generator never claimed)
  longer    generated function larger than its .pdata length (it runs past its end: swallows what follows)
  shorter   generated function smaller than its .pdata length (the rest is a gap or other functions)
and writes `size = N` candidates for the `longer` class (evidence: .pdata length) to --toml.
"""
import argparse, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdata_table, find_gaps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument('--image', default=os.path.join(ROOT, 'out', 'default_image.bin'))
ap.add_argument('--gen', default=os.path.join(ROOT, 'app', 'generated', 'default'))
ap.add_argument('--toml', default=os.path.join(ROOT, 'out', 'pdata_fix.toml'))
a = ap.parse_args()

_, ents = pdata_table.load(a.image)
starts = sorted(int(k, 16) for k in json.load(open(os.path.join(a.gen, 'codegen.partition.json')))['assignments'])
sset = set(starts)
sizes = find_gaps.measure_functions(a.gen)
missing, longer, shorter = [], [], []
for b, ln, _ in ents:
    if b not in sset:
        missing.append((b, ln)); continue
    g = sizes.get(b)
    if g is None:
        continue
    g *= 4
    if g > ln:
        longer.append((b, ln, g))
    elif g < ln:
        shorter.append((b, ln, g))
print('pdata entries %d, generated functions %d' % (len(ents), len(starts)))
print('missing (pdata function not a generated function): %d' % len(missing))
for b, ln in missing[:40]:
    print('   0x%08X length 0x%X' % (b, ln))
print('longer than pdata: %d' % len(longer))
for b, ln, g in longer[:40]:
    print('   0x%08X pdata 0x%X generated 0x%X (+0x%X)' % (b, ln, g, g - ln))
print('shorter than pdata: %d' % len(shorter))
for b, ln, g in shorter[:40]:
    print('   0x%08X pdata 0x%X generated 0x%X (-0x%X)' % (b, ln, g, ln - g))
with open(a.toml, 'w') as f:
    f.write('# candidates from tools/pdata_audit.py (evidence: .pdata length). Review before use.\n')
    for b, ln, g in longer:
        f.write('"0x%08X" = { size = 0x%X }  # .pdata length 0x%X; generated 0x%X\n' % (b, ln, ln, g))
    for b, ln in missing:
        f.write('"0x%08X" = { size = 0x%X }  # .pdata entry, not a generated function\n' % (b, ln))
