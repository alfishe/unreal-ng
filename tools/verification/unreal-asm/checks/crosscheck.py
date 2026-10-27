#!/usr/bin/env python3
"""Compare targets of the dialect conversion on real disks: every main source of a set of TR-DOS images is converted to
sjasmplus and to the other targets (pasmo, z88dk's z80asm), each is assembled, and the memory each assembler built is
compared byte for byte (sjasmplus' as the reference: its conversions are checked against the original assemblers).

    crosscheck.py <out dir> [<image.trd|.scl>...] [--images-from LIST] [--targets pasmo,z88dk] [--disk-sets]
                  [--zxasm PATH] [--sjasmplus PATH] [--pasmo PATH] [--z80asm PATH]

A main source is a file no other file of its image INCLUDEs. Sources sjasmplus does not assemble without errors, or
whose sjasmplus form needs pages (a DEVICE other than NOSLOT64K, the flat 64K SAVEBIN needs), are skipped. pasmo
0.5.5 writes an empty file when the code spans the whole 64K (its size wraps to 16 bits): build it with that one line
fixed or such sources show as "pasmo wrote nothing".
--disk-sets: the images of one directory are one set of disks, converted with zxasm convert --with: a file a source
INCLUDEs that its disk lacks comes from another disk of the set (ZAsm's own sources on C33_F9.trd include ovldef, and
its macros, from the program disk Z33_F9.trd).
z80asm writes one binary per section (every ORG opens one) and their addresses to its map: they are put together.
The original assembler's objects: a main source whose sjasmplus form SAVEBINs a file its disk holds (ALASM's object
saver: the conversion writes a SAVEBIN for each entry of its ObjTab) is assembled as it is, in its own DEVICE with its
pages, and each object compared with the disk's file of that name (the last of that name, as TR-DOS finds it). Sources
with pages are compared so too.
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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import zxdisk  # noqa: E402

TIMEOUT = 60   # seconds an assembler may take on one source


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
    bytes equal in both are the ones it wrote. A source that says DEVICE NOSLOT64K (no pages: for its SAVEBIN) is
    built in that device"""
    devices = set(re.findall(rb'^\s*DEVICE\s+(\w+)', text_with_includes(directory, name), re.M | re.I))
    if {d.upper() for d in devices} - {b'NOSLOT64K'}:
        return None
    device = 'NOSLOT64K' if devices else 'ZXSPECTRUM48'
    images = []
    for fill in ('#00', '#FF'):
        harness = os.path.join(directory, '__harness.asm')
        open(harness, 'w').write(f'        DEVICE {device}\n        ORG 0\n        DS 32768,{fill}\n        DS 32768,{fill}\n        ORG 0\n'
                                 f'        INCLUDE "{name}"\n        SAVEBIN "__sj.bin",0,65536\n')
        run = subprocess.run([sjasmplus, '--nologo', '__harness.asm'], cwd=directory, capture_output=True, text=True, errors='replace')
        if run.returncode != 0 or 'error' in run.stdout.lower().replace('errors: 0', ''):
            return None
        images.append(open(os.path.join(directory, '__sj.bin'), 'rb').read())
    return {a: images[0][a] for a in range(65536) if images[0][a] == images[1][a]}


def disk_files(image):
    """{(name, type): (length, sectors)} of an image's files (of a name given twice, the last)"""
    return {(name, type_): (length, data) for name, type_, start, length, data in zxdisk.files(open(image, 'rb').read())}


