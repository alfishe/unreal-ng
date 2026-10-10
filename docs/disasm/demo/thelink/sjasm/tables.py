#!/usr/bin/env python3
"""The Link's tables, computed the way the author's BASIC programs computed them (../basic/*.bas).

    python3 tables.py <out folder>     writes sin+, sectg, sin32768, uv256d

Byte for byte the files on the author's disk. Sinclair BASIC rounds where it stores: POKE and
RANDOMIZE take the nearest integer of the magnitude and keep the sign, INT(x+.5) rounds up at .5.
"""

import math
import os
import sys


def poke(value):
    """POKE a,v: the nearest integer (magnitude rounded, sign kept), a negative one stored as 256+v"""
    r = int(abs(value) + 0.5)
    return (-r if value < 0 else r) & 0xFF


def sin_plus():
    """sin+.bas: 127*SIN of the middle of each of 256 steps (no zero crossing lands on a step)"""
    return bytes(poke(127 * math.sin((n + .5) / 256 * 2 * math.pi)) for n in range(256))


def sectg():
    """sectg.bas: 128/COS a and 256*TAN a for a in 0..PI/4 (128 = PI/4), INT(x+.5) each"""
    sec = [math.floor(128 / math.cos(n / 512 * math.pi) + .5) & 0xFF for n in range(128)]
    tg = [math.floor(256 * math.tan(n / 512 * math.pi) + .5) & 0xFF for n in range(128)]
    return bytes(sec + tg)


def sin32768():
    """sin32768.bas: 32768*SIN as words, low bytes then high bytes (RANDOMIZE s; PEEK 23670/23671).
    The disk has #7FFF at 90 degrees where the formula gives 32768, which a signed word would read
    as -32768: the table is the formula with that one entry held at the signed maximum."""
    words = []
    for n in range(256):
        s = 32768 * math.sin(n / 256 * 2 * math.pi)
        word = int(abs(s) + 0.5) * (1 if s >= 0 else -1)
        words.append(min(word, 0x7FFF) & 0xFFFF)
    return bytes([w & 0xFF for w in words] + [w >> 8 for w in words])


def uv256d():
    """uv256d.bas: the u,v texture offsets of a 64x32 quarter of the screen, y and x from the far
    corner inwards (63.5 .. 0.5)"""
    out = bytearray()
    for yi in range(64):
        y = 63.5 - yi
        for xi in range(32):
            x = 31.5 - xi
            v = -((y / 64) * 64 * (160 - (x * x) / 64) / 8192) * 64
            u = -((x / 64) * 64 * (160 - (y * y) / 64) / 8192) * 64
            out += bytes([poke(u), poke(v)])
    return bytes(out)


TABLES = {"sin+": sin_plus, "sectg": sectg, "sin32768": sin32768, "uv256d": uv256d}


def write_all(folder):
    for name, make in TABLES.items():
        with open(os.path.join(folder, name), "wb") as f:
            f.write(make())


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    write_all(sys.argv[1])
