#!/usr/bin/env python3
"""Which assemblers build the Z80N instructions, and to the same bytes: every instruction of the set is assembled alone by
each assembler given and its bytes are compared with sjasmplus' (--zxnext, the reference the Next test suite is built with).

    z80nmatrix.py [--sjasmplus P] [--z80asm P] [--pasmo P] [--fantasm P] [--zasm P] [--keep]

Paths also come from UNREAL_ASM_SJASMPLUS / _Z80ASM (+ ZCCCFG) / _PASMO / _FANTASM / _ZASM. A tool left out is skipped. Each
instruction is written in the spelling all of these take (decimal numbers, `mirror a`, `mul d,e`, `bsla de,b`). Prints one
row per instruction (OK, a different byte string, or the tool's error) and a count per tool. Work files: scratch/z80nmatrix.
"""
import argparse
import os
import shutil
import subprocess
import sys

# (source line, ED opcode) for the whole set: 29 instructions
SET = [
    'swapnib', 'mirror a', 'test 85', 'bsla de,b', 'bsra de,b', 'bsrl de,b', 'bsrf de,b', 'brlc de,b', 'mul d,e', 'add hl,a',
    'add de,a', 'add bc,a', 'add hl,4660', 'add de,4660', 'add bc,4660', 'push 4660', 'outinb', 'nextreg 7,3', 'nextreg 7,a',
    'pixeldn', 'pixelad', 'setae', 'jp (c)', 'ldix', 'ldws', 'lddx', 'ldirx', 'ldpirx', 'lddrx',
]

# name -> argv builder (source, output); the result is read from `output` unless the tool names its own
TOOLS = {
    'sjasmplus': lambda p, s, o: [p, '--nologo', '--zxnext', f'--raw={o}', s],
    'z80asm': lambda p, s, o: [p, '-mz80n', '-b', s],
    'pasmo': lambda p, s, o: [p, s, o],
    'fantasm': lambda p, s, o: [p, '-N', '-n', s, o],
    'zasm': lambda p, s, o: [p, '--z80n', s, '-o', o],
}


def assemble(name, path, directory, line):
    source = os.path.join(directory, 'i.asm')
    out = os.path.join(directory, 'i.bin')
    for f in (out,):
        if os.path.exists(f):
            os.remove(f)
    open(source, 'w').write(f' org 32768\n {line}\n')
    env = dict(os.environ)
    run = subprocess.run(TOOLS[name](path, source, out), cwd=directory, capture_output=True, text=True, env=env)
    if not os.path.exists(out):
        message = (run.stdout + run.stderr).strip().splitlines()
        return None, next((m for m in reversed(message) if 'rror' in m or 'not' in m), message[-1] if message else f'rc {run.returncode}')
    return open(out, 'rb').read(), ''


def main():
    ap = argparse.ArgumentParser()
    for name in TOOLS:
        ap.add_argument(f'--{name}', default=os.environ.get(f'UNREAL_ASM_{name.upper()}'))
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if not args.sjasmplus:
        sys.exit('the reference needs --sjasmplus (or UNREAL_ASM_SJASMPLUS)')
    tools = {n: getattr(args, n) for n in TOOLS if getattr(args, n)}
    work = os.path.abspath(os.path.join('scratch', 'z80nmatrix'))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    score = {n: 0 for n in tools}
    print(f'{"instruction":14} ' + ' '.join(f'{n:10}' for n in tools if n != 'sjasmplus'))
    for line in SET:
        want, error = assemble('sjasmplus', tools['sjasmplus'], work, line)
        if want is None:
            print(f'{line:14} REFERENCE FAILED: {error}')
            continue
        cells = []
        for name, path in tools.items():
            if name == 'sjasmplus':
                continue
            got, error = assemble(name, path, work, line)
            if got == want:
                cells.append('OK')
                score[name] += 1
            elif got is None:
                cells.append('error')
            else:
                cells.append(got.hex())
        print(f'{line:14} ' + ' '.join(f'{c:10}' for c in cells) + f'  ref {want.hex()}')
    print(f'\nequal to sjasmplus, of {len(SET)}: ' + ', '.join(f'{n} {s}' for n, s in score.items() if n != 'sjasmplus'))
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
