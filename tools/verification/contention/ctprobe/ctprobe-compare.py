#!/usr/bin/env python3
"""Compare a ctprobe memory dump with the expected values, check by check.

After ctprobe has finished (DONE = 1), dump memory from START (36000, #8CA0) up to PROBEEND (see ctprobe.sym) as a raw
binary file, then:

    python3 ctprobe-compare.py dump.bin [ctprobe.sym]

For every check that differs it prints the whole row of measured and expected values, and whether the row
matches the expected one shifted by a few ticks ("everything happens N ticks later / earlier"). The expected
values are the ones built into the dump for the machine type the probe detected.
"""
import os
import re
import sys

RECORD = 19


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dump = open(sys.argv[1], 'rb').read()
    symfile = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ctprobe.sym')
    sym = {}
    for line in open(symfile):
        m = re.match(r'(\w+) equ #([0-9A-F]+)', line)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)

    BASE = sym['START']

    def peek(a):
        return dump[a - BASE]

    def word(a):
        return peek(a) | peek(a + 1) << 8

    if len(dump) < sym['PROBEEND'] - BASE:
        print(f'the dump is {len(dump)} bytes; it must cover #{BASE:04X}..#{sym["PROBEEND"] - 1:04X}')
        return 2
    if peek(sym['DONE']) != 1:
        print('DONE is not 1: the probe had not finished when the dump was taken')
        return 2
    if 'TOOFAST' in sym and peek(sym['TOOFAST']):
        print('The CPU ran faster than 3.5 MHz, so nothing was measured: switch the machine to 3.5 MHz')
        return 2

    names = ['ULA 48K', 'ULA 128K', 'gate array', 'no contention', 'no contention, attr bus',
             'no contention, attr bus, Even M1', 'no contention, Even M1']
    cls = peek(sym['CLASS'])
    caps = peek(sym['CAPS'])
    onset = word(sym['ONSET'])
    size = sym['RESULTSEND'] - sym['RESULTS']
    print(f'Machine: {names[cls] if cls < len(names) else cls}, onset {onset}, '
          f'paging {"yes" if caps & 1 else "no"}, +3 layouts {"yes" if caps & 2 else "no"}')

    at = sym['CASES']
    wrong = 0
    bad_checks = 0
    while peek(at):
        flags = peek(at + 1)
        offset = word(at + 9)
        offset = offset - 65536 if offset > 32767 else offset
        count = peek(at + 11)
        results = word(at + 12)
        name = bytes(dump[at + 14 - BASE:at + 19 - BASE]).decode().strip()
        at += RECORD
        if flags & 3 & ~caps or (flags & 32 and cls in (3, 6)):
            continue  # skipped as N/A on this machine
        got = list(dump[results - BASE:results - BASE + count])
        exp_at = sym['EXPECTED'] + cls * size + (results - sym['RESULTS']) - BASE
        exp = list(dump[exp_at:exp_at + count])
        if got == exp:
            continue
        bad_checks += 1
        wrong += sum(g != e for g, e in zip(got, exp))
        first = onset + offset
        print(f'\n{name}: from T{first}, one value per tick')
        print(f'  got {" ".join(f"{v:3}" for v in got)}')
        print(f'  exp {" ".join(f"{v:3}" for v in exp)}')
        shifts = []
        for k in [k for k in range(-8, 9) if k != 0]:  # up to one 8-tick group either way
            pairs = [(got[i], exp[i + k]) for i in range(count) if 0 <= i + k < count]
            if len(pairs) * 2 >= count and all(g == e for g, e in pairs):
                shifts.append(k)
        if shifts:
            k = min(shifts, key=lambda k: (abs(k), k > 0))  # the smallest; "later" on a tie (a repeating row)
            when = 'later' if k < 0 else 'earlier'
            print(f'  = the expected row shifted: this machine behaves {abs(k)} tick(s) {when} than a real one')
        else:
            print('  (no simple shift explains it)')

    print(f'\n{wrong} values wrong in {bad_checks} checks' if wrong else '\nALL VALUES AS EXPECTED')
    return 1 if wrong else 0


if __name__ == '__main__':
    sys.exit(main())
