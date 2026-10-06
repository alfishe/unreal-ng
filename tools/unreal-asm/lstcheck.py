#!/usr/bin/env python3
"""Compare the bytes of a sjasmplus listing with a memory dump: every listed line whose bytes differ from the dump at
its address is printed. The check behind a converted program that has no saved object but a running copy (an
assembler's own source against the assembler unpacked in memory).

    lstcheck.py <listing.lst> <memory.bin> [--base ADDRESS] [--from ADDRESS] [--to ADDRESS]

memory.bin starts at --base (default 0: a 64K dump). Lines outside --from / --to (default #0000 / #FFFF) are skipped.
Variables a program changed while it ran differ as a matter of course: read the printed lines, not just the count.
"""
import argparse
import re
import sys

LINE = re.compile(r'^\s*\d+\+*\s*([0-9A-F]{4})\s((?:[0-9A-F]{2}\s?)+)\s*(.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('listing')
    parser.add_argument('memory')
    parser.add_argument('--base', type=lambda v: int(v, 0), default=0)
    parser.add_argument('--from', dest='low', type=lambda v: int(v, 0), default=0)
    parser.add_argument('--to', dest='high', type=lambda v: int(v, 0), default=0xFFFF)
    args = parser.parse_args()
    memory = open(args.memory, 'rb').read()
    compared = differing = 0
    for line in open(args.listing, encoding='cp866', errors='replace'):
        m = LINE.match(line.rstrip('\n'))
        if not m:
            continue
        address = int(m.group(1), 16)
        want = [int(x, 16) for x in m.group(2).split()]
        if address < args.low or address > args.high or address < args.base:
            continue
        got = list(memory[address - args.base:address - args.base + len(want)])
        compared += len(want)
        if got != want:
            differing += sum(1 for a, b in zip(got, want) if a != b) + abs(len(want) - len(got))
            print(f'{address:04X}  listing {" ".join(f"{x:02X}" for x in want):<14} memory {" ".join(f"{x:02X}" for x in got):<14} | {m.group(3)[:60]}')
    print(f'bytes compared: {compared}  differing: {differing}')
    return 0 if differing == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
