#!/usr/bin/env python3
"""Read a turbotest memory dump: the counts it measured, the two firmware tables, its verdict.

After turbotest has finished (DONE = 1), dump memory from START (36000, #8CA0) up to PROBEEND (see turbotest.sym)
as a raw binary file, then:

    python3 turbotest-compare.py dump.bin [turbotest.sym]

It prints every body's count at 3.5 MHz and in turbo next to what the SC15.1 and SC15.3 firmwares give in
unreal-ng's model, and the difference in percent from the closer one. Exit code: 0 the counts match a firmware's
table, 1 they match neither or turbo makes no difference, 3 the machine is not a Scorpion (turbo not tried),
2 the dump is unusable.
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
    got, sc151, sc153 = table('COUNTS'), table('EXP151'), table('EXP153')
    print(f'{"":10}  {"3.5 MHz":>23}  {"turbo":>23}')
    print(f'{"":10}  {"got":>7}{"SC15.1":>8}{"SC15.3":>8}  {"got":>7}{"SC15.1":>8}{"SC15.3":>8}')
    for b, name in enumerate(BODIES):
        cells = []
        for k in (2 * b, 2 * b + 1):
            cells.append(f'{got[k]:7}{sc151[k]:8}{sc153[k]:8}')
        print(f'{name:10}  ' + '  '.join(cells))

    match = peek(sym['MATCH'])
    fails = peek(sym['FAILS']) | peek(sym['FAILS'] + 1) << 8
    if match == 0xFE:
        print('not a Scorpion: the first NOP count is not a Scorpion\'s, turbo not tried')
        return 3
    if match == 0xFF:
        print('turbo makes no difference: the NOP body ran as often in turbo as at 3.5 MHz')
        return 1
    if match in (1, 2):
        print(f'all counts as the SC15.{1 if match == 1 else 3} firmware gives')
        return 0
    closer = sc151 if sum(abs(g - e) for g, e in zip(got, sc151)) <= sum(abs(g - e) for g, e in zip(got, sc153)) \
        else sc153
    worst = max(range(10), key=lambda k: abs(got[k] - closer[k]) / max(closer[k], 1))
    pct = 100.0 * (got[worst] - closer[worst]) / max(closer[worst], 1)
    print(f'matches neither firmware: {fails} counts differ from SC15.{1 if closer is sc151 else 3}, the most '
          f'{BODIES[worst // 2]} {"turbo" if worst & 1 else "3.5 MHz"} {pct:+.1f} %')
    return 1


if __name__ == '__main__':
    sys.exit(main())
