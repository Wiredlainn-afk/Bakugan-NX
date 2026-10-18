#!/usr/bin/env python3
"""Audits the [functions] entries of app/overrides.toml: a real function start follows a terminator (blr, b, bctr,
an unconditional trap, a data word, or the end of the previous .pdata function). An entry whose previous instruction
falls through into it is the middle of a block, and declaring it a function splits its parent (the parent's own
branches to the rest then become "Unresolved conditional branch").

  python3 -I tools/entry_audit.py [--image out/default_image.bin] [--toml app/overrides.toml]
"""
import argparse, bisect, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdata_table
from ppc_mini import Image, decode

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument('--image', default=os.path.join(ROOT, 'out', 'default_image.bin'))
ap.add_argument('--toml', default=os.path.join(ROOT, 'app', 'overrides.toml'))
a = ap.parse_args()
img = Image(a.image, 0x82000000)
_, ents = pdata_table.load(a.image)
pend = {b + l for b, l, _ in ents}
text = open(a.toml, encoding='utf-8').read()
end = text.find('[[switch_tables]]')
body = text[text.index('[functions]'):end if end > 0 else len(text)]
bad = []
n = 0
for m in re.finditer(r'^"0x([0-9A-Fa-f]{8})"\s*=\s*\{([^}]*)\}', body, re.M):
    addr = int(m.group(1), 16)
    n += 1
    if addr in pend:
        continue
    w = img.word(addr - 4)
    i = decode(w, addr - 4)
    op = getattr(i, 'op', '')
    # terminators: blr, bctr, b (not bl), unconditional trap, and non-instruction words (data / padding)
    if w == 0x4E800020 or w == 0x4E800420 or (w >> 26 == 18 and not (w & 1)) or w == 0x0FE00016 or w == 0:
        continue
    if op in ('other',) and (w >> 26) not in (4, 6, 31, 59, 63):
        continue
    bad.append((addr, w, op))
print('%d entries, %d follow a non-terminator:' % (n, len(bad)))
for addr, w, op in bad:
    print('  0x%08X  previous word %08X (%s)' % (addr, w, op))
