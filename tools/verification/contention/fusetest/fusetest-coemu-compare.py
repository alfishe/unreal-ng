#!/usr/bin/env python3
"""Read fusetest's report out of a fusetest-coemu memory dump.

fusetest-coemu (fusetest wrapped for the coemu harness) keeps a copy of everything fusetest prints in BUFFER.
After it has finished (DONE = 1), dump memory from START (#9000) up to PROBEEND (#A000) as a raw binary file, then:

    python3 fusetest-coemu-compare.py dump.bin [fusetest-coemu.sym]

It prints the report and every test's verdict. fusetest itself knows what each machine should print: a test
that does not apply to the machine it found prints "skipped". So the run is right when every line is "passed"
or "skipped". Exit code: 0 right, 1 a line "failed" or "incomplete", 3 fusetest cannot test this machine (it
takes a Pentagon for a TS2068, or cannot measure the frame, see README.md), 2 the dump is unusable.
"""
import os
import re
import sys

TESTS = ['BIT n,(IX+d)', 'DAA', 'OUTI', 'LDIR', 'Contended IN', 'Floating bus', 'Contended memory',
         'High port contention 1', 'High port contention 2', '0xbffd read', '0x3ffd read', '0x7ffd read']


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dump = open(sys.argv[1], 'rb').read()
    symfile = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                 'fusetest-coemu.sym')
    sym = {}
    for line in open(symfile):
        m = re.match(r'(\w+) equ #([0-9A-F]+)', line)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)
    base = sym['START']
    if len(dump) < sym['PROBEEND'] - base:
        print(f'the dump is {len(dump)} bytes, fusetest-coemu needs {sym["PROBEEND"] - base}')
        return 2
    if dump[sym['DONE'] - base] != 1:
        print('fusetest did not finish (DONE is not 1)')
        return 2

    raw = dump[sym['BUFFER'] - base:sym['PROBEEND'] - base].split(b'\0', 1)[0]
    text = raw.decode('latin-1').replace('\r', '\n')
    print(text.rstrip())
    print()
    if 'Frame length unknown' in text:
        print('fusetest cannot measure the frame length here (its interrupt sync needs every instruction at its '
              'documented length: seen on every Scorpion tried, likely its Even M1, and on MAME\'s ATM Turbo 2+)')
        return 3
    if 'Machine type: TS2068' in text:
        print('fusetest takes this machine for a TS2068 (it does so with every Pentagon): its timing tests mean '
              'nothing here')
        return 3

    joined = text.replace('\n', '')
    bad = []
    for name in TESTS:
        m = re.search(re.escape(name) + r'\.\.\. (passed|skipped|failed \([^)]*\)|incomplete \([^)]*\))', joined)
        verdict = m.group(1) if m else 'missing'
        if verdict not in ('passed', 'skipped'):
            bad.append(f'{name}: {verdict}')
    if bad:
        print(f'{len(bad)} of {len(TESTS)} tests wrong: ' + '; '.join(bad))
        return 1
    print(f'all {len(TESTS)} tests passed or skipped')
    return 0


if __name__ == '__main__':
    sys.exit(main())
