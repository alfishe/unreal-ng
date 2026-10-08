#!/usr/bin/env python3
"""Compare the objects a converted ALASM project builds with the files ALASM saved on the same disk.

    objcheck.py <converted dir> <image.trd> [--sjasmplus PATH]

<converted dir> is what `zxasm convert <image.trd> --to sjasmplus -o <dir>` wrote. Every main source with an ALASM
SAVEOBJ table (ObjTab: DB 'NAME....T', DW begin, DW length, DB page, DW start) gets a harness that SAVEBINs each
object from its page; sjasmplus assembles it and each object is compared with the disk's file of that name.
sjasmplus: --sjasmplus or UNREAL_ASM_SJASMPLUS.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import zxdisk  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dir')
    parser.add_argument('image')
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    args = parser.parse_args()

    disk = {(name, type_): data[:length] for name, type_, start, length, data in zxdisk.files(open(args.image, 'rb').read())}
    total = equal = 0
    for f in sorted(os.listdir(args.dir)):
        if not f.endswith('.asm') or f.startswith('harness_'):
            continue
        lines = open(os.path.join(args.dir, f), 'rb').read().decode('cp866').split('\n')
        if any('ALASM MAIN' in line for line in lines):
            continue   # a part of a project: assembled through its main source
        try:
            k = next(i for i, line in enumerate(lines) if line.strip().startswith('ObjTab'))
        except StopIteration:
            continue
        values = []
        for line in lines[k + 1:]:
            if not line.strip() or line.strip().startswith(';'):
                continue
            m = re.match(r"\s+(DB|DW)\s+(.*?)\s*(;.*)?$", line)
            if not m:
                break
            values.append((m.group(1), m.group(2)))
        entries = []
        i = 0
        while i + 4 < len(values) and values[i][0] == 'DB' and values[i][1].startswith("'"):
            name = values[i][1].strip("'")
            entries.append((name[:8].rstrip(), name[8:9] or 'C', values[i + 1][1], values[i + 2][1], values[i + 3][1]))
            i += 5
        if not entries:
            continue
        harness = [f'        INCLUDE "{f}"', '        SLOT 3']
        for n, (name, type_, begin, length, page) in enumerate(entries):
            harness += [f'        PAGE {page}', f'        SAVEBIN "obj_{n}.bin",{begin},{length}']
        open(os.path.join(args.dir, 'harness_' + f), 'w').write('\n'.join(harness) + '\n')
        run = subprocess.run([args.sjasmplus, '--nologo', 'harness_' + f], cwd=args.dir, capture_output=True, text=True, errors='replace', timeout=120)
        errors = [line for line in (run.stdout + run.stderr).split('\n') if 'error' in line and 'complete' not in line]
        for n, (name, type_, begin, length, page) in enumerate(entries):
            total += 1
            path = os.path.join(args.dir, f'obj_{n}.bin')
            mine = open(path, 'rb').read() if os.path.exists(path) else None
            reference = disk.get((name, type_))
            if reference is None:
                print(f'{f:12} {name}.{type_:2} ALASM file not on the disk')
                continue
            if mine is None:
                print(f'{f:12} {name}.{type_:2} not built ({errors[:1]})')
                continue
            same = mine == reference
            equal += same
            diff = sum(1 for a, b in zip(mine, reference) if a != b) + abs(len(mine) - len(reference))
            print(f'{f:12} {name}.{type_:2} {len(reference):6} bytes  ' + ('EQUAL' if same else f'differs: {diff} bytes (ours {len(mine)})'))
            os.remove(path)
    print(f'objects equal: {equal} of {total}')
    return 0 if equal == total else 1


if __name__ == '__main__':
    sys.exit(main())
