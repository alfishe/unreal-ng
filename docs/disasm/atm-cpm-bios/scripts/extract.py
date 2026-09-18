#!/usr/bin/env python3
"""Extract the CP/M system page from the ATM710 ROM bundle.

atm2.rom is a 64 KiB bundle of four 16 KiB pages.  core/src/emulator/
memory/rom.cpp maps the MM_ATM710 slots as sos=p0 / dos=p1 / 128=p2 /
sys=p3, and Memory::SetROMSystem() maps base_sys_rom (p3) to $0000-$3FFF
whenever the machine enters a service/CP/M session (#7FFD bit 4).

This script slices page 3 into atm_sys_p3.bin next to this script, ready
for analyze.py / gen.py / checkcov.py.

Run from anywhere:  python3 extract.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))

ROM = 'data/rom/atm2.rom'
OUT = 'atm_sys_p3.bin'
PAGE, SIZE = 0xC000, 0x4000

data = open(os.path.join(ROOT, ROM), 'rb').read()
page = data[PAGE:PAGE + SIZE]
assert len(page) == SIZE, '%s: short bundle (%d bytes)' % (ROM, len(data))
dst = os.path.join(HERE, OUT)
with open(dst, 'wb') as f:
    f.write(page)
print('%s: %s page 3 [$%05X-$%05X] [%d bytes]' % (OUT, ROM, PAGE, PAGE + SIZE - 1, SIZE))
