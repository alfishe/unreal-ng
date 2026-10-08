#!/usr/bin/env python3
"""zasm sources through the zasm frontend: every .asm of the Test and Examples folders of Megatokio's zasm (without #include
and without a blank in its path) is assembled by zasm itself (-b, a binary file), converted by `zxasm convert --codec zasm`
to sjasmplus and to z80asm, each assembled, and the bytes compared with zasm's.

    zasmcheck.py <zasm dir> [--zxasm P] [--zasm P] [--sjasmplus P] [--z80asm P] [--keep]

Tools: --zxasm / UNREAL_ASM_ZXASM, --zasm / UNREAL_ASM_ZASM, --sjasmplus / UNREAL_ASM_SJASMPLUS, --z80asm / UNREAL_ASM_Z80ASM
(+ ZCCCFG). Work files: scratch/zasmcheck.
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


def same(got, want):
    """Equal bytes; zasm pads a #code segment to its declared size with the fill byte (0 or FF), which the other assemblers leave out"""
    return got == want or (len(got) < len(want) and want[:len(got)] == got and set(want[len(got):]) <= {0x00, 0xFF})


def where(sjasmplus, converted, work, got, want):
    """The line of the converted source that emitted the first byte differing from zasm's (from sjasmplus' listing)"""
    first = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
    listing = f'{work}/where.lst'
    run([sjasmplus, '--nologo', '--zxnext', f'--lst={listing}', f'--raw={work}/where.bin', converted], work)
    emitted = 0
    for line in open(listing, errors='replace'):
        m = re.match(r'\s*(\d+)\+*\s+[0-9A-F]{4}\s+((?:[0-9A-F]{2})+)(?:\s|$)(.*)', line)
        if m:
            size = len(m.group(2)) // 2
            if emitted + size > first:
                return f'byte {first} at line {m.group(1)}: {m.group(3).strip()[:60]} (zasm {want[first]:02X}, got {got[first]:02X})' if first < min(len(got), len(want)) else f'size {len(got)} vs {len(want)}'
            emitted += size
    return f'size {len(got)} vs {len(want)}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('zasmdir')
    for name in ('zxasm', 'zasm', 'sjasmplus', 'z80asm'):
        ap.add_argument(f'--{name}', default=os.environ.get(f'UNREAL_ASM_{name.upper()}'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.zasm:
        sys.exit('need --zxasm and --zasm')
    work = os.path.abspath(os.path.join('scratch', 'zasmcheck'))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    targets = [t for t in ('sjasmplus', 'z80asm') if getattr(args, t)]
    equal = {t: 0 for t in targets}
    different = {t: [] for t in targets}
    skipped = 0
    sources = sorted(glob.glob(os.path.join(args.zasmdir, 'Test', '**', '*.asm'), recursive=True) + glob.glob(os.path.join(args.zasmdir, 'Examples', '*.asm')))
    for path in sources:
        label = os.path.relpath(path, args.zasmdir).replace('/', '_')
        text = open(path, errors='replace').read()
        if ' ' in label or re.search(r'^\s*[#.]?include\b', text, re.I | re.M):
            skipped += 1
            continue
        # outputs that are not a flat binary of the code (tape / snapshot / machine files, a rom whose DS fills with FF), the 8080 and
        # Z180 modes and the options a shebang line or --flatops set
        target = re.search(r'^\s*#target\s+(\w+)', text, re.I | re.M)
        if (target and target.group(1).lower() not in ('bin', 'ram')) or re.search(r'8080|z180|flatops|asm8080', label + text[:200], re.I):
            skipped += 1
            continue
        src = f'{work}/{label}'
        shutil.copyfile(path, src)
        ref = f'{work}/ref.bin'
        if os.path.exists(ref):
            os.remove(ref)
        run([args.zasm, '-b', src, ref], work)
        if not os.path.exists(ref) or os.path.getsize(ref) == 0:
            skipped += 1
            continue
        want = open(ref, 'rb').read()
        for target in targets:
            out = f'{work}/{label}.{target}.asm'
            dialect = {'z80asm': 'z88dk'}.get(target, target)
            c = run([args.zxasm, 'convert', src, '--codec', 'zasm', '--to', dialect, '-o', out], work)
            if c.returncode != 0 or not os.path.exists(out):
                different[target].append(f'{label}: convert failed')
                continue
            got_file = f'{work}/got.bin'
            for old in glob.glob(f'{work}/got*') + glob.glob(f'{work}/{label}.{target}*.bin'):
                os.remove(old)
            if target == 'sjasmplus':
                b = run([args.sjasmplus, '--nologo', '--zxnext', f'--raw={got_file}', out], work)
            else:
                b = run([args.z80asm, '-b', out], work)
                bins = sorted(glob.glob(f'{work}/{label}.{target}*.bin'))
                bins = [b for b in bins if os.path.getsize(b)]   # the empty one is the section-less file
                if bins:
                    shutil.copyfile(bins[0], got_file)
            if not os.path.exists(got_file):
                message = (b.stdout + b.stderr).strip().splitlines()
                different[target].append(f'{label}: does not build ({message[-1][:80] if message else b.returncode})')
            elif not same(open(got_file, 'rb').read(), want):
                detail = where(args.sjasmplus, out, work, open(got_file, 'rb').read(), want) if target == 'sjasmplus' else ''
                different[target].append(f'{label}: other bytes {detail}')
            else:
                equal[target] += 1
    for target in targets:
        print(f'{target}: equal {equal[target]}, different {len(different[target])}')
        for d in different[target]:
            print('   ', d)
    print(f'skipped (an #include, a blank in the path, or zasm builds nothing): {skipped}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if any(different.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
