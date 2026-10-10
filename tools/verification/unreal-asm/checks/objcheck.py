#!/usr/bin/env python3
"""Assemble a converted ALASM project as it is and compare its objects with the files ALASM saved on the same disk.

    objcheck.py <converted dir> <image.trd> [--sjasmplus PATH]

<converted dir> is what `zxasm convert <image.trd> --to sjasmplus -o <dir>` wrote. Every main source with an ALASM
SAVEOBJ table (ObjTab: DB 'NAME....T', DW begin, DW length, DB page, DW start) ends with the SAVEBIN lines the
conversion writes for it; sjasmplus assembles the source unchanged (in a folder of its own, so the disk's files next
to the sources stay as they are), must report no warning, and each object is compared with the disk's file of that
name. sjasmplus: --sjasmplus or UNREAL_ASM_SJASMPLUS.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import zxdisk  # noqa: E402


def read_text(path):
    data = open(path, 'rb').read()
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError:
        return data.decode('cp866')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dir')
    parser.add_argument('image')
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    args = parser.parse_args()

    source_dir = os.path.abspath(args.dir)
    disk = {(name, type_): data[:length] for name, type_, start, length, data in zxdisk.files(open(args.image, 'rb').read())}
    total = equal = units = clean = 0
    for f in sorted(os.listdir(source_dir)):
        if not f.endswith('.asm'):
            continue
        lines = read_text(os.path.join(source_dir, f)).split('\n')
        if any('ALASM MAIN' in line for line in lines):
            continue   # a part of a project: assembled through its main source
        saves = [m.groups() for m in (re.match(r'\s+SAVEBIN\s+"([^"]+)"', line) for line in lines) if m]
        if not saves:
            continue
        units += 1
        out = tempfile.mkdtemp(prefix='objcheck_')
        run = subprocess.run([args.sjasmplus, '--nologo', '-I' + source_dir, os.path.join(source_dir, f)], cwd=out,
                             capture_output=True, text=True, errors='replace', timeout=120)
        log = run.stdout + run.stderr
        summary = re.search(r'Errors: (\d+), warnings: (\d+)', log)
        errors, warnings = (int(summary.group(1)), int(summary.group(2))) if summary else (-1, -1)
        clean += errors == 0 and warnings == 0
        print(f'{f:12} sjasmplus: {errors} errors, {warnings} warnings')
        if warnings:
            for line in log.split('\n'):
                if 'warning' in line and 'warnings:' not in line:
                    print('             ' + line.strip())
        for (host,) in saves:
            total += 1
            name, _, type_ = host.partition('.')
            type_ = type_ or 'C'
            path = os.path.join(out, host)
            mine = open(path, 'rb').read() if os.path.exists(path) else None
            reference = disk.get((name, type_))
            if reference is None:
                total -= 1   # nothing to compare with (a file the program makes when it runs: 16CCON's creds)
                print(f'{f:12} {name}.{type_:2} ALASM file not on the disk')
                continue
            if mine is None:
                print(f'{f:12} {name}.{type_:2} not built')
                continue
            same = mine == reference
            equal += same
            diff = sum(1 for a, b in zip(mine, reference) if a != b) + abs(len(mine) - len(reference))
            print(f'{f:12} {name}.{type_:2} {len(reference):6} bytes  ' + ('EQUAL' if same else f'differs: {diff} bytes (ours {len(mine)})'))
        shutil.rmtree(out)
    print(f'objects equal: {equal} of {total}; units without errors or warnings: {clean} of {units}')
    return 0 if equal == total and clean == units else 1


if __name__ == '__main__':
    sys.exit(main())
