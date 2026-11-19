#!/usr/bin/env python3
"""Turn the oracle traces into the golden files of the emulator test (core/tests/emulator/p2666/p2666_timing_test.cpp).

Usage: golden.py <traces dir> <programs dir> <out dir>

The full traces are tens of MB (every idle HALT fetch at 28 MHz). The golden file keeps:
- the M1 lines with an address other than the previous M1's (a HALT's idle fetches collapse to the first),
- the INTA, IOR, IOW and INT lines,
- only inside the compared windows: the start (the first WINDOW edges) and WINDOW edges either side of every INT pulse.
`WIN <from> <to>` lines list the windows; the test compares the emulator's instruction starts inside them only.
"""
import os
import shutil
import sys

WINDOW = 40000


def main():
    traces, programs, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    for name in sorted(f[:-4] for f in os.listdir(traces) if f.endswith('.txt')):
        lines = open(os.path.join(traces, name + '.txt')).read().split('\n')
        ints = [int(l.split()[1]) for l in lines if l.startswith('INT ')]
        windows = [(0, WINDOW)] + [(e - WINDOW, e + WINDOW) for e in ints]
        merged = []
        for a, b in sorted(windows):
            if merged and a <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], b))
            else:
                merged.append((max(0, a), b))
        keep = []
        last = None
        for l in lines:
            if not l:
                continue
            parts = l.split()
            if parts[0] == 'M1':
                if parts[2] == last:
                    continue
                last = parts[2]
            elif parts[0] != 'INTA':
                last = None
            edge = int(parts[1]) if len(parts) > 1 else None
            if edge is not None and not any(a <= edge <= b for a, b in merged):
                continue
            keep.append(l)
        with open(os.path.join(out, name + '.trace'), 'w') as f:
            f.write('FRAMES %s\n' % open(os.path.join(programs, name + '.frames')).read().strip())
            for a, b in merged:
                f.write('WIN %d %d\n' % (a, b))
            f.write('\n'.join(keep) + '\n')
        shutil.copy(os.path.join(programs, name + '.bin'), os.path.join(out, name + '.bin'))
        print(name, len(keep), 'lines', len(merged), 'windows')


if __name__ == '__main__':
    main()
