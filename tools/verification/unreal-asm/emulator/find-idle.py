#!/usr/bin/env python3
"""Finds an assembler's idle rule (asm-synchronizer.md §6.3: the return addresses its key wait leaves on the stack
while it waits at its prompt). The assembler is started with an inject-oracle profile (../lib/assemblers.py), the
machine paused at many moments of its wait, and the stack read each time: a pair of words that follow each other on
every sample, each one right after a CALL (CD / C4 ... / RST), is a candidate chain. The descriptor gets the chain as
`idleChain` (the first pair) and `idleAlternatives` (the others), and inject-oracle.py checks it.

    find-idle.py zeusgg <ZEUS_GG.TRD> --boot ZEUS [--samples 16] [--depth 64] [--keep]
"""
import argparse
import os
import random
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from assemblers import PROFILES  # noqa: E402
from emulator import DEFAULT_PORT, Emulator  # noqa: E402


def after_call(emu, address):
    """`address` follows a CALL nn / CALL cc,nn (3 bytes) or an RST (1 byte)"""
    if address < 3:
        return False
    before = emu.read(address - 3, 3)
    return before[0] == 0xCD or (before[0] & 0xC7) == 0xC4 or (before[2] & 0xC7) == 0xC7


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=sorted(PROFILES))
    parser.add_argument('disk')
    parser.add_argument('--boot')
    parser.add_argument('--assembler', dest='descriptor', help='the descriptor whose start the profile takes (zasmr: zasm-3.2x ...)')
    parser.add_argument('--right', type=int, default=0)
    parser.add_argument('--combo-frames', type=int, default=4)
    parser.add_argument('--help-key', action='store_true')
    parser.add_argument('--samples', type=int, default=16)
    parser.add_argument('--depth', type=int, default=64, help='stack bytes read at each sample')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    parser.add_argument('--keep', action='store_true')
    args = parser.parse_args()
    profile = PROFILES[args.assembler]
    args.descriptor = args.descriptor or profile.descriptor
    emu = Emulator(port=args.port, model=profile.model, ram_size=profile.ram_size)
    try:
        profile.boot(emu, args)
        emu.stop_recording()
        samples = []
        for _ in range(args.samples):
            time.sleep(random.uniform(0.05, 0.35))
            emu.post('/pause')
            sp = emu.get('/registers')['special']['sp']
            stack = emu.read(sp, min(args.depth, 0x10000 - sp))
            words = [stack[k] | (stack[k + 1] << 8) for k in range(0, len(stack) - 1, 2)]
            samples.append((sp, words))
            emu.post('/resume')
        emu.post('/pause')
        pairs = None
        for _, words in samples:
            here = {(words[k], words[k + 1]) for k in range(len(words) - 1)}
            pairs = here if pairs is None else pairs & here
        print('SP at the samples:', sorted({f'#{sp:04X}' for sp, _ in samples}))
        found = sorted((p for p in pairs or set() if min(p) >= 0x4000 and after_call(emu, p[0]) and after_call(emu, p[1])),   # the ROM's own waits left out
                       key=lambda p: min(samples[0][1].index(p[0]) if p[0] in samples[0][1] else 99, 99))
        for a, b in found:
            print(f'candidate chain: #{a:04X}, #{b:04X}')
        if not found:
            print('no pair of return addresses on every sample')
        emu.post('/resume')
    finally:
        if not args.keep:
            emu._request('DELETE', emu.base)
    return 0 if found else 1


if __name__ == '__main__':
    sys.exit(main())
