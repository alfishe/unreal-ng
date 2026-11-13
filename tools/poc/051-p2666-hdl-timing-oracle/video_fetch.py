#!/usr/bin/env python3
"""POC 051: where the Pentagon 2.666 LE reads each ZX screen byte, in the emulator's frame.

Reads the video oracle's trace (tb/p2666_video_tb.vhd: RD <edge> <SRAM address>, INT <edge>) and prints, for the ZX
mode (screen in RAM page 5, SRAM bytes #14000-#17FFF), the emulator line and T (base clock, 32 mainclk edges) at
which the byte of ZX line Y, column c goes to the SRAM, against the renderer's latch at line 80 + Y, T 30 + 4c.

usage: video_fetch.py <trace> [frame origin shift in edges]
"""
import sys
from collections import Counter

FRAME = 2293760
trace = sys.argv[1]
shift = int(sys.argv[2]) if len(sys.argv) > 2 else 114176


def emu(edge):
    t = ((edge + shift) % FRAME) / 32
    return int(t // 224), t % 224


ints, deltas = [], {"pixel": Counter(), "attribute": Counter()}
border = {}  # FE index -> the first (pixel_c, line_c) it shows at in the border
for line in open(trace):
    p = line.split()
    if p[0] == 'INT':
        ints.append(int(p[1]))
        continue
    if p[0] == 'PAL':
        px, ln, idx = int(p[2]), int(p[3]), int(p[4])
        if idx in (2, 3, 5) and idx not in border and (px < 193 or px > 704 or ln < 130 or ln > 513):
            border[idx] = (px, ln)
        continue
    edge, addr = int(p[1]), int(p[2], 16)
    if edge >= FRAME or not 0x14000 <= addr < 0x14000 + 6912:
        continue
    off = addr - 0x14000
    if off < 0x1800:
        kind, y = 'pixel', ((off >> 11) << 6) | (((off >> 5) & 7) << 3) | ((off >> 8) & 7)
    else:
        kind, y = 'attribute', None
    c = off & 31
    line_no, t = emu(edge)
    if kind == 'pixel':
        deltas[kind][(line_no - 80 - y, t - (30 + 4 * c))] += 1
    else:
        row = (off - 0x1800) >> 5
        deltas[kind][((line_no - 80) // 8 - row, t - (30 + 4 * c))] += 1

print('INT strobe edges:', ints, '-> emulator (line, T)', [emu(e) for e in ints])
# zx_main holds a cell's address on the bus for its whole 4 T, so the same byte is read again and again; the reads it
# keeps (zx_main.vhd:549-555, the counter one step behind pixel_c) are the ones 0.25 - 0.375 T after the cell's latch
for kind in deltas:
    near = {k: v for k, v in deltas[kind].items() if k[0] == 0 and 0 <= k[1] < 1}
    print('%s: reads in the latch T (line - latch line, T - latch T): %s' % (kind, near))

# the video.sh border writes: index -> (emulator line, T). A ZX line L is shown on VGA lines 2 L - 30 / - 29, ZX pixel x
# of the 388 window at VGA pixel 2 x + 73 (the paper's first pixel x = 60 at VGA 193)
writes = {2: (100, 10), 5: (100, 180), 3: (60, 100)}
for idx, (wl, wt) in writes.items():
    if idx in border:
        px, ln = border[idx]
        print('border write line %d T %d: shown on VGA line %d (line %d), x %.1f = T %.2f + %.2f' %
              (wl, wt, ln, (ln + 30) // 2, (px - 73) / 2, wt, (px - 73) / 4 - wt))
