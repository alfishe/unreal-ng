#!/usr/bin/env python3
"""rasm's crunchers against unreal-asm's (include/unrealasm/compress.h): every input of the fixture set crunched by
rasm itself with every algorithm, from a crunched section (LZ48 ... LZCLOSE) and from a crunched include (INCL48 'file'),
and the bytes rasm emits compared with the fixtures unreal-asm-tests checks (CrunchFixtures.*).

    crunchcheck.py --rasm P [--update] [--fuzz N --seed S] [--tests P] [--only ALGO,...] [--keep]

  default     rasm's output for every fixture case against testdata/compress (a rasm that crunches differently shows up)
  --update    rewrite testdata/compress/expected and manifest.tsv from rasm (the inputs stay)
  --fuzz N    N more inputs (pseudo-random text, Z80-like bytes and runs, seed S), crunched by rasm into
              scratch/crunchcheck/fuzz; with --tests the unit tests run on that set (UNREAL_ASM_CRUNCH_FIXTURES)
  --tests P   unreal-asm-tests binary: run CrunchFixtures.* on the set checked (the fixtures, or the fuzz set)
  --only      algorithms to run (Lz4, Lz48, Lz49, Zx7, Zx0, Zx0Backward, Exomizer, ApUltra, Lzsa1, Lzsa2)

rasm: --rasm / UNREAL_ASM_RASM (rasm 3.3.x built with its third-party crunchers). Work files: scratch/crunchcheck.
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
FIXTURES = os.path.join(REPO, 'core', 'src', '3rdparty', 'unreal-asm', 'testdata', 'compress')
WORK = os.path.join(REPO, 'scratch', 'crunchcheck')

# Algorithm name (compress.h), section directive, include directive
ALGORITHMS = [
    ('Lz4', 'LZ4', 'INCLZ4'),
    ('Lz48', 'LZ48', 'INCL48'),
    ('Lz49', 'LZ49', 'INCL49'),
    ('Zx7', 'LZX7', 'INCZX7'),
    ('Zx0', 'LZX0', 'INCZX0'),
    ('Zx0Backward', 'LZX0B', 'INCZX0B'),
    ('Exomizer', 'LZEXO', 'INCEXO'),
    ('ApUltra', 'LZAPU', 'INCAPU'),
    ('Lzsa1', 'LZSA1', 'INCLZSA1'),
    ('Lzsa2', 'LZSA2', 'INCLZSA2'),
]

# Generated inputs (not stored): the unit tests build the same bytes (compress_test.cpp, GeneratedInput)
GENERATED = {
    '@big': ((b'Hello big world 0123456789 ' * 4000)[:100000]),
}
# rasm needs minutes on the 100K input with these (ZX7's optimiser, Exomizer's search): left out
GENERATED_SKIP = {'@big': {'Zx7', 'Exomizer'}}
# Sections are at most 64K: the generated input only goes through the includes
INCBIN_ONLY = {'@big'}
# Inputs whose outputs are kept as CRC-32 and size only (the fixtures stay small): the 20K ROM and lzDataTest.bin
HASHED = {'rom20k.bin', 'lzdatatest.bin'}
# LZSA sections with a minimum match argument (LZSA1 4 ... LZCLOSE), on these inputs
MINMATCH_CASES = {'text.bin': [('Lzsa1', 3), ('Lzsa1', 4), ('Lzsa2', 3), ('Lzsa2', 5)]}


def input_bytes(name, directory):
    if name in GENERATED:
        return GENERATED[name]
    with open(os.path.join(directory, 'inputs', name), 'rb') as f:
        return f.read()


def rasm_run(rasm, work, source):
    """Assembles source in work; (bytes or None, stdout+stderr, return code)"""
    with open(os.path.join(work, 'case.asm'), 'w') as f:
        f.write(source)
    out = os.path.join(work, 'case.bin')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([rasm, 'case.asm', '-o', 'case'], cwd=work, capture_output=True)
    text = (p.stdout + p.stderr).decode('latin-1')
    data = None
    if os.path.exists(out):
        with open(out, 'rb') as f:
            data = f.read()
    return data, text, p.returncode


def crunch_case(rasm, work, data, algorithm, origin, minmatch):
    """What rasm emits: (outcome, bytes). outcome: crunched, raw, reversed, empty, error, crash"""
    with open(os.path.join(work, 'in.bin'), 'wb') as f:
        f.write(data)
    name = next(a for a in ALGORITHMS if a[0] == algorithm)
    if origin == 'section':
        opener = name[1] + (' %d' % minmatch if minmatch else '')
        source = "org #4000\n%s\nincbin 'in.bin'\nlzclose\n" % opener
    else:
        source = "org #4000\n%s 'in.bin'\n" % name[2]
    out, text, code = rasm_run(rasm, work, source)
    if code < 0 or code in (134, 139):
        return 'crash', b''
    if 'error' in text.lower() and out is None:
        return 'error', b''
    if not data:
        return 'empty', b''
    if out is None:
        return 'error', b''
    if origin == 'section' and 'bigger than original' in text:
        if out == data:
            return 'raw', b''
        if out == data[::-1]:
            return 'reversed', b''
        raise RuntimeError('section kept but not as the input: %s %s' % (algorithm, text))
    return 'crunched', out


def cases_for(inputs, only):
    for name in inputs:
        for algorithm, _, _ in ALGORITHMS:
            if only and algorithm not in only:
                continue
            if algorithm in GENERATED_SKIP.get(name, ()):
                continue
            origins = ['incbin'] if name in INCBIN_ONLY else ['section', 'incbin']
            for origin in origins:
                yield name, algorithm, origin, 0
        for algorithm, minmatch in MINMATCH_CASES.get(name, []):
            if not only or algorithm in only:
                yield name, algorithm, 'section', minmatch


def origin_label(origin, minmatch):
    return origin + ('/minmatch=%d' % minmatch if minmatch else '')


def read_manifest(directory):
    rows = {}
    path = os.path.join(directory, 'manifest.tsv')
    if not os.path.exists(path):
        return rows
    for line in open(path):
        if line.startswith('#') or not line.strip():
            continue
        name, algorithm, origin, outcome, file = (line.rstrip('\n').split('\t') + [''])[:5]
        rows[(name, algorithm, origin)] = (outcome, file)
    return rows


def run_set(rasm, directory, inputs, only, write):
    """Crunches every case with rasm; write: store expected files and the manifest, else compare with them"""
    work = os.path.join(WORK, 'run')
    os.makedirs(work, exist_ok=True)
    expected_dir = os.path.join(directory, 'expected')
    old = read_manifest(directory)
    rows = []
    differences = 0
    for name, algorithm, origin, minmatch in cases_for(inputs, only):
        data = input_bytes(name, directory)
        outcome, out = crunch_case(rasm, work, data, algorithm, origin, minmatch)
        label = origin_label(origin, minmatch)
        file = ''
        if outcome == 'crunched' and name in HASHED:
            file = 'crc32:%08x:%d' % (zlib.crc32(out) & 0xFFFFFFFF, len(out))
        elif outcome == 'crunched':
            base = name.lstrip('@').rsplit('.', 1)[0]
            file = '%s.%s.bin' % (base, algorithm)
            if minmatch:
                file = '%s.%s.minmatch%d.bin' % (base, algorithm, minmatch)
            # one file for the section and the include when rasm emits the same bytes for both
            target = os.path.join(expected_dir, file)
            if write:
                if os.path.exists(target) and any(r[4] == file for r in rows):
                    with open(target, 'rb') as f:
                        if f.read() != out:
                            file = '%s.%s.%s.bin' % (base, algorithm, origin)
                            target = os.path.join(expected_dir, file)
                os.makedirs(expected_dir, exist_ok=True)
                with open(target, 'wb') as f:
                    f.write(out)
        rows.append((name, algorithm, label, outcome, file))
        if not write:
            want = old.get((name, algorithm, label))
            same = want is not None and want[0] == outcome
            if same and outcome == 'crunched' and want[1].startswith('crc32:'):
                same = want[1] == 'crc32:%08x:%d' % (zlib.crc32(out) & 0xFFFFFFFF, len(out))
            elif same and outcome == 'crunched':
                with open(os.path.join(expected_dir, want[1]), 'rb') as f:
                    same = f.read() == out
            if not same:
                differences += 1
                print('DIFF %s %s %s: rasm %s (%d bytes), fixture %s' % (name, algorithm, label, outcome, len(out),
                                                                       want[0] if want else 'missing'))
        print('%-16s %-12s %-20s %-9s %s' % (name, algorithm, label, outcome, len(out) if outcome == 'crunched' else ''))
    if write:
        # The manifest keeps rows of algorithms not rerun (--only)
        merged = {(r[0], r[1], r[2]): r for r in rows}
        for key, (outcome, file) in old.items():
            if key not in merged:
                merged[key] = (key[0], key[1], key[2], outcome, file)
        with open(os.path.join(directory, 'manifest.tsv'), 'w') as f:
            f.write('# input\talgorithm\torigin\toutcome\texpected file (crunched only); written by crunchcheck.py\n')
            for key in sorted(merged, key=lambda k: (k[0], [a[0] for a in ALGORITHMS].index(k[1]), k[2])):
                f.write('\t'.join(merged[key]) + '\n')
        used = {r[4] for r in merged.values() if r[4] and not r[4].startswith('crc32:')}
        for stale in os.listdir(expected_dir):
            if stale not in used:
                os.remove(os.path.join(expected_dir, stale))
    return differences


def fuzz_inputs(directory, count, seed):
    """Pseudo-random inputs that crunch: words, Z80-like code with repeats, runs, noise"""
    rng = random.Random(seed)
    os.makedirs(os.path.join(directory, 'inputs'), exist_ok=True)
    words = [b'LD', b'A,', b'(HL)', b'JR', b'NZ', b'loop', b'the', b'rasm', b'crunch', b' ', b'\n', b'0', b'#FF']
    names = []
    for k in range(count):
        size = rng.choice([1, 2, 3, 4, 5, 6, 17, 100, 255, 256, 257, 1000, 3000, 8000])
        kind = k % 4
        data = bytearray()
        while len(data) < size:
            if kind == 0:
                data += rng.choice(words)
            elif kind == 1:
                if data and rng.random() < 0.4:
                    start = rng.randrange(len(data))
                    data += data[start:start + rng.randrange(2, 40)]
                else:
                    data += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 8)))
            elif kind == 2:
                data += bytes([rng.randrange(256)]) * rng.randrange(1, 300)
            else:
                data += bytes([rng.randrange(256)])
        name = 'fuzz%03d.bin' % k
        with open(os.path.join(directory, 'inputs', name), 'wb') as f:
            f.write(bytes(data[:size]))
        names.append(name)
    return names


def run_tests(tests, directory):
    env = dict(os.environ, UNREAL_ASM_CRUNCH_FIXTURES=directory)
    p = subprocess.run([tests, '--gtest_filter=CrunchFixtures.*'], env=env)
    return p.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rasm', default=os.environ.get('UNREAL_ASM_RASM'))
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--fuzz', type=int, default=0)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--tests')
    ap.add_argument('--only', default='')
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.rasm:
        ap.error('--rasm (or UNREAL_ASM_RASM) is required')
    rasm = os.path.abspath(args.rasm)
    only = set(a for a in args.only.split(',') if a)

    if args.fuzz:
        directory = os.path.join(WORK, 'fuzz')
        shutil.rmtree(directory, ignore_errors=True)
        inputs = fuzz_inputs(directory, args.fuzz, args.seed)
        run_set(rasm, directory, inputs, only, write=True)
        differences = 0
    else:
        directory = FIXTURES
        inputs = sorted(os.listdir(os.path.join(directory, 'inputs'))) + sorted(GENERATED)
        differences = run_set(rasm, directory, inputs, only, write=args.update)
        if differences:
            print('%d case(s) differ from the fixtures' % differences)
    code = 1 if differences else 0
    if args.tests:
        code = run_tests(os.path.abspath(args.tests), directory) or code
    if not args.keep:
        shutil.rmtree(os.path.join(WORK, 'run'), ignore_errors=True)
    return code


if __name__ == '__main__':
    sys.exit(main())
