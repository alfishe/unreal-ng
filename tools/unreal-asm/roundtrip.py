#!/usr/bin/env python3
"""Convert every source of a set of TR-DOS images to sjasmplus, read each result back through the sjasmplus frontend
and backend (the text must not change), and optionally assemble every converted source.

    roundtrip.py <out dir> <image.trd|.scl>... [--zxasm PATH] [--sjasmplus PATH] [--assemble]

Writes <out dir>/<n>/ per image (index.txt maps numbers to images) and prints a summary: sources converted, lines
the converter could not convert, round-trip differences, and with --assemble the sjasmplus error count per file
(a part of a project, ALASM MAIN, is skipped: it assembles through its main source).
zxasm: --zxasm or UNREAL_ASM_ZXASM; sjasmplus: --sjasmplus or UNREAL_ASM_SJASMPLUS.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zxdisk  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('out')
    parser.add_argument('images', nargs='+')
    parser.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM', 'zxasm'))
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--assemble', action='store_true')
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    index = open(os.path.join(args.out, 'index.txt'), 'w')
    problems = collections.Counter()
    converted = differing = assembled = clean = 0
    for n, image in enumerate(args.images, 1):
        index.write(f'{n} {image}\n')
        d = os.path.join(args.out, str(n))
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(os.path.join(d, 'rt'))
        source = image
        if image.lower().endswith('.scl'):
            source = os.path.join(d, 'image.trd')
            open(source, 'wb').write(zxdisk.scl2trd(open(image, 'rb').read()))
        run = subprocess.run([args.zxasm, 'convert', source, '--to', 'sjasmplus', '-o', d], capture_output=True, text=True, errors='replace')
        open(os.path.join(d, 'convert.log'), 'w').write(run.stderr)
        for line in run.stderr.split('\n'):
            m = re.search(r'not converted: (unparsed line|[^:]+)', line) or re.search(r'not parsed \(([^)]*)\)', line)
            if m:
                problems[m.group(1)] += 1
        for f in sorted(os.listdir(d)):
            if not f.endswith('.asm'):
                continue
            converted += 1
            path, back = os.path.join(d, f), os.path.join(d, 'rt', f)
            subprocess.run([args.zxasm, 'convert', path, '--codec', 'sjasmplus', '--codepage', 'cp866', '--to', 'sjasmplus', '-o', back],
                           capture_output=True)
            if not os.path.exists(back) or open(path, 'rb').read() != open(back, 'rb').read():
                differing += 1
                print(f'round trip differs: {n}/{f}')
            if args.assemble and b'ALASM MAIN' not in open(path, 'rb').read():
                assembled += 1
                with tempfile.NamedTemporaryFile('w', suffix='.asm', dir=d, delete=False) as h:
                    h.write(f'        DEVICE ZXSPECTRUM128\n        INCLUDE "{f}"\n')
                result = subprocess.run([args.sjasmplus, '--nologo', os.path.basename(h.name)], cwd=d, capture_output=True, text=True,
                                        errors='replace', timeout=120)
                os.remove(h.name)
                m = re.search(r'Errors: (\d+)', result.stdout + result.stderr)
                errors = int(m.group(1)) if m else -1
                clean += errors == 0
                if errors:
                    first = next((line for line in (result.stdout + result.stderr).split('\n') if 'error:' in line), '')
                    print(f'sjasmplus {n}/{f}: {errors if errors >= 0 else "no result"} error(s)  {first.strip()[:120]}')
    print(f'images: {len(args.images)}  sources: {converted}  round-trip differences: {differing}')
    if args.assemble:
        print(f'assembled alone: {assembled}  without errors: {clean}')
    for what, count in problems.most_common(15):
        print(f'  {count:5}  {what}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
