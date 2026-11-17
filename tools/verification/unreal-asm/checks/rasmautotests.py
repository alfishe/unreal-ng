#!/usr/bin/env python3
"""rasm's own autotest snippets as a corpus for unreal-asm's rasm engine.

rasm.c holds a few hundred `#define AUTOTEST_NAME "source"` snippets that its `-autotest` run assembles. This script pulls them
out through the C preprocessor (so string concatenation and escapes come out exactly as rasm sees them), assembles each one with
the rasm binary and writes a corpus directory:

    <out>/<NAME>.asm        the snippet
    <out>/<NAME>.bin        what rasm produced (absent when rasm reported an error or produced nothing)
    <out>/index.tsv         NAME, rasm's exit code, output size, the first error line

    rasmautotests.py --rasm-source /path/to/rasm.c --rasm /path/to/rasm --out DIR [--cc cc]

Snippets that need files the C test code writes first (includes, tiles, ...) fail here and are listed as errors.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


def defines(text):
    """The #define AUTOTEST_* blocks with their continuation lines"""
    lines = text.split('\n')
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if re.match(r'\s*#define\s+AUTOTEST_\w+', line):
            block = [line]
            while block[-1].rstrip().endswith('\\') and i + 1 < len(lines):
                i += 1
                block.append(lines[i])
            out.append('\n'.join(block))
        i += 1
    return out


def snippets(source, cc, work):
    blocks = defines(open(source, encoding='latin-1').read())
    names = [re.match(r'\s*#define\s+(AUTOTEST_\w+)', b).group(1) for b in blocks]
    program = ['#include <stdio.h>', '#include <string.h>']
    program += blocks
    program.append('static void put(const char *n, const char *s) { size_t l = strlen(s); printf("%s %zu\\n", n, l); '
                   'fwrite(s, 1, l, stdout); printf("\\n"); }')
    program.append('int main(void) {')
    for n in names:
        program.append(f'#ifdef {n}\n  put("{n}", {n});\n#endif')
    program.append('return 0; }')
    c = os.path.join(work, 'autotests.c')
    with open(c, 'w', encoding='latin-1') as f:
        f.write('\n'.join(program))
    exe = os.path.join(work, 'autotests')
    r = subprocess.run([cc, '-w', '-o', exe, c], capture_output=True, text=True)
    if r.returncode:
        sys.exit('cc failed:\n' + r.stderr[:4000])
    data = subprocess.run([exe], capture_output=True).stdout
    pos = 0
    result = []
    while pos < len(data):
        nl = data.index(b'\n', pos)
        name, size = data[pos:nl].decode().split()
        size = int(size)
        body = data[nl + 1:nl + 1 + size]
        pos = nl + 1 + size + 1
        result.append((name, body))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rasm-source', required=True)
    ap.add_argument('--rasm', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--cc', default='cc')
    args = ap.parse_args()
    rasm = os.path.abspath(args.rasm)
    out = os.path.abspath(args.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    with tempfile.TemporaryDirectory(dir=os.path.dirname(out)) as work:
        items = snippets(args.rasm_source, args.cc, work)
        index = []
        for name, body in items:
            src = os.path.join(out, name + '.asm')
            with open(src, 'wb') as f:
                f.write(body)
            run = os.path.join(work, 'run')
            shutil.rmtree(run, ignore_errors=True)
            os.makedirs(run)
            shutil.copy(src, os.path.join(run, 'a.asm'))
            try:
                r = subprocess.run([rasm, 'a.asm', '-o', 'a'], cwd=run, capture_output=True, timeout=20)
                code = r.returncode
                log = (r.stdout + r.stderr).decode('latin-1')
            except subprocess.TimeoutExpired:
                code, log = 'timeout', ''
            produced = os.path.join(run, 'a.bin')
            size = ''
            if code == 0 and os.path.exists(produced):
                shutil.copy(produced, os.path.join(out, name + '.bin'))
                size = os.path.getsize(produced)
            err = next((re.sub(r'\x1b\[[0-9;]*m', '', l).strip() for l in log.split('\n') if 'error' in l.lower()), '')
            index.append(f'{name}\t{code}\t{size}\t{err}')
        with open(os.path.join(out, 'index.tsv'), 'w') as f:
            f.write('\n'.join(index) + '\n')
    ok = sum(1 for l in index if l.split('\t')[2])
    print(f'{len(index)} snippets, {ok} assembled with output, {len(index) - ok} errors or no output -> {out}')


if __name__ == '__main__':
    main()
