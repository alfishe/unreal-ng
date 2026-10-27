#!/usr/bin/env python3
"""Check the labels the symbol module takes from sources against sjasmplus on real disks: every main source of a set of
TR-DOS images is converted to sjasmplus (zxasm) and assembled with sjasmplus --sym; symconv source lays out the same
project and writes its labels under the converted names (--sjasmplus-names, in the native format: no target's name
rules rename them); the two must hold the same names with the same values.

    symcheck.py <out dir> [<image.trd|.scl>...] [--images-from LIST] [--zxasm PATH] [--symconv PATH] [--sjasmplus PATH]

A main source is a file no other file of its image INCLUDEs. Sources sjasmplus does not assemble without errors (or
within --timeout seconds) are skipped. zxasm: --zxasm or UNREAL_ASM_ZXASM; symconv: --symconv or UNREAL_ASM_SYMCONV; sjasmplus: --sjasmplus or
UNREAL_ASM_SJASMPLUS.
"""
import argparse
import collections
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import crosscheck  # noqa: E402
import zxdisk  # noqa: E402


def read_sym(path):
    """{name: value} of a sjasmplus --sym file"""
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, encoding='latin-1'):
        name, sep, value = line.rstrip('\n').partition(': EQU 0x')
        if sep:
            out[name] = int(value, 16)
    return out


def read_native(path):
    """{name: value} of a native symbol file: a page symbol at its CPU address (the window it was shown in)"""
    out = {}
    if not os.path.exists(path):
        return out
    for symbol in json.load(open(path, encoding='utf-8'))['sets'][0]['symbols']:
        value = symbol.get('offset', 0)
        if 'window' in symbol and symbol.get('space', '').startswith(('ram', 'rom')):
            value = symbol['window'] * 0x4000 + value
        out[symbol['name']] = value
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('out')
    parser.add_argument('images', nargs='*')
    parser.add_argument('--images-from')
    parser.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM', 'zxasm'))
    parser.add_argument('--symconv', default=os.environ.get('UNREAL_ASM_SYMCONV', 'symconv'))
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    if args.images_from:
        args.images += [line.rstrip('\n') for line in open(args.images_from) if line.strip()]
    os.makedirs(args.out, exist_ok=True)
    index = open(os.path.join(args.out, 'index.txt'), 'w')
    tally = collections.Counter()
    labels = 0
    for n, image in enumerate(args.images, 1):
        index.write(f'{n} {image}\n')
        index.flush()
        base = os.path.join(args.out, str(n))
        shutil.rmtree(base, ignore_errors=True)
        os.makedirs(base)
        source = image
        if image.lower().endswith('.scl'):
            source = os.path.join(base, 'image.trd')
            open(source, 'wb').write(zxdisk.scl2trd(open(image, 'rb').read()))
        converted = os.path.join(base, 'sjasmplus')
        subprocess.run([args.zxasm, 'convert', source, '--to', 'sjasmplus', '-o', converted], capture_output=True)
        if not os.path.isdir(converted):
            continue
        sources = sorted(f for f in os.listdir(converted) if f.endswith('.asm'))
        # Two sources whose names differ only in case (lorenz1k.a, LORENZ1K.H) share one file on a case-insensitive
        # host: sjasmplus would assemble the other one
        listed = subprocess.run([args.zxasm, 'files', source], capture_output=True, text=True, errors='replace').stdout.split('\n')
        names = [line.split('\t')[0].rsplit('.', 1)[0] for line in listed if line.count('\t') >= 3 and not line.endswith('\t-')]
        clash = {n.lower() for n in names if sum(1 for m in names if m.lower() == n.lower()) > 1}
        included = set()
        for f in sources:
            included |= crosscheck.includes(converted, f)
        for f in sources:
            name = f.removesuffix('.asm')
            if name.lower() in included:
                continue
            if name.lower() in clash:
                tally['skipped (names differ only in case)'] += 1
                continue
            try:
                run = subprocess.run([args.sjasmplus, '--nologo', '--sym=__sj.sym', f], cwd=converted, capture_output=True, text=True,
                                     errors='replace', timeout=args.timeout)
            except subprocess.TimeoutExpired:
                tally['skipped (sjasmplus timeout)'] += 1
                print(f'timeout {n}/{name}', flush=True)
                continue
            if run.returncode != 0 or 'error' in run.stdout.lower().replace('errors: 0', ''):
                tally['skipped (sjasmplus errors)'] += 1
                continue
            reference = read_sym(os.path.join(converted, '__sj.sym'))
            ours_path = os.path.join(base, name + '.ours.usym.json')
            try:
                ours_run = subprocess.run([args.symconv, 'source', source, '--main', name, '--to', 'native', '--sjasmplus-names', '-o',
                                           ours_path], capture_output=True, text=True, errors='replace', timeout=args.timeout)
            except subprocess.TimeoutExpired:
                tally['symconv timeout'] += 1
                print(f'symconv timeout {n}/{name}', flush=True)
                continue
            ours = read_native(ours_path)
            missing = sorted(set(reference) - set(ours))
            extra = sorted(set(ours) - set(reference))
            wrong = sorted(k for k in set(reference) & set(ours) if reference[k] != ours[k])
            labels += len(reference)
            if missing or extra or wrong:
                tally['differ'] += 1
                print(f'differ {n}/{name}: {len(wrong)} values, {len(missing)} missing, {len(extra)} extra '
                      f'(first: {(wrong + missing + extra)[0]})', flush=True)
                for line in ours_run.stderr.split('\n'):
                    if line.startswith('error'):
                        print('   ', line, flush=True)
            else:
                tally['equal'] += 1
    print('  '.join(f'{k}: {v}' for k, v in sorted(tally.items())) + f'  labels compared: {labels}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
