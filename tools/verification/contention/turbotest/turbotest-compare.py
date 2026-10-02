#!/usr/bin/env python3
"""Read a turbotest memory dump: the counts it measured, the two firmware tables, its verdict.

After turbotest has finished (DONE = 1), dump memory from START (36000, #8CA0) up to PROBEEND (see turbotest.sym)
as a raw binary file, then:

    python3 turbotest-compare.py dump.bin [turbotest.sym]

A count matches within one body (the loop's start moves by up to 3 T with the code before its HALT).
It prints every body's count at 3.5 MHz and in turbo next to unreal-ng's: the Scorpion's SC15.1 and SC15.3
firmwares, or the ZX-Evo, and the difference in percent from the closest. Exit code: 0 the counts match a table,
1 they match none or turbo makes no difference, 3 neither a Scorpion nor a ZX-Evo (turbo not tried), 2 the dump
is unusable.
"""
import os
import re
import sys

BODIES = ['NOP', 'LD A,(RAM)', 'LD A,(ROM)', 'LD (RAM),A', 'OUT (FE),A']


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dump = open(sys.argv[1], 'rb').read()
    symfile = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                 'turbotest.sym')
    sym = {}
    for line in open(symfile):
        m = re.match(r'(\w+) equ #([0-9A-F]+)', line)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)
    base = sym['START']
    if len(dump) < sym['PROBEEND'] - base:
        print(f'the dump is {len(dump)} bytes, turbotest needs {sym["PROBEEND"] - base}')
        return 2

    def peek(a):
        return dump[a - base]

    def table(name):
        at = sym[name]
        return [peek(at + 3 * i) | peek(at + 3 * i + 1) << 8 | peek(at + 3 * i + 2) << 16 for i in range(10)]

    if peek(sym['DONE']) != 1:
        print('turbotest did not finish (DONE is not 1)')
        return 2
    got = table('COUNTS')
    evo = peek(sym['EVO']) == 1
    tables = [('ZX-Evo', table('EXPEVO'))] if evo else [('SC15.1', table('EXP151')), ('SC15.3', table('EXP153'))]
    width = 7 + 8 * len(tables)
    print(f'{"":10}  {"3.5 MHz":>{width}}  {"14 MHz" if evo else "turbo":>{width}}')
    print(f'{"":10}  ' + '  '.join([f'{"got":>7}' + ''.join(f'{n:>8}' for n, _ in tables)] * 2))
    for b, name in enumerate(BODIES):
        cells = []
        for k in (2 * b, 2 * b + 1):
            cells.append(f'{got[k]:7}' + ''.join(f'{t[k]:8}' for _, t in tables))
        print(f'{name:10}  ' + '  '.join(cells))

    match = peek(sym['MATCH'])
    fails = peek(sym['FAILS']) | peek(sym['FAILS'] + 1) << 8
    if match == 0xFE:
        print('neither a Scorpion nor a ZX-Evo: turbo not tried')
        return 3
    if match == 0xFF:
        print('turbo makes no difference: the NOP body ran as often in turbo as at 3.5 MHz')
        return 1
    if match in (1, 2, 3):
        print('all counts as unreal-ng\'s ZX-Evo' if match == 3 else
              f'all counts as the SC15.{1 if match == 1 else 3} firmware gives')
        return 0
    name, closer = min(tables, key=lambda nt: sum(abs(g - e) for g, e in zip(got, nt[1])))
    worst = max(range(10), key=lambda k: abs(got[k] - closer[k]) / max(closer[k], 1))
    pct = 100.0 * (got[worst] - closer[worst]) / max(closer[worst], 1)
    print(f'{"differs from the ZX-Evo model" if evo else "matches neither firmware"}: {fails} counts differ from '
          f'{name}, the most {BODIES[worst // 2]} {"turbo" if worst & 1 else "3.5 MHz"} {pct:+.1f} %')
    return 1


if __name__ == '__main__':
    sys.exit(main())
