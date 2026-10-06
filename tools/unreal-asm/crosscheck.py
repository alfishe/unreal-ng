#!/usr/bin/env python3
"""Compare targets of the dialect conversion on real disks: every main source of a set of TR-DOS images is converted to
sjasmplus and to the other targets (pasmo, z88dk's z80asm), each is assembled, and the memory each assembler built is
compared byte for byte (sjasmplus' as the reference: its conversions are checked against the original assemblers).

    crosscheck.py <out dir> [<image.trd|.scl>...] [--images-from LIST] [--targets pasmo,z88dk] [--zxasm PATH]
                  [--sjasmplus PATH] [--pasmo PATH] [--z80asm PATH]

A main source is a file no other file of its image INCLUDEs. Sources sjasmplus does not assemble without errors, or
whose sjasmplus form needs pages (DEVICE), are skipped. pasmo 0.5.5 writes an empty file when the code spans the whole
64K (its size wraps to 16 bits): build it with that one line fixed or such sources show as "pasmo wrote nothing".
z80asm writes one binary per section (every ORG opens one) and their addresses to its map: they are put together.
zxasm: --zxasm or UNREAL_ASM_ZXASM; sjasmplus: --sjasmplus or UNREAL_ASM_SJASMPLUS; pasmo: --pasmo or UNREAL_ASM_PASMO;
z80asm: --z80asm or UNREAL_ASM_Z80ASM.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zxdisk  # noqa: E402


def includes(directory, name):
    """The names a converted file INCLUDEs (lower case, without .asm)"""
    out = set()
    for line in open(os.path.join(directory, name), 'rb').read().split(b'\n'):
        m = re.match(rb'\s*INCLUDE\s+"([^"]+)"', line, re.I)
        if m:
            out.add(m.group(1).decode('latin1').lower().removesuffix('.asm'))
    return out


def text_with_includes(directory, name, seen=None):
    """The bytes of a converted file and every file it INCLUDEs"""
    seen = seen if seen is not None else set()
    path = os.path.join(directory, name)
    if name in seen or not os.path.exists(path):
        return b''
    seen.add(name)
    text = open(path, 'rb').read()
    for m in re.finditer(rb'INCLUDE\s+"([^"]+)"', text, re.I):
        text += text_with_includes(directory, m.group(1).decode('latin1'), seen)
    return text


def sjasmplus_image(sjasmplus, directory, name):
    """{address: byte} sjasmplus wrote for the source, or None when it reports errors or uses pages. Untouched device
    memory is not zero (system variables): the source is built over memory filled with #00 and with #FF, and the
    bytes equal in both are the ones it wrote"""
    if b'DEVICE' in open(os.path.join(directory, name), 'rb').read():
        return None
    images = []
    for fill in ('#00', '#FF'):
        harness = os.path.join(directory, '__harness.asm')
        open(harness, 'w').write(f'        DEVICE ZXSPECTRUM48\n        ORG 0\n        DS 32768,{fill}\n        DS 32768,{fill}\n        ORG 0\n'
                                 f'        INCLUDE "{name}"\n        SAVEBIN "__sj.bin",0,65536\n')
        run = subprocess.run([sjasmplus, '--nologo', '__harness.asm'], cwd=directory, capture_output=True, text=True, errors='replace')
        if run.returncode != 0 or 'error' in run.stdout.lower().replace('errors: 0', ''):
            return None
        images.append(open(os.path.join(directory, '__sj.bin'), 'rb').read())
    return {a: images[0][a] for a in range(65536) if images[0][a] == images[1][a]}


def pasmo_image(pasmo, directory, name):
    """(start, bytes) pasmo built, or an error text"""
    run = subprocess.run([pasmo, '-d', name, '__pasmo.bin'], cwd=directory, capture_output=True, text=True, errors='replace')
    errors = [line for line in run.stderr.split('\n') if line.startswith('ERROR')]
    if run.returncode != 0 or errors:
        return errors[0] if errors else 'pasmo failed'
    m = re.findall(r'Emiting raw binary from ([0-9A-F]{4}) to ([0-9A-F]{4})', run.stdout)
    data = open(os.path.join(directory, '__pasmo.bin'), 'rb').read()
    if not m or int(m[-1][0], 16) > int(m[-1][1], 16):
        return 0, b''   # no code (definitions only)
    if not data:
        return 'pasmo wrote nothing'
    return int(m[-1][0], 16), data


def z80asm_image(z80asm, directory, name):
    """{address: byte} z80asm built (its sections put together in the map's order), or an error text"""
    base = '__z80asm'
    for f in os.listdir(directory):
        if f.startswith(base):
            os.remove(os.path.join(directory, f))
    run = subprocess.run([z80asm, '-b', '-m', f'-o={base}.bin', name], cwd=directory, capture_output=True, text=True, errors='replace')
    errors = [line for line in (run.stdout + run.stderr).split('\n') if ': error:' in line]
    if run.returncode != 0 or errors:
        return errors[0] if errors else 'z80asm failed'
    sections = []
    for line in open(os.path.join(directory, base + '.map'), encoding='latin-1'):
        m = re.match(r'^__(\w*?)_?head\s*=\s*\$([0-9A-F]+)', line)
        if m:
            sections.append((m.group(1), int(m.group(2), 16)))
    # The sector tails of TASM's INCBIN (sections s_tail_n) first: the code after them writes over them
    sections.sort(key=lambda entry: not entry[0].startswith('s_tail_'))
    image = {}
    for section, address in sections:
        path = os.path.join(directory, base + ('.bin' if not section else '_' + section + '.bin'))
        if os.path.exists(path):
            for k, b in enumerate(open(path, 'rb').read()):
                image[(address + k) & 0xFFFF] = b
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('out')
    parser.add_argument('images', nargs='*')
    parser.add_argument('--images-from')
    parser.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM', 'zxasm'))
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--pasmo', default=os.environ.get('UNREAL_ASM_PASMO', 'pasmo'))
    parser.add_argument('--z80asm', default=os.environ.get('UNREAL_ASM_Z80ASM', 'z88dk-z80asm'))
    parser.add_argument('--targets', default='pasmo')
    args = parser.parse_args()
    if args.images_from:
        args.images += [line.rstrip('\n') for line in open(args.images_from) if line.strip()]
    os.makedirs(args.out, exist_ok=True)
    index = open(os.path.join(args.out, 'index.txt'), 'w')
    tally = collections.Counter()
    for n, image in enumerate(args.images, 1):
        index.write(f'{n} {image}\n')
        base = os.path.join(args.out, str(n))
        shutil.rmtree(base, ignore_errors=True)
        os.makedirs(base)
        source = image
        if image.lower().endswith('.scl'):
            source = os.path.join(base, 'image.trd')
            open(source, 'wb').write(zxdisk.scl2trd(open(image, 'rb').read()))
        dirs = {}
        targets = args.targets.split(',')
        for target in ['sjasmplus'] + targets:
            dirs[target] = os.path.join(base, target)
            subprocess.run([args.zxasm, 'convert', source, '--to', target, '-o', dirs[target]], capture_output=True)
        if not all(os.path.isdir(dirs[t]) for t in ['sjasmplus'] + targets):
            continue
        sources = sorted(f for f in os.listdir(dirs['sjasmplus']) if f.endswith('.asm'))
        included = set()
        for f in sources:
            included |= includes(dirs['sjasmplus'], f)
        for f in sources:
            if f.lower().removesuffix('.asm') in included:
                continue
            reference = sjasmplus_image(args.sjasmplus, dirs['sjasmplus'], f)
            if reference is None:
                tally['skipped (sjasmplus errors or pages)'] += 1
                continue
            for target in targets:
                if b'has none)' in text_with_includes(dirs[target], f):
                    tally[f'{target}: needs IFUSED (assembles every block)'] += 1
                    continue
                if target == 'pasmo':
                    built = pasmo_image(args.pasmo, dirs[target], f)
                    if not isinstance(built, str):
                        start, data = built
                        built = {start + k: b for k, b in enumerate(data)} if data else {}
                else:
                    built = z80asm_image(args.z80asm, dirs[target], f)
                if isinstance(built, str):
                    tally[f'{target}: errors'] += 1
                    print(f'{target} {n}/{f}: {built}')
                    continue
                if not built:
                    tally[f'{target}: no code'] += 1
                    continue
                differing = sum(1 for address, b in reference.items() if built.get(address, 0) != b)
                if differing:
                    tally[f'{target}: differ'] += 1
                    print(f'differ {target} {n}/{f}: {differing} of {len(reference)} bytes')
                else:
                    tally[f'{target}: equal'] += 1
    print('  '.join(f'{k}: {v}' for k, v in sorted(tally.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
