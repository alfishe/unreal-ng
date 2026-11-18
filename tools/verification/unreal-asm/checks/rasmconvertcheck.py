#!/usr/bin/env python3
"""rasm sources to sjasmplus through the rasm frontend (the rasm engine): every snippet of unreal-asm
testdata/rasm/autotests that rasm assembled to a binary (index.tsv exit code 0, NAME.bin) is converted by
`zxasm convert --codec rasm --from rasm --to sjasmplus`, built by sjasmplus (--raw) and its bytes compared with rasm's. A snippet the
frontend converts line by line (the engine refused it) is reported apart; a conversion or build that fails, or other
bytes, is a difference.

    rasmconvertcheck.py [--zxasm P] [--sjasmplus P] [--filter SUBSTRING] [--keep] [-v]

Tools: --zxasm / UNREAL_ASM_ZXASM, --sjasmplus / UNREAL_ASM_SJASMPLUS. Work files: scratch/rasmconvertcheck.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.normpath(os.path.join(HERE, '../../../../core/src/3rdparty/unreal-asm/testdata/rasm/autotests'))


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM'))
    ap.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS'))
    ap.add_argument('--filter', default='')
    ap.add_argument('--keep', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.sjasmplus:
        sys.exit('need --zxasm and --sjasmplus')
    work = os.path.abspath(os.path.join('scratch', 'rasmconvertcheck'))
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(CORPUS, work)

    cases = []
    for line in open(os.path.join(CORPUS, 'index.tsv')):
        fields = line.rstrip('\n').split('\t')
        if len(fields) >= 2 and fields[1] == '0' and os.path.exists(os.path.join(CORPUS, fields[0] + '.bin')):
            if args.filter in fields[0]:
                cases.append(fields[0])

    same, textual, differ = [], [], []
    for name in cases:
        want = open(os.path.join(CORPUS, name + '.bin'), 'rb').read()
        conv = run([args.zxasm, 'convert', name + '.asm', '--codec', 'rasm', '--from', 'rasm', '--to', 'sjasmplus', '-o', name + '.sja'], work)
        if conv.returncode != 0:
            differ.append((name, 'convert: ' + (conv.stderr or conv.stdout).strip().splitlines()[-1:][0] if (conv.stderr or conv.stdout).strip() else 'convert failed'))
            continue
        if 'converted line by line' in conv.stderr + conv.stdout:
            textual.append(name)
            continue
        build = run([args.sjasmplus, '--nologo', '--raw=' + name + '.out', name + '.sja'], work)
        out = os.path.join(work, name + '.out')
        if build.returncode != 0 or not os.path.exists(out):
            lines = [l for l in (build.stdout + build.stderr).splitlines() if 'rror' in l]
            differ.append((name, 'sjasmplus: ' + (lines[0] if lines else 'failed')))
            continue
        got = open(out, 'rb').read()
        if got == want:
            same.append(name)
        else:
            k = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), min(len(got), len(want)))
            differ.append((name, f'bytes: {len(got)} vs rasm {len(want)}, first difference at {k}'))

    print(f'same bytes: {len(same)}  converted line by line: {len(textual)}  different: {len(differ)}  (of {len(cases)})')
    if args.verbose:
        for name in textual:
            print('  line by line:', name)
    for name, why in differ:
        print(f'  {name}: {why}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if differ else 0)


if __name__ == '__main__':
    main()
