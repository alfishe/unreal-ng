#!/usr/bin/env python3
"""A dialect's sources through its frontend against the real assembler: zmac (the sources of unreal-asm's testdata/zmac, assembled
with zmac itself) and rasm (the decrunch routines of rasm's repository, each macro called once; assembled with rasm). Every source
is converted by `zxasm convert --codec <dialect>` to sjasmplus (and z80asm when given), assembled, and the bytes compared.

    dialectcheck.py zmac  <dir of .z files>    --zxasm P --zmac P --sjasmplus P [--z80asm P]
    dialectcheck.py rasm  <rasm dir>           --zxasm P --rasm P --sjasmplus P [--z80asm P]

Tools: --zxasm / UNREAL_ASM_ZXASM, --zmac / UNREAL_ASM_ZMAC, --rasm / UNREAL_ASM_RASM, --sjasmplus / UNREAL_ASM_SJASMPLUS,
--z80asm / UNREAL_ASM_Z80ASM (+ ZCCCFG). Work files: scratch/dialectcheck.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, capture_output=True, text=True)


def zmac_cases(directory):
    for path in sorted(glob.glob(os.path.join(directory, '*.z'))):
        yield os.path.basename(path), open(path, errors='replace').read()


def rasm_cases(directory):
    """Each decrunch source behind an ORG, with every macro it defines called once (a macro with parameters is left out)"""
    for path in sorted(glob.glob(os.path.join(directory, 'decrunch', '*.asm'))):
        text = open(path, errors='replace').read()
        calls = [m.group(1) for m in re.finditer(r'^\s*macro\s+(\w+)\s*$', text, re.I | re.M)]
        yield os.path.basename(path), ' org #4000\n' + text + '\n' + ''.join(f' {c}\n' for c in calls)


def oracle(name, args, source, work):
    out = work + '/ref.bin'
    for old in glob.glob(work + '/ref*'):
        os.remove(old)
    if name == 'zmac':
        run([args.zmac, '-z', '--od', work + '/zout', '--oo', 'cim', source], work)
        produced = os.path.join(work, 'zout', os.path.splitext(os.path.basename(source))[0] + '.cim')
    else:
        run([args.rasm, source, '-o', work + '/ref'], work)
        produced = work + '/ref.bin'
    return open(produced, 'rb').read() if os.path.exists(produced) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dialect', choices=('zmac', 'rasm'))
    ap.add_argument('directory')
    for name in ('zxasm', 'zmac', 'rasm', 'sjasmplus', 'z80asm'):
        ap.add_argument(f'--{name}', default=os.environ.get(f'UNREAL_ASM_{name.upper()}'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not getattr(args, args.dialect):
        sys.exit(f'need --zxasm and --{args.dialect}')
    work = os.path.abspath(os.path.join('scratch', 'dialectcheck'))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    cases = zmac_cases(args.directory) if args.dialect == 'zmac' else rasm_cases(args.directory)
    ext = '.z' if args.dialect == 'zmac' else '.asm'
    targets = [t for t in ('sjasmplus', 'z80asm') if getattr(args, t)]
    equal = {t: 0 for t in targets}
    different = {t: [] for t in targets}
    skipped = 0
    for name, text in cases:
        source = f'{work}/{name}'
        open(source, 'w', errors='replace').write(text)
        want = oracle(args.dialect, args, source, work)
        if not want:
            skipped += 1
            continue
        for target in targets:
            out = f'{work}/{name}.{target}.asm'
            dialect = {'z80asm': 'z88dk'}.get(target, target)
            c = run([args.zxasm, 'convert', source, '--codec', args.dialect, '--to', dialect, '-o', out], work)
            if c.returncode != 0 or not os.path.exists(out):
                different[target].append(f'{name}: convert failed')
                continue
            got_file = work + '/got.bin'
            for old in glob.glob(work + '/got*') + glob.glob(f'{work}/{name}.{target}*.bin'):
                os.remove(old)
            if target == 'sjasmplus':
                b = run([args.sjasmplus, '--nologo', f'--raw={got_file}', out], work)
            else:
                b = run([args.z80asm, '-b', out], work)
                bins = [x for x in sorted(glob.glob(f'{work}/{name}.{target}*.bin')) if os.path.getsize(x)]
                if bins:
                    shutil.copyfile(bins[0], got_file)
            if not os.path.exists(got_file):
                message = (b.stdout + b.stderr).strip().splitlines()
                different[target].append(f'{name}: does not build ({message[-1][:80] if message else b.returncode})')
            elif open(got_file, 'rb').read() != want:
                different[target].append(f'{name}: other bytes ({len(open(got_file, "rb").read())} vs {len(want)})')
            else:
                equal[target] += 1
    for target in targets:
        print(f'{target}: equal {equal[target]}, different {len(different[target])}')
        for d in different[target]:
            print('   ', d)
    print(f'skipped (the assembler builds nothing from it): {skipped}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if any(different.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
