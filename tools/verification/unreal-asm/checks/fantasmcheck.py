#!/usr/bin/env python3
"""FantASM sources through the FantASM frontend: every tests/*.asm of FantASM (the files its own test script builds with FantASM
and with sjasmplus) is assembled by FantASM (-N, --cspect), converted by `zxasm convert --codec fantasm --z80n` to sjasmplus
and to z80asm, each assembled, and the bytes compared with FantASM's.

    fantasmcheck.py <FantASM dir> [--zxasm P] [--fantasm P] [--sjasmplus P] [--z80asm P] [--keep]

Tools: --zxasm / UNREAL_ASM_ZXASM, --fantasm / UNREAL_ASM_FANTASM, --sjasmplus / UNREAL_ASM_SJASMPLUS, --z80asm /
UNREAL_ASM_Z80ASM (+ ZCCCFG). Work files: scratch/fantasmcheck.
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
    ap.add_argument('fantasmdir')
    for name in ('zxasm', 'fantasm', 'sjasmplus', 'z80asm'):
        ap.add_argument(f'--{name}', default=os.environ.get(f'UNREAL_ASM_{name.upper()}'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.fantasm:
        sys.exit('need --zxasm and --fantasm')
    work = os.path.abspath(os.path.join('scratch', 'fantasmcheck'))
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(os.path.join(args.fantasmdir, 'tests'), work + '/src')
    src = work + '/src'
    targets = [t for t in ('sjasmplus', 'z80asm') if getattr(args, t)]
    equal = {t: 0 for t in targets}
    different = {t: [] for t in targets}
    skipped = 0
    for path in sorted(glob.glob(src + '/*.asm')):
        name = os.path.basename(path)
        ref = work + '/ref.bin'
        if os.path.exists(ref):
            os.remove(ref)
        run([args.fantasm, name, ref, '--z80n', '--nologo', '--cspect'], src)
        if not os.path.exists(ref) or os.path.getsize(ref) == 0:
            skipped += 1
            continue
        want = open(ref, 'rb').read()
        for target in targets:
            out = f'{src}/{name}.{target}.asm'
            dialect = {'z80asm': 'z88dk'}.get(target, target)
            c = run([args.zxasm, 'convert', path, '--codec', 'fantasm', '--to', dialect, '--z80n', '-o', out], src)
            if c.returncode != 0 or not os.path.exists(out):
                different[target].append(f'{name}: convert failed')
                continue
            got_file = work + '/got.bin'
            for old in glob.glob(work + '/got*') + glob.glob(f'{src}/{name}.{target}*.bin'):
                os.remove(old)
            if target == 'sjasmplus':
                b = run([args.sjasmplus, '--nologo', '--zxnext=cspect', f'--raw={got_file}', out], src)
            else:
                b = run([args.z80asm, '-mz80n', '-b', out], src)
                bins = sorted(glob.glob(f'{src}/{name}.{target}*.bin'))
                bins = [b for b in bins if os.path.getsize(b)]   # the empty one is the section-less file
                if bins:
                    shutil.copyfile(bins[0], got_file)
            if not os.path.exists(got_file):
                message = (b.stdout + b.stderr).strip().splitlines()
                different[target].append(f'{name}: does not build ({message[-1][:80] if message else b.returncode})')
            elif open(got_file, 'rb').read() != want:
                different[target].append(f'{name}: other bytes')
            else:
                equal[target] += 1
    for target in targets:
        print(f'{target}: equal {equal[target]}, different {len(different[target])}')
        for d in different[target]:
            print('   ', d)
    print(f'skipped (FantASM builds nothing from it): {skipped}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if any(different.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
