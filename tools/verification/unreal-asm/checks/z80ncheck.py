#!/usr/bin/env python3
"""ZX Spectrum Next sources through unreal-asm and back: every test program of a ZXSpectrumNextTests checkout is built
with sjasmplus (--zxnext=cspect, as the suite's own Tools/buildTests.sh does), every source of the checkout is converted
by `zxasm convert --z80n` to sjasmplus, the programs are built again from the converted tree, and the files the two
builds wrote (.snx / .nex ...) are compared byte for byte.

    z80ncheck.py <ZXSpectrumNextTests dir> [--zxasm PATH] [--sjasmplus PATH] [--keep]

zxasm: --zxasm or UNREAL_ASM_ZXASM; sjasmplus: --sjasmplus or UNREAL_ASM_SJASMPLUS. Work files go to scratch/z80ncheck
(the checkout is copied: a build writes its output next to the source).
"""
import argparse
import os
import shutil
import subprocess
import sys

FLAGS = ['--syntax=abF', '--fullpath', '--nologo', '--zxnext=cspect', '--msg=war']
SKIP = ('.asm', '.lst', '.sld', '.sym')


def build(sjasmplus, directory, main):
    """The files the build wrote (name -> bytes), or None and the message when sjasmplus reports errors"""
    before = {n: os.path.getmtime(os.path.join(directory, n)) for n in os.listdir(directory)}
    run = subprocess.run([sjasmplus, *FLAGS, main], cwd=directory, capture_output=True, text=True)
    if run.returncode != 0:
        return None, (run.stdout + run.stderr).strip()
    out = {}
    for n in sorted(os.listdir(directory)):
        path = os.path.join(directory, n)
        if os.path.isfile(path) and (n not in before or os.path.getmtime(path) != before[n]) and not n.endswith(SKIP):
            out[n] = open(path, 'rb').read()
    return out, ''


def convert_tree(zxasm, tree, work):
    """Every directory holding sources is converted on its own; the converted text replaces the source in place"""
    problems = []
    for root, _, files in os.walk(tree):
        names = {f.lower(): f for f in files if f.lower().endswith('.asm')}
        if not names:
            continue
        out = os.path.join(work, 'convert', os.path.relpath(root, tree))
        run = subprocess.run([zxasm, 'convert', root, '--to', 'sjasmplus', '--from', 'sjasmplus', '--z80n', '-o', out], capture_output=True, text=True)
        if run.returncode != 0:
            problems.append(f'{os.path.relpath(root, tree)}: zxasm {run.returncode}')
        for low, name in names.items():
            converted = os.path.join(out, low)
            if os.path.exists(converted):
                shutil.copyfile(converted, os.path.join(root, name))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tests')
    ap.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM'))
    ap.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.sjasmplus:
        sys.exit('need --zxasm and --sjasmplus (or UNREAL_ASM_ZXASM / UNREAL_ASM_SJASMPLUS)')
    work = os.path.abspath(os.path.join('scratch', 'z80ncheck'))
    shutil.rmtree(work, ignore_errors=True)
    original, converted = os.path.join(work, 'orig'), os.path.join(work, 'conv')
    ignore = shutil.ignore_patterns('.git')
    shutil.copytree(args.tests, original, ignore=ignore)
    shutil.copytree(args.tests, converted, ignore=ignore)
    for problem in convert_tree(os.path.abspath(args.zxasm), converted, work):
        print('CONVERT', problem)
    equal = different = skipped = 0
    for root, _, files in sorted(os.walk(os.path.join(original, 'Tests'))):
        mains = [f for f in files if f.lower() == 'main.asm']
        if not mains:
            continue
        name = os.path.relpath(root, original)
        built, error = build(args.sjasmplus, root, mains[0])
        if not built:
            print(f'SKIP  {name}: {"sjasmplus: " + error.splitlines()[0] if built is None else "wrote no file"}')
            skipped += 1
            continue
        back, error = build(args.sjasmplus, os.path.join(converted, name), mains[0])
        if back is None:
            print(f'DIFF  {name}: converted source does not build: {error.splitlines()[0]}')
            different += 1
        elif back == built:
            print(f'EQUAL {name}: {", ".join(f"{n} {len(b)}" for n, b in built.items())}')
            equal += 1
        else:
            print(f'DIFF  {name}: {sorted(built)} vs {sorted(back)}')
            different += 1
    print(f'equal {equal}, different {different}, skipped {skipped}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if different else 0


if __name__ == '__main__':
    sys.exit(main())
