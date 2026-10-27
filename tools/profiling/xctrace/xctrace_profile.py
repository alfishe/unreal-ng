#!/usr/bin/env python3
"""Reports from an xctrace Time Profiler export (the time-profile table, see record.sh and README.md).

Each sample is one thread's stack at one moment, leaf first, with inlined frames and source lines when the binary
has debug info (a Release build with -g, as tools/build/build.sh makes). Every report counts the samples left
after the filters (thread, running state, --under, --exclude) and prints percentages of them.

  threads                    samples per thread: pick the --thread to look at
  top     [-n N]             per function: inclusive (on the stack) and self (the leaf)
  lines   [-n N] [--func S]  self time per source line of the leaf, with its caller
  callees FUNC [--depth D]   where the time under FUNC goes: the callee chains below its outermost occurrence

Frames without a symbol (a GUI app's own code without dSYM, JIT, stripped libraries) show as addresses;
--binary and --load symbolize them with atos (--load: the binary's load address, from the trace's process list
in Instruments or `vmmap <pid>`).
"""
import argparse
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import Counter


def load_samples(path):
    """[(thread, running, [(name, addr, 'file:line'), ...] leaf first)]"""
    root = ET.parse(path).getroot()
    ids = {}
    for el in root.iter():
        i = el.get('id')
        if i is not None:
            ids[i] = el

    def resolve(el):
        ref = el.get('ref')
        return ids[ref] if ref is not None else el

    samples = []
    for row in root.iter('row'):
        thread_el = row.find('thread')
        thread = (resolve(thread_el).get('fmt') or '?') if thread_el is not None else '?'
        state_el = row.find('thread-state')
        running = state_el is None or resolve(state_el).text == 'Running'
        bt = row.find('backtrace')
        if bt is None:
            continue
        frames = []
        for f in resolve(bt).findall('frame'):
            f = resolve(f)
            line = '?'
            src = f.find('source')
            if src is not None:
                path_el = src.find('path')
                file = resolve(path_el).text if path_el is not None else ''
                line = (file or '').split('/')[-1] + ':' + (src.get('line') or '?')
            frames.append((f.get('name') or '?', f.get('addr') or '', line))
        samples.append((thread, running, frames))
    return samples


def short(name):
    """The function name without its parameter list ("(anonymous namespace)" kept as "{anon}")"""
    name = name.replace('(anonymous namespace)', '{anon}')
    return re.sub(r'\(.*', '', name).strip()


def symbolize(samples, binary, load):
    addrs = sorted({a for _, _, fr in samples for n, a, _ in fr if n.startswith('0x') and a})
    names = {}
    for i in range(0, len(addrs), 2000):
        chunk = addrs[i:i + 2000]
        out = subprocess.run(['atos', '-o', binary, '-l', load] + chunk, capture_output=True, text=True).stdout
        for a, s in zip(chunk, out.strip().split('\n')):
            names[a] = re.sub(r' \(in [^)]*\)', '', s)
    return [(t, r, [(names.get(a, n) if n.startswith('0x') else n, a, l) for n, a, l in fr]) for t, r, fr in samples]


def selected(samples, args):
    kept = []
    for thread, running, frames in samples:
        if args.thread and args.thread not in thread:
            continue
        if not running and not args.all_states:
            continue
        names = [n for n, _, _ in frames]
        if args.under and not any(u in n for u in args.under for n in names):
            continue
        if args.exclude and any(x in n for x in args.exclude for n in names):
            continue
        kept.append(frames)
    return kept


def pct(count, total):
    return f'{100.0 * count / total:5.1f}% {count:6d}'


def report_threads(samples, args):
    counts = Counter(t for t, running, _ in samples if running or args.all_states)
    total = sum(counts.values()) or 1
    for thread, count in counts.most_common():
        print(f'{pct(count, total)}  {thread}')


def report_top(stacks, args):
    total = len(stacks)
    inclusive, own = Counter(), Counter()
    for frames in stacks:
        for name in {short(n) for n, _, _ in frames}:
            inclusive[name] += 1
        own[short(frames[0][0])] += 1
    print(f'samples: {total}')
    print('--- inclusive')
    for name, count in inclusive.most_common(args.n):
        print(f'{pct(count, total)}  {name[:120]}')
    print('--- self')
    for name, count in own.most_common(args.n):
        print(f'{pct(count, total)}  {name[:120]}')


def report_lines(stacks, args):
    total = len(stacks)
    by_line = Counter()
    for frames in stacks:
        name, _, line = frames[0]
        caller = short(frames[1][0])[:40] if len(frames) > 1 else ''
        by_line[(short(name)[:60], line, caller)] += 1
    items = by_line.most_common()
    if args.func:
        items = [x for x in items if any(f in x[0][0] or f in x[0][2] for f in args.func)]
    print(f'samples: {total}')
    for (name, line, caller), count in items[:args.n]:
        print(f'{pct(count, total)}  {line:36s} {name:60s} [{caller}]')


def report_callees(stacks, args):
    total = len(stacks)
    under = 0
    chains = Counter()
    for frames in stacks:
        names = [short(n)[:70] for n, _, _ in frames]
        hits = [i for i, n in enumerate(names) if n.startswith(args.function)]
        if not hits:
            continue
        under += 1
        i = hits[-1]  # the outermost occurrence: recursion counts once
        chain = ' > '.join(names[max(0, i - args.depth):i][::-1]) if i > 0 else '(self)'
        chains[chain] += 1
    print(f'{args.function}: {under} of {total} samples ({100.0 * under / max(total, 1):.1f}%)')
    for chain, count in chains.most_common(args.n):
        print(f'{pct(count, total)}  {chain}')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('xml', help='the export written by record.sh (or xctrace export ... time-profile)')
    p.add_argument('--thread', default='Main Thread',
                   help='keep the threads whose name contains this (default "Main Thread"; "" = all threads)')
    p.add_argument('--under', action='append', default=[], metavar='FUNC',
                   help='keep only the samples with FUNC on the stack (substring; repeatable: any of them)')
    p.add_argument('--exclude', action='append', default=[], metavar='FUNC',
                   help='drop the samples with FUNC on the stack (setup, checks: the part not measured)')
    p.add_argument('--all-states', action='store_true', help='count blocked / waiting samples too')
    p.add_argument('--binary', help='symbolize address-only frames with atos from this binary')
    p.add_argument('--load', help='the binary\'s load address for atos (with --binary)')
    sub = p.add_subparsers(dest='report', required=True)
    sub.add_parser('threads')
    top = sub.add_parser('top')
    top.add_argument('-n', type=int, default=40)
    lines = sub.add_parser('lines')
    lines.add_argument('-n', type=int, default=60)
    lines.add_argument('--func', action='append', default=[], help='only the lines of / called from FUNC')
    callees = sub.add_parser('callees')
    callees.add_argument('function', help='function name prefix (without the parameter list)')
    callees.add_argument('--depth', type=int, default=1, help='callee chain length (default 1)')
    callees.add_argument('-n', type=int, default=25)
    args = p.parse_args()

    samples = load_samples(args.xml)
    if args.binary:
        if not args.load:
            p.error('--binary needs --load')
        samples = symbolize(samples, args.binary, args.load)
    if args.report == 'threads':
        report_threads(samples, args)
        return
    stacks = selected(samples, args)
    if not stacks:
        sys.exit('no samples left after the filters (try the threads report, --thread "", --under)')
    {'top': report_top, 'lines': report_lines, 'callees': report_callees}[args.report](stacks, args)


if __name__ == '__main__':
    main()
