#!/usr/bin/env python3
"""The (source, bytes) pairs of z88dk's z80asm test suite (src/z80asm/t/*.t): every `z80asm_ok("", "", "", <<END, bytes(...))`
call with no assembler options and plain numeric bytes. Used as the oracle of the z80asm frontend: the bytes the original
test expects against what the converted source assembles to.

    z80asmcases.py <z88dk dir> [--dump DIR]    list the cases (count), or write them as DIR/<test>-<n>.asm + .bin
"""
import argparse
import glob
import os
import re
import sys

CALL = re.compile(r'z80asm_ok\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*<<\s*([\'"]?)(\w+)\4\s*,\s*bytes\(([^)]*)\)\s*\)\s*;')


def cases(root):
    for path in sorted(glob.glob(os.path.join(root, 'src', 'z80asm', 't', '*.t'))):
        text = open(path, errors='replace').read()
        lines = text.split('\n')
        i = 0
        while i < len(lines):
            line = lines[i]
            m = re.match(r'\s*z80asm_ok\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*<<\s*([\'"]?)(\w+)\4\s*,\s*bytes\(([^)]*)\)\s*\)\s*;', line)
            if not m:
                i += 1
                continue
            options = m.group(1) + m.group(2) + m.group(3)
            tag = m.group(5)
            body = []
            i += 1
            while i < len(lines) and lines[i] != tag:
                body.append(lines[i])
                i += 1
            i += 1
            args = [a.strip() for a in m.group(6).split(',') if a.strip()]
            try:
                data = bytes(int(a, 0) & 0xFF if re.fullmatch(r'-?(0x[0-9a-fA-F]+|\d+)', a) else (_ for _ in ()).throw(ValueError) for a in args)
            except ValueError:
                continue
            if options.strip() or m.group(4) == '"' and ('$' in '\n'.join(body) or '@' in '\n'.join(body)):
                continue   # an assembler option, or an interpolated Perl variable
            yield os.path.basename(path)[:-2], '\n'.join(body) + '\n', data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('z88dk')
    ap.add_argument('--dump')
    args = ap.parse_args()
    found = list(cases(args.z88dk))
    print(len(found), 'cases from', len({n for n, _, _ in found}), 'tests')
    if args.dump:
        os.makedirs(args.dump, exist_ok=True)
        count = {}
        for name, source, data in found:
            count[name] = count.get(name, 0) + 1
            stem = os.path.join(args.dump, f'{name}-{count[name]}')
            open(stem + '.asm', 'w').write(source)
            open(stem + '.bin', 'wb').write(data)


if __name__ == '__main__':
    main()
