#!/usr/bin/env python3
"""Maps which guest functions build Xenos PM4 type-3 packet headers (0xC0xxxxxx), to find the game's Direct3D
functions without a reference game: Swap writes XE_SWAP (0x64), Draw* write DRAW_INDX/DRAW_INDX_2 (0x22/0x36),
shader loads write IM_LOAD (0x27)/IM_LOAD_IMMEDIATE (0x2B), clears/resolves EVENT_WRITE_EXT (0x5A), ...

A header is built as `lis rD,0xC0xx` followed by `ori/addi rD,rD,imm` (or `li` + `oris`) a few instructions later.

  python3 -I tools/pm4_sites.py [--image out/default_image.bin] [--gen app/generated/default]
"""
import argparse, bisect, collections, json, os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument('--image', default=os.path.join(ROOT, 'out', 'default_image.bin'))
ap.add_argument('--gen', default=os.path.join(ROOT, 'app', 'generated', 'default'))
ap.add_argument('--lo', type=lambda x: int(x, 0), default=0x82120000)
ap.add_argument('--hi', type=lambda x: int(x, 0), default=0x829780C8)
a = ap.parse_args()
img = open(a.image, 'rb').read()
BASE = 0x82000000
starts = sorted(int(k, 16) for k in json.load(open(os.path.join(a.gen, 'codegen.partition.json')))['assignments'])
OPS = {0x10: 'NOP', 0x22: 'DRAW_INDX', 0x36: 'DRAW_INDX_2', 0x23: 'VIZ_QUERY', 0x27: 'IM_LOAD', 0x2B: 'IM_LOAD_IMM',
       0x2D: 'SET_CONSTANT', 0x2F: 'LOAD_ALU_CONSTANT', 0x55: 'SET_CONSTANT2', 0x56: 'SET_SHADER_CONSTANTS',
       0x3B: 'INVALIDATE_STATE', 0x3C: 'WAIT_REG_MEM', 0x21: 'REG_RMW', 0x45: 'COND_WRITE', 0x46: 'EVENT_WRITE',
       0x58: 'EVENT_WRITE_SHD', 0x5A: 'EVENT_WRITE_EXT', 0x64: 'XE_SWAP', 0x40: 'INTERRUPT', 0x5E: 'CONTEXT_UPDATE',
       0x60: 'SET_BIN_MASK_LO', 0x61: 'SET_BIN_MASK_HI', 0x62: 'SET_BIN_SELECT_LO', 0x63: 'SET_BIN_SELECT_HI',
       0x37: 'DRAW_INDX_BIN', 0x34: 'DRAW_INDX_2_BIN', 0x24: 'WAIT_FOR_IDLE', 0x25: 'WAIT_FOR_IDLE'}
sites = collections.defaultdict(lambda: collections.Counter())
for off in range(a.lo - BASE, a.hi - BASE, 4):
    w = struct.unpack_from('>I', img, off)[0]
    # lis rD,0xC0xx : primary 15, rA = 0
    if w >> 26 == 15 and (w >> 16) & 31 == 0 and (w & 0xFFFF) >> 8 == 0xC0:
        rd = (w >> 21) & 31
        hi = w & 0xFFFF
        for k in range(1, 8):
            w2 = struct.unpack_from('>I', img, off + 4 * k)[0]
            op2 = w2 >> 26
            if op2 in (24, 14) and (w2 >> 16) & 31 == rd:      # ori / addi rD? (ra == rd)
                lo = w2 & 0xFFFF
                if op2 == 14 and lo & 0x8000:
                    break
                # header = 0xC0xx0000 | lo ; opcode in bits 15..8 of lo
                opc = (lo >> 8) & 0x7F
                addr = BASE + off
                f = starts[bisect.bisect_right(starts, addr) - 1]
                sites[f][(opc, lo, hi)] += 1
                break
print('functions that build PM4 type-3 headers: %d' % len(sites))
by_op = collections.defaultdict(set)
for f, c in sites.items():
    for (opc, lo, hi) in c:
        by_op[opc].add(f)
for opc in sorted(by_op):
    print('%02X %-18s %3d functions: %s' % (opc, OPS.get(opc, '?'), len(by_op[opc]),
          ' '.join('%08X' % f for f in sorted(by_op[opc])[:14]) + (' ...' if len(by_op[opc]) > 14 else '')))
