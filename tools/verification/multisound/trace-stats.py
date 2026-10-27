#!/usr/bin/env python3
"""Statistics of a ZX-MultiSound real-program trace (CL-2): testdata/sound/multisound/traces/<name>.msc.zst.

Usage: trace-stats.py <trace.msc[.zst]> [--examples N]

Prints the trace's length, the host and GS cycles per port, the polling loops (collapsed reads '*N' and the read
that ended them: where, how long, what value let the program go on) and the intervals between host writes per port.
Times are card-axis ticks (the header's 'rate', 3.5 MHz T-states) from the card's power-on.
Format: tools/verification/multisound/README.md, "Real-program traces".
"""
import collections
import statistics
import subprocess
import sys


def read_text(path):
    if path.endswith('.zst'):
        return subprocess.run(['zstd', '-dcq', path], check=True, capture_output=True).stdout.decode()
    with open(path) as f:
        return f.read()


def port_class(op, port):
    p = int(port, 16)
    if op in ('gin', 'gout'):
        return 'GS port %X' % (p & 0x0F)
    if op == 'gmr':
        return 'GS DAC fetch ch%d' % ((p >> 8) & 3)
    if (p & 0xC00F) == 0xC00D:
        return '#FFFD (YM register / status)'
    if (p & 0xC00F) == 0x800D:
        return '#BFFD (YM data)'
    if (p & 0xFF) == 0xFF:
        return '#%sFF (SAA %s)' % ('01' if p & 0x100 else '00', 'address' if p & 0x100 else 'data')
    if (p & 0xFF) == 0xB3:
        return '#B3 (GS data)'
    if (p & 0xFF) == 0xBB:
        return '#BB (GS command / status)'
    if (p & 0xAF) == 0x0F:
        return '#%02X (SounDrive)' % (p & 0xFF)
    return '#%04X' % p


def main():
    path = sys.argv[1]
    examples = int(sys.argv[sys.argv.index('--examples') + 1]) if '--examples' in sys.argv else 3
    header = {}
    t = 0
    m1 = None
    frames = 0
    counts = collections.Counter()      # (side, op, class) -> cycles
    lines = 0
    runs = collections.defaultdict(list)   # (m1, port class) -> [(iterations, start t, value, exit t, exit value)]
    pending = None
    last_write = {}
    reads_at = collections.defaultdict(collections.Counter)   # (m1, class) -> value -> reads
    intervals = collections.defaultdict(list)
    first_lines = collections.defaultdict(list)
    for raw in read_text(path).splitlines():
        if not raw or raw.startswith('#'):
            continue
        tok = raw.split()
        if tok[0] in ('mask', 'ram', 'dip', 'cpu', 'frame', 'rate', 'length') and len(tok) == 2:
            header[tok[0]] = tok[1]
            continue
        if tok[0] == 'frame-end':
            frames += 1
            continue
        if tok[0] == 'frame-start':
            continue
        lines += 1
        if tok[0] == 'm1':
            m1 = tok[1]
            continue
        for x in tok:
            if x.startswith('+'):
                t += int(x[1:])
        if tok[0] == 'reset':
            continue
        op, port = tok[0], tok[1]
        n = 1
        val = None
        for x in tok[2:]:
            if x.startswith('*'):
                n = int(x[1:])
            elif x.startswith('='):
                val = x[1:]
        cls = port_class(op, port)
        side = 'GS' if op.startswith('g') else 'host'
        counts[(side, op, cls)] += n
        if op == 'in':
            reads_at[(m1, cls)][val] += n
            if pending and pending['m1'] == m1 and pending['cls'] == cls and pending['value'] != val:
                runs[(m1, cls)].append((pending['n'], pending['t'], pending['value'], t, val))
                pending = None
            if pending and pending['m1'] == m1 and pending['cls'] == cls and pending['value'] == val:
                pending['n'] += n       # the same loop across a frame boundary or a GS line
            elif n > 1:
                pending = {'m1': m1, 'cls': cls, 'n': n, 't': t, 'value': val}
            elif pending and (pending['m1'] != m1 or pending['cls'] != cls):
                pending = None
        if op == 'out':
            if cls in last_write:
                intervals[cls].append(t - last_write[cls])
            last_write[cls] = t
        if op in ('in', 'out') and len(first_lines[cls]) < examples:
            first_lines[cls].append('t=%d PC=%s %s' % (t, m1, raw))

    rate = int(header.get('rate', 3500000))
    length = int(header.get('length', t))
    print('%s: %d cycle lines, %d frames, %.2f s (%s ticks at %d Hz), cpu %s MHz, mask %s, dip %s' % (
        path.split('/')[-1], lines, frames, length / rate, length, rate, header.get('cpu'), header.get('mask'),
        header.get('dip')))
    print('\ncycles per port (collapsed reads counted with their repeats):')
    for (side, op, cls), n in sorted(counts.items(), key=lambda kv: (kv[0][0] != 'host', -kv[1])):
        print('  %-4s %-4s %-32s %9d' % (side, op, cls, n))
    print('\nhost reads per instruction (PC of the IN, port, reads, values):')
    if not reads_at:
        print('  none')
    for (pc, cls), vals in sorted(reads_at.items(), key=lambda kv: -sum(kv[1].values()))[:12]:
        print('  PC %s %-30s %8d  %s' % (pc, cls, sum(vals.values()),
                                          ', '.join('%s x%d' % (v, c) for v, c in vals.most_common(4))))
    print('\npolling loops (a run of identical reads at one instruction, ended by a different value):')
    if not runs:
        print('  none')
    for (pc, cls), rs in sorted(runs.items(), key=lambda kv: -len(kv[1])):
        its = [r[0] for r in rs]
        waits = [r[3] - r[1] for r in rs]
        vals = collections.Counter((r[2], r[4]) for r in rs)
        print('  PC %s %-30s %6d loops, reads per loop median %d max %d, wait median %d max %d ticks; %s' % (
            pc, cls, len(rs), statistics.median(its), max(its), statistics.median(waits), max(waits),
            ', '.join('%s->%s x%d' % (a, b, c) for (a, b), c in vals.most_common(3))))
    print('\nintervals between host writes (ticks):')
    for cls, iv in sorted(intervals.items()):
        if len(iv) > 2:
            print('  %-32s %7d writes, median %d, min %d' % (cls, len(iv) + 1, statistics.median(iv), min(iv)))
    print('\nfirst host cycles per port:')
    for cls, ex in sorted(first_lines.items()):
        for e in ex:
            print('  %-32s %s' % (cls, e))


if __name__ == '__main__':
    main()
