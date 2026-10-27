#!/usr/bin/env python3
"""z80asm sources through the z80asm frontend: the cases of z88dk's own test suite (z80asmcases.py) are converted by
`zxasm convert --codec z80asm` to sjasmplus and to z80asm (the backend), each assembled, and the bytes compared with the bytes
the test suite expects (z80asm itself is run on the original as a control).

    z80asmcheck.py <z88dk dir> [--zxasm P] [--sjasmplus P] [--z80asm P] [--keep]

Tools: --zxasm / UNREAL_ASM_ZXASM, --sjasmplus / UNREAL_ASM_SJASMPLUS, --z80asm / UNREAL_ASM_Z80ASM (+ ZCCCFG). Work files:
scratch/z80asmcheck.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z80asmcases  # noqa: E402


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, capture_output=True, text=True)


def assemble_sjasmplus(sjasmplus, src, work):
    out = work + '/sj.bin'
    if os.path.exists(out):
        os.remove(out)
    run([sjasmplus, '--nologo', f'--raw={out}', src], work)
    return open(out, 'rb').read() if os.path.exists(out) else None


def assemble_z80asm(z80asm, src, work):
    for old in glob.glob(work + '/z8.*'):
        os.remove(old)
    shutil.copyfile(src, work + '/z8.asm')
    run([z80asm, '-b', 'z8.asm'], work)
    bins = sorted(glob.glob(work + '/z8*.bin'))
    return open(bins[0], 'rb').read() if bins else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('z88dk')
    for name in ('zxasm', 'sjasmplus', 'z80asm'):
        ap.add_argument(f'--{name}', default=os.environ.get(f'UNREAL_ASM_{name.upper()}'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.z80asm:
        sys.exit('need --zxasm and --z80asm')
    work = os.path.abspath(os.path.join('scratch', 'z80asmcheck'))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    targets = [t for t in ('sjasmplus', 'z80asm') if t == 'z80asm' or args.sjasmplus]
    equal = {t: 0 for t in targets}
    different = {t: [] for t in targets}
    controls = 0
    count = {}
    for name, source, want in z80asmcases.cases(args.z88dk):
        count[name] = count.get(name, 0) + 1
        label = f'{name}-{count[name]}'
        original = f'{work}/{label}.asm'
        open(original, 'w').write(source)
        if assemble_z80asm(args.z80asm, original, work) != want:
            continue   # z80asm itself does not give the expected bytes here (an option, a library): not a case for the frontend
        controls += 1
        for target in targets:
            out = f'{work}/{label}.{target}.asm'
            dialect = {'z80asm': 'z88dk'}.get(target, target)
            c = run([args.zxasm, 'convert', original, '--codec', 'z80asm', '--to', dialect, '-o', out], work)
            if c.returncode != 0 or not os.path.exists(out):
                different[target].append(f'{label}: convert failed')
                continue
            got = assemble_sjasmplus(args.sjasmplus, out, work) if target == 'sjasmplus' else assemble_z80asm(args.z80asm, out, work)
            if got is None:
                different[target].append(f'{label}: does not build')
            elif got != want:
                different[target].append(f'{label}: other bytes ({got.hex()[:24]} vs {want.hex()[:24]})')
            else:
                equal[target] += 1
    print(f'cases z80asm builds to the expected bytes: {controls}')
    for target in targets:
        print(f'{target}: equal {equal[target]}, different {len(different[target])}')
        for d in different[target]:
            print('   ', d)
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if any(different.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
