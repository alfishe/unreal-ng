#!/usr/bin/env python3
"""Linear Z80 disassembly of a ROM range. Usage: dis.py file offset start count"""
import sys
from z80dis import z80
f, off, start, n = sys.argv[1], int(sys.argv[2],0), int(sys.argv[3],0), int(sys.argv[4])
d = open(f,'rb').read()[off:off+16384]
pc = start
for _ in range(n):
    try:
        ins = z80.decode(d[pc:pc+4], pc); l = ins.len; t = z80.disasm(ins)
    except Exception as e:
        l, t = 1, 'db %02x' % d[pc]
    print('%04X  %-12s %s' % (pc, d[pc:pc+l].hex(), t)); pc += l
