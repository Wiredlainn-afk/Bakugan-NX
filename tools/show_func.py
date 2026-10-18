#!/usr/bin/env python3
"""Prints the instruction comments (and the calls) of generated functions.  python3 -I tools/show_func.py 821A7500 ..."""
import glob, os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
want = {a.upper() for a in sys.argv[1:]}
for p in sorted(glob.glob(os.path.join(ROOT, 'app', 'generated', 'default', 'bakugan_recomp.*.cpp'))):
    cur = None
    for line in open(p, errors='replace'):
        m = re.match(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', line)
        if m:
            cur = m.group(1) if m.group(1) in want else None
            if cur: print('=== sub_' + cur)
            continue
        if cur is None:
            continue
        if line.startswith('}'):
            cur = None
        elif line.startswith('\t// ') or line.startswith('loc_'):
            print('  ' + line.strip())
