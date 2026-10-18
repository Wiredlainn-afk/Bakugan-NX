#!/usr/bin/env python3
"""Prints the optional headers of an XEX2 (no decryption): execution info, original PE name, import libraries, file
format info (compression block layout), resource info. Reads only the header, so it works on the encrypted file.

Usage: python3 -I tools/xex_header.py path/to/default.xex
"""
import struct, sys

NAMES = {0x2FF: "resource info", 0x3FF: "file format info", 0x405: "delta patch descriptor", 0x80: "base file info",
         0x103: "bounding path?", 0x183FF: "original PE name", 0x10001: "static libraries", 0x10100: "entry point",
         0x10201: "image base", 0x103FF: "import libraries", 0x18002: "tls info", 0x20104: "title workspace size?",
         0x20200: "default stack size", 0x20301: "default filesystem cache size", 0x20401: "default heap size",
         0x30000: "page heap size and flags", 0x40006: "execution info", 0x40201: "game ratings",
         0x40310: "lan key", 0x406: "?", 0x18102: "system flags", 0x10004: "?", 0x10005: "export table?",
         0x10104: "?", 0x20104: "?", 0x1: "?"}

def main(path):
    d = open(path, "rb").read(0x20000)
    magic, flags, pe_off, _, sec_off, count = struct.unpack(">4sIIIII", d[:24])
    assert magic == b"XEX2", magic
    print("module flags 0x%X (title=%d dll=%d patch=%d), pe offset 0x%X, security 0x%X, %d optional headers"
          % (flags, flags & 1 != 0, flags & 4 != 0, flags & 0x10 != 0, pe_off, sec_off, count))
    for i in range(count):
        key, val = struct.unpack_from(">II", d, 0x18 + i * 8)
        low = key & 0xFF
        kind = "value" if low in (0x00, 0x01) else ("ptr->data" if low == 0xFF else "ptr->%d dwords" % low)
        print("  %08X %-28s %08X  (%s)" % (key, NAMES.get(key, ""), val, kind))
        if key == 0x40006:
            media, ver, bver, title, plat, exe, disc, discs, sav = struct.unpack_from(">IIII BBBB I", d, val)
            print("      media id %08X, version %08X, base version %08X, title id %08X (%s%04d), disc %d/%d, savegame id %08X"
                  % (media, ver, bver, title, chr(title >> 24) + chr((title >> 16) & 0xFF), title & 0xFFFF, disc, discs, sav))
        elif key == 0x183FF:
            size = struct.unpack_from(">I", d, val)[0]
            print("      original PE name:", d[val + 4:val + 4 + size].split(b"\0")[0].decode("latin-1"))
        elif key == 0x103FF:
            size, table_size, n = struct.unpack_from(">III", d, val)
            p = val + 12
            names = d[p:p + table_size]
            libs = [x.decode() for x in names.split(b"\0") if x]
            print("      %d import libraries: %s" % (n, ", ".join(libs)))
        elif key == 0x3FF:
            size, enc, comp = struct.unpack_from(">IHH", d, val)
            print("      format info: size 0x%X encryption %d compression %d" % (size, enc, comp))
            if comp == 1:
                blocks = (size - 8) // 8
                tot = 0
                for b in range(min(blocks, 6)):
                    data_sz, zero_sz = struct.unpack_from(">II", d, val + 8 + b * 8)
                    print("        block %d: data 0x%X, zero 0x%X" % (b, data_sz, zero_sz))
                print("        %d blocks" % blocks)
        elif key == 0x20200:
            print("      default stack size: %d KiB" % (val // 1024))
    # security info
    sec = d[sec_off:sec_off + 0x200]
    img_size = struct.unpack_from(">I", sec, 4)[0]
    load = struct.unpack_from(">I", sec, 0x110)[0]
    region, = struct.unpack_from(">I", sec, 0x178)
    print("image size 0x%X, load address 0x%08X, region flags 0x%08X, allowed media 0x%08X"
          % (img_size, load, region, struct.unpack_from(">I", sec, 0x17C)[0]))

main(sys.argv[1])
