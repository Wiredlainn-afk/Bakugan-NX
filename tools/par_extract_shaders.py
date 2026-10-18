#!/usr/bin/env python3
"""Extracts the Xenos shader containers from the game's media/shaders/*.par files (and any other file) into a folder
as vs_<xxh3>.bin / ps_<xxh3>.bin, the format the renderer's own dumps use (virtual part + microcode).

A container starts with 0x102A1100 (pixel) or 0x102A1101 (vertex), then [1] virtual size, [2] physical size; its total
size is [1] + [2]. Found by scanning 4-aligned words and validating the sizes.

  python3 -I tools/par_extract_shaders.py <out folder> <file> [<file> ...]
Needs the xxhash module (the venv in C:akugan-tools\venv has it).
"""
import os, struct, sys
import xxhash

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
seen = {}
for path in sys.argv[2:]:
    d = open(path, 'rb').read()
    n = 0
    o = 0
    while o + 12 <= len(d):
        w0 = struct.unpack_from('>I', d, o)[0]
        if w0 & 0xFFFFFFFE == 0x102A1100:
            vs, ps = struct.unpack_from('>II', d, o + 4)
            if 28 <= vs <= 0x40000 and 0 < ps <= 0x40000 and o + vs + ps <= len(d):
                blob = d[o:o + vs + ps]
                h = xxhash.xxh3_64_intdigest(blob)
                name = '%s_%016x.bin' % ('vs' if w0 & 1 else 'ps', h)
                if name not in seen:
                    open(os.path.join(out, name), 'wb').write(blob)
                    seen[name] = path
                n += 1
                o += (vs + ps + 3) & ~3
                continue
        o += 4
    print('%s: %d containers' % (os.path.basename(path), n))
print('%d distinct (%d vs, %d ps) -> %s' % (len(seen), sum(1 for k in seen if k.startswith('vs')),
                                          sum(1 for k in seen if k.startswith('ps')), out))