def original_objects(sjasmplus, directory, name, disk):
    """(name, None or the bytes differing) for each SAVEBIN of the source naming a file of its disk, or an error text;
    None when it names none. sjasmplus assembles the source in a folder of its own (objects/<source>), so the files
    next to the sources stay as they are"""
    saves = []
    for host in re.findall(rb'^\s+SAVEBIN\s+"([^"]+)"', text_with_includes(directory, name), re.M | re.I):
        stem, _, type_ = host.decode('latin1').partition('.')
        if (stem, type_ or 'C') in disk:
            saves.append((host.decode('latin1'), disk[(stem, type_ or 'C')]))
    if not saves:
        return None
    out = os.path.join(os.path.dirname(directory), 'objects', name.removesuffix('.asm'))
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    try:
        run = subprocess.run([sjasmplus, '--nologo', '-I' + os.path.abspath(directory), os.path.abspath(os.path.join(directory, name))], cwd=out,
                             capture_output=True, text=True, errors='replace', timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return f'sjasmplus did not end in {TIMEOUT} s'
    # The object saver's own code (SAVEOBJ*, run in ALASM) may be missing from the disk: the SAVEBINs stand for it
    errors = [line for line in (run.stdout + run.stderr).split('\n') if ': error' in line and 'opening file: SAVEOBJ' not in line]
    if errors or not re.search(r'Errors: \d+', run.stdout + run.stderr):
        return errors[0].strip() if errors else 'sjasmplus failed'
    result = []
    for host, (length, sectors) in saves:
        path = os.path.join(out, host)
        if not os.path.exists(path):
            result.append((host, 'not written'))
            continue
        mine = open(path, 'rb').read()
        # A BASIC file's catalog length is the program's (ZX-ASM's SAVE of TELNUM.B: 51 of the 256 bytes it wrote)
        reference = sectors[:len(mine)] if host.upper().endswith('.B') and length <= len(mine) <= len(sectors) else sectors[:length]
        differing = sum(1 for a, b in zip(mine, reference) if a != b) + abs(len(mine) - len(reference))
        result.append((host, differing or None))
    return result


def pasmo_image(pasmo, directory, name):
    """(start, bytes) pasmo built, or an error text"""
    try:
        run = subprocess.run([pasmo, '-d', name, '__pasmo.bin'], cwd=directory, capture_output=True, text=True, errors='replace', timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return f'pasmo did not end in {TIMEOUT} s'
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
    try:   # z80asm's DEFL is a text macro: names assigned from each other may never end (a warning of the backend)
        run = subprocess.run([z80asm, '-b', '-m', f'-o={base}.bin', name], cwd=directory, capture_output=True, text=True, errors='replace',
                             timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return f'z80asm did not end in {TIMEOUT} s'
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


def convert(zxasm, image, base, targets, others=()):
    """{target: directory} of an image converted to sjasmplus and the targets, under base (others: the other disks of
    its set, zxasm convert --with)"""
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base)
    source = image
    if image.lower().endswith('.scl'):
        source = os.path.join(base, 'image.trd')
        open(source, 'wb').write(zxdisk.scl2trd(open(image, 'rb').read()))
    with_disks = []
    for k, other in enumerate(others):
        if other.lower().endswith('.scl'):
            path = os.path.join(base, f'with{k}.trd')
            open(path, 'wb').write(zxdisk.scl2trd(open(other, 'rb').read()))
            other = path
        with_disks += ['--with', other]
    dirs = {}
    for target in ['sjasmplus'] + targets:
        dirs[target] = os.path.join(base, target)
        subprocess.run([zxasm, 'convert', source, '--to', target, '-o', dirs[target]] + with_disks, capture_output=True)
    return dirs


def disk_set(image):
    """The other images in the directory of an image (one set of disks)"""
    directory = os.path.dirname(image) or '.'
    return sorted(os.path.join(os.path.dirname(image), f) for f in os.listdir(directory)
                  if f.lower().endswith(('.trd', '.scl')) and f != os.path.basename(image))


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
    parser.add_argument('--disk-sets', action='store_true')
    args = parser.parse_args()
    if args.images_from:
        args.images += [line.rstrip('\n') for line in open(args.images_from) if line.strip()]
    os.makedirs(args.out, exist_ok=True)
    index = open(os.path.join(args.out, 'index.txt'), 'w')
    tally = collections.Counter()
    for n, image in enumerate(args.images, 1):
        index.write(f'{n} {image}\n')
        targets = args.targets.split(',')
        dirs = convert(args.zxasm, image, os.path.join(args.out, str(n)), targets, disk_set(image) if args.disk_sets else ())
        if not all(os.path.isdir(dirs[t]) for t in ['sjasmplus'] + targets):
            continue
        sources = sorted(f for f in os.listdir(dirs['sjasmplus']) if f.endswith('.asm'))
        disk = disk_files(image)
        included = set()
        for f in sources:
            included |= includes(dirs['sjasmplus'], f)
        for f in sources:
            if f.lower().removesuffix('.asm') in included:
                continue
            objects = original_objects(args.sjasmplus, dirs['sjasmplus'], f, disk)
            if isinstance(objects, str):
                tally['original objects: sjasmplus errors'] += 1
                print(f'objects {n}/{f}: {objects}')
            for host, differing in objects if isinstance(objects, list) else []:
                tally['original objects: ' + ('equal' if differing is None else 'differ')] += 1
                if differing is not None:
                    print(f'differ original {n}/{f} {host}: {differing}' + (' bytes' if isinstance(differing, int) else ''))
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
                if not built and not reference:
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
