#!/usr/bin/env python3
"""pasmo sources through the pasmo frontend: every .asm of a directory is assembled by pasmo itself (--bin), converted by
`zxasm convert --codec pasmo` to sjasmplus, pasmo and z88dk's z80asm, each result assembled by its assembler, and the bytes
compared with pasmo's. Sources pasmo does not assemble are skipped; a target that does not build, or builds other bytes, is
a difference.

    pasmocheck.py <dir with .asm files> [--zxasm P] [--pasmo P] [--sjasmplus P] [--z80asm P] [--keep]

Tools: --zxasm / UNREAL_ASM_ZXASM, --pasmo / UNREAL_ASM_PASMO, --sjasmplus / UNREAL_ASM_SJASMPLUS, --z80asm / UNREAL_ASM_Z80ASM
(+ ZCCCFG). A target left out is skipped. Work files: scratch/pasmocheck.
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


def hex_image(path):
    """The 64K memory image of an Intel HEX file (pasmo --hex): pasmo fills the gaps between ORGs, sjasmplus' raw output does not"""
    image = bytearray(65536)
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        count, address, kind = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
        if kind == 0:
            for k in range(count):
                image[(address + k) & 0xFFFF] = int(line[9 + 2 * k:11 + 2 * k], 16)
    return bytes(image)


def same_span(got, want):
    """z80asm writes the code from its ORG on; the image holds it at its address: the same bytes from the first one pasmo emitted"""
    first = next((k for k, b in enumerate(want) if b), None)
    if first is None:
        return False
    last = max(k for k, b in enumerate(want) if b)
    return got.rstrip(b'\0') == want[first:last + 1].rstrip(b'\0') or got.rstrip(b'\0').endswith(want[first:last + 1].rstrip(b'\0'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('directory')
    for name in ('zxasm', 'pasmo', 'sjasmplus', 'z80asm'):
        ap.add_argument(f'--{name}', default=os.environ.get(f'UNREAL_ASM_{name.upper()}'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.zxasm or not args.pasmo:
        sys.exit('need --zxasm and --pasmo')
    work = os.path.abspath(os.path.join('scratch', 'pasmocheck'))
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(args.directory, work + '/src', ignore=shutil.ignore_patterns('*.o', '.git'))
    totals = {'skipped': 0}
    targets = [t for t in ('sjasmplus', 'pasmo', 'z80asm') if t == 'pasmo' or getattr(args, t)]
    equal = {t: 0 for t in targets}
    different = {t: [] for t in targets}
    src = work + '/src'
    for path in sorted(glob.glob(src + '/*.asm')):
        name = os.path.basename(path)
        ref = work + '/ref.bin'
        if os.path.exists(ref):
            os.remove(ref)
        ref = work + '/ref.hex'
        r = run([args.pasmo, '--hex', name, ref], src)
        if r.returncode != 0 or not os.path.exists(ref):
            totals['skipped'] += 1
            continue
        want = hex_image(ref)
        if not any(want):
            totals['skipped'] += 1
            continue
        for target in targets:
            out = f'{work}/{name}.{target}.asm'
            dialect = {'z80asm': 'z88dk'}.get(target, target)
            c = run([args.zxasm, 'convert', path, '--codec', 'pasmo', '--to', dialect, '-o', out], src)
            if c.returncode != 0 or not os.path.exists(out):
                different[target].append(f'{name}: convert failed')
                continue
            got_file = work + '/got.bin'
            if os.path.exists(got_file):
                os.remove(got_file)
            if target == 'sjasmplus':
                wrapper = f'{work}/{name}.wrap.asm'
                # sjasmplus' END stops the whole assembly (pasmo's only names the entry point): the SAVEBIN after the file would not run
                text = open(out, encoding='latin-1').read().splitlines()
                kept = [re.sub(r'^(?:(\S+:?)[ \t]+|[ \t]+)END\b[^;]*', lambda m: m.group(1) or '', l, flags=re.I) for l in text]
                open(out, 'w', encoding='latin-1').write('\n'.join(kept) + '\n')
                open(wrapper, 'w').write(f'        DEVICE NOSLOT64K\n        INCLUDE "{out}"\n        SAVEBIN "{got_file}",0,65536\n')
                b = run([args.sjasmplus, '--nologo', wrapper], src)
            elif target == 'pasmo':
                hex_out = got_file + '.hex'
                if os.path.exists(hex_out):
                    os.remove(hex_out)
                b = run([args.pasmo, '--hex', out, hex_out], src)
                if os.path.exists(hex_out):
                    open(got_file, 'wb').write(hex_image(hex_out))
            else:
                for old in glob.glob(f'{work}/{name}.z80asm*.bin'):
                    os.remove(old)
                b = run([args.z80asm, '-b', out], src)
                bins = sorted(glob.glob(f'{work}/{name}.z80asm*.bin'))
                bins = [b for b in bins if os.path.getsize(b)]   # the empty one is the section-less file
                if bins:
                    shutil.copyfile(bins[0], got_file)
            if not os.path.exists(got_file):
                message = (b.stdout + b.stderr).strip().splitlines()
                different[target].append(f'{name}: does not build ({message[-1][:90] if message else b.returncode})')
            elif (got := open(got_file, 'rb').read()) != want and not (target == 'z80asm' and same_span(got, want)):
                first = next(k for k in range(len(want)) if k >= len(got) or want[k] != got[k])
                different[target].append(f'{name}: other bytes (first at {first:04X}: pasmo {want[first]:02X}, got {got[first] if first < len(got) else -1:02X})')
            else:
                equal[target] += 1
    for target in targets:
        print(f'{target}: equal {equal[target]}, different {len(different[target])}')
        for d in different[target]:
            print('   ', d)
    print(f'skipped (pasmo builds nothing): {totals["skipped"]}')
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if any(different.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
