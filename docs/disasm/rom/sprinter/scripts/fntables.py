"""Read the BIOS function dispatch out of the ROM and name the handlers.

Two dispatch forms exist in BIOS 3.04:
  * a word table (page 8 #3000: functions #80-#FF, entry = 2 * (C - #80));
  * a "DEC A / JP Z,handler" chain (page 8 #057B: #40-#4F; page 0 #0431:
    #50-#5F; page 0 #010F: the old #40-#47 copy).
Each handler gets the name "Fn<number>_<NAME>" with NAME from biosfn.py.
"""
import os

import biosfn

HERE = os.path.dirname(os.path.abspath(__file__))


# Numbers whose name is a local label in BIOS_equ.inc (RST_CONF.*) or that the
# current BIOS no longer has (the old numbers of moved functions)
EXTRA = {0xEE: 'RST_CONF_AY8910', 0xF0: 'RST_CONF_SP97_1', 0xF1: 'RST_CONF_SP97_2',
         0xF3: 'RST_CONF_CUSTOM', 0x90: 'GetMemSize_old', 0x91: 'InitMem_old'}


def carried(target):
    """addr -> first carried (non-local) name from labels-<target>.json."""
    import json
    path = os.path.join(HERE, f'labels-{target}.json')
    out = {}
    if os.path.exists(path):
        for row in json.load(open(path))['labels']:
            if '.' not in row[1] and row[0] not in out:
                out[row[0]] = row[1]
    return out


def _name(num, fallback=None):
    name = biosfn.NAMES.get(num) or EXTRA.get(num) or fallback or 'Unnamed'
    return f'Fn{num:02X}_{name}'


def wordTable(binName, org, start, firstNumber, count, skip=()):
    d = open(os.path.join(HERE, binName), 'rb').read()
    out = []
    for i in range(count):
        p = start - org + 2 * i
        target = d[p] | (d[p + 1] << 8)
        if target not in skip:
            out.append((target, firstNumber + i))
    return out


def chain(binName, org, start, firstNumber):
    """Follow LD A,C / AND A / INC C / DEC C / DEC A / JP Z,nn from start."""
    d = open(os.path.join(HERE, binName), 'rb').read()
    pos, n, out = start - org, 0, []
    while pos < len(d):
        b = d[pos]
        if b == 0xCA:                                   # JP Z,nn
            out.append((d[pos + 1] | (d[pos + 2] << 8), firstNumber + n))
            pos += 3
        elif b in (0x3D, 0x0D):                         # DEC A / DEC C
            n += 1
            pos += 1
        elif b in (0x79, 0xA7, 0x0C):                   # LD A,C / AND A / INC C
            pos += 1
        else:
            break
    return out


def symbols(pairs, exclude=(), fallback=None):
    """[(addr, number)] -> [(addr, name)], first number wins per address;
    fallback: addr -> name for numbers BIOS_equ.inc does not name."""
    seen, out = set(), []
    for addr, num in pairs:
        if addr in seen or addr in exclude:
            continue
        seen.add(addr)
        out.append((addr, _name(num, (fallback or {}).get(addr))))
    return out


def numbersAt(pairs):
    """addr -> sorted function numbers dispatched there."""
    m = {}
    for addr, num in pairs:
        m.setdefault(addr, []).append(num)
    return m
