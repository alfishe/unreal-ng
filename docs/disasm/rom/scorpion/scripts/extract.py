#!/usr/bin/env python3
"""Extract the service-monitor pages from the Scorpion ROM bundles.

Each bundle carries the 16 KiB monitor at file offset $8000-$BFFF
("p2" in README terms).  This script slices it into mon_<name>_p2.bin
next to this script, ready for analyze.py / gen.py / checkcov.py.

Run from anywhere:  python3 extract.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..', '..'))

SOURCES = [
    ('data/rom/scorpion.rom', 'mon_scorpion_p2.bin'),
    ('data/rom/scorp295.rom', 'mon_scorp295_p2.bin'),
    ('data/rom/scorp_prof401.rom', 'mon_prof401_p2.bin'),
]

PAGE, SIZE = 0x8000, 0x4000

for rom, out in SOURCES:
    data = open(os.path.join(ROOT, rom), 'rb').read()
    page = data[PAGE:PAGE + SIZE]
    assert len(page) == SIZE, '%s: short bundle (%d bytes)' % (rom, len(data))
    dst = os.path.join(HERE, out)
    with open(dst, 'wb') as f:
        f.write(page)
    print('%s: %s [%d bytes]' % (out, rom, SIZE))
