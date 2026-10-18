#!/usr/bin/env python3
"""Reads the .pdata (RUNTIME_FUNCTION table) of the decrypted image: Xbox 360 images list every non-leaf function with
its exact length. Authoritative evidence for function boundaries; also an audit of the generator's function list.

Entry (8 bytes, big endian): BeginAddress; then PrologLength:8, FunctionLength:22 (in instructions), 32bit:1, Ex:1.

  python3 -I tools/pdata_table.py out/default_image.bin                  summary
  python3 -I tools/pdata_table.py out/default_image.bin 0x82937508       the entry covering an address
"""
import struct, sys

def load(path, base=0x82000000):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    off = pe + 24 + opt
    for i in range(nsec):
        n, vs, va, rs, rp = struct.unpack_from('<8sIIII', d, off + 40 * i)
        if n.rstrip(b'\0') == b'.pdata':
            ents = []
            for o in range(va, va + vs - 7, 8):
                b, w = struct.unpack_from('>II', d, o)
                if b == 0:
                    break
                ents.append((b, ((w >> 8) & 0x3FFFFF) * 4, w & 0xFF))
            return d, ents
    raise SystemExit('no .pdata')

if __name__ == '__main__':
    d, ents = load(sys.argv[1])
    if len(sys.argv) > 2:
        a = int(sys.argv[2], 16)
        for b, ln, pro in ents:
            if b <= a < b + ln:
                print('0x%08X length 0x%X (%d bytes) prolog %d bytes -> ends 0x%08X' % (b, ln, ln, pro, b + ln))
                break
        else:
            print('no entry covers 0x%08X' % a)
    else:
        print(len(ents), 'entries, covering', sum(l for _, l, _ in ents), 'bytes')
