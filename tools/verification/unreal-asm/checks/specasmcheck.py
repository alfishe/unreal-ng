#!/usr/bin/env python3
"""Specasm sources (.s text) through the Specasm frontend: every single-file program of the tests/ folders and the examples of
Specasm's repository is imported and linked by Specasm's own tools (saimport, salink built on the host), converted by
`zxasm convert --codec specasm` to sjasmplus (--zxnext), assembled, and the bytes compared with the linked binary.

    specasmcheck.py <specasm dir> [--zxasm P] [--sjasmplus P] [--keep]

The Specasm tools are the ones built in the directory (`make saimport salink`). Programs of several files and the tests that
expect a link error are left out. Work files: scratch/specasmcheck.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('specasm')
    ap.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM'))
    ap.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.sjasmplus:
        sys.exit('need --zxasm and --sjasmplus')
    saimport, salink = os.path.abspath(os.path.join(args.specasm, 'saimport')), os.path.abspath(os.path.join(args.specasm, 'salink'))
    work = os.path.abspath(os.path.join('scratch', 'specasmcheck'))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    equal, different, skipped = 0, [], 0
    directories = sorted(glob.glob(os.path.join(args.specasm, 'tests', 'test_*')) + glob.glob(os.path.join(args.specasm, 'examples', '*')))
    for source in directories:
        sources = glob.glob(os.path.join(source, '*.s'))
        name = os.path.basename(source)
        if len(sources) != 1:
            skipped += 1
            continue
        d = os.path.join(work, name)
        shutil.copytree(source, d)
        for stale in glob.glob(d + '/*.x') + [p for p in glob.glob(d + '/*') if '.' not in os.path.basename(p) and os.path.basename(p) != 'test']:
            if os.path.isfile(stale):
                os.remove(stale)
        # salink starts a program without an ORG at $8000, sjasmplus at 0: the same origin goes in front of both
        for path in glob.glob(d + '/*.s'):
            text = open(path).read()
            if not any(l.split(';')[0].split()[:1] == ['org'] for l in text.splitlines()):
                open(path, 'w').write('org $8000\n' + text)
        before = set(os.listdir(d))
        run([saimport] + [os.path.basename(p) for p in glob.glob(d + '/*.s')], d)
        run([salink], d)
        produced = [f for f in os.listdir(d) if f not in before and not f.endswith('.x') and os.path.getsize(os.path.join(d, f)) > 0]
        if not produced:
            skipped += 1
            continue
        want = open(os.path.join(d, produced[0]), 'rb').read()
        out = os.path.join(d, 'converted.asm')
        c = run([args.zxasm, 'convert', glob.glob(d + '/*.s')[0], '--codec', 'specasm', '--to', 'sjasmplus', '-o', out], d)
        if c.returncode != 0 or not os.path.exists(out):
            different.append(f'{name}: convert failed')
            continue
        got_file = os.path.join(d, 'got.bin')
        b = run([args.sjasmplus, '--nologo', '--zxnext', f'--raw={got_file}', out], d)
        if not os.path.exists(got_file):
            message = (b.stdout + b.stderr).strip().splitlines()
            different.append(f'{name}: does not build ({message[-1][:80] if message else b.returncode})')
        elif open(got_file, 'rb').read() != want:
            different.append(f'{name}: other bytes ({len(open(got_file, "rb").read())} vs {len(want)})')
        else:
            equal += 1
    print(f'sjasmplus: equal {equal}, different {len(different)}')
    for d in different:
        print('   ', d)
    print(f'skipped (several files, or no linked output): {skipped}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if different else 0


if __name__ == '__main__':
    sys.exit(main())
