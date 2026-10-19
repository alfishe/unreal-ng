#!/usr/bin/env python3
"""Random sjasmplus expressions through the STORM backend into STORM 1.3 itself: each `DW expr` built by sjasmplus and,
converted with `zxasm convert --to storm`, assembled by STORM 1.3 in the emulator (assemble-in-emulator.py); the words
must be equal. A line that differs is printed with its STORM text.

    storm-expression-fuzz.py <STORM_13.TRD> --seed 1 [--lines 100] [--ops +,-,*,&] [--port 8395]
        [--sjasmplus PATH] [--zxasm PATH] [--work scratch/storm-fuzz]

Labels (EQU above, one defined below), numbers, unary - ~ high low !, binary + - * / % & | ^ << >> and the
comparisons and && || by default (--ops narrows them: comparisons and logic mostly go as values, see
research-sjasmplus-to-storm.md §2). Exit 0 when all words agree.
"""
import argparse, os, random, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../../../..'))
OPS = ['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>', '<', '>', '<=', '>=', '==', '!=', '&&', '||']
UNARY = ['-', '~', 'high ', 'low ', '!']
LABELS = {'alpha': 1234, 'beta': 0x8001, 'gamma': 7, 'delta': 300}


def generate(rnd, ops, depth):
    def atom():
        r = rnd.random()
        if r < 0.3:
            return rnd.choice(list(LABELS) + ['fwd'])
        if r < 0.5:
            return '#%X' % rnd.randint(0, 0xFFFF)
        return str(rnd.randint(0, 40))

    def expr(d):
        if d == 0 or rnd.random() < 0.25:
            return atom()
        if rnd.random() < 0.15:
            return rnd.choice(UNARY) + '(' + expr(d - 1) + ')'
        op = rnd.choice(ops)
        left, right = expr(d - 1), expr(d - 1)
        if op in ('/', '%'):
            right = str(rnd.randint(1, 30))
        if op in ('<<', '>>'):
            right = str(rnd.randint(0, 15))
        l = '(' + left + ')' if rnd.random() < 0.5 else left
        r = '(' + right + ')' if rnd.random() < 0.5 else right
        return l + ' ' + op + ' ' + r

    return expr(depth)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('disk')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--lines', type=int, default=100)
    p.add_argument('--ops', help='comma-separated binary operators (default: all)')
    p.add_argument('--port', type=int, default=8395)
    p.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    p.add_argument('--zxasm', default=os.path.join(ROOT, 'cmake-build-agent-release/bin/zxasm'))
    p.add_argument('--work', default=os.path.join(ROOT, 'scratch/storm-fuzz'))
    a = p.parse_args()
    os.makedirs(a.work, exist_ok=True)
    rnd = random.Random(a.seed)
    ops = a.ops.split(',') if a.ops else OPS
    exprs = [generate(rnd, ops, 3) for _ in range(a.lines)]
    stem = os.path.join(a.work, f'fuzz{a.seed}')
    lines = [f'{k} equ {v}' for k, v in LABELS.items()] + ['        org #8000'] + [f'        dw {e}' for e in exprs] + ['fwd     nop']
    open(stem + '.asm', 'w').write('\n'.join(lines) + '\n')
    subprocess.run([a.sjasmplus, f'--raw={stem}.bin', stem + '.asm'], capture_output=True, cwd=a.work, check=True)
    want = open(stem + '.bin', 'rb').read()
    conv = subprocess.run([a.zxasm, 'convert', stem + '.asm', '--from', 'sjasmplus', '--to', 'storm'], capture_output=True)
    open(stem + '.st', 'wb').write(conv.stdout)
    text = subprocess.run([a.zxasm, 'decode', stem + '.st', '--codec', 'storm'], capture_output=True, text=True).stdout
    text = '\n'.join(l for l in text.splitlines() if not l.startswith('storm 1.3:')) + '\n'
    open(stem + '.txt', 'w').write(text)
    # a file name of letters only: STORM's LOAD prompt takes some digits as cursor keys
    hobeta = os.path.join(a.work, 'FUZZ' + chr(ord('A') + a.seed % 26) + '.$C')
    subprocess.run([a.zxasm, 'encode', stem + '.txt', '--codec', 'storm', '-o', hobeta], capture_output=True, check=True)
    subprocess.run([sys.executable, os.path.join(HERE, 'assemble-in-emulator.py'), 'storm13', a.disk, hobeta, '0x8000', str(len(want)),
                    stem + '.out', '--port', str(a.port)], capture_output=True, timeout=300)
    got = open(stem + '.out', 'rb').read()
    storm = [l for l in text.splitlines() if l.strip().startswith('DW')]
    bad = 0
    for i, e in enumerate(exprs):
        w, g = want[2 * i:2 * i + 2], got[2 * i:2 * i + 2]
        if w != g:
            bad += 1
            print(f'line {i}: sjasmplus {w.hex()} STORM {g.hex()}: {e}  =>  {storm[i].strip() if i < len(storm) else "?"}')
    symbolic = sum(1 for l in storm if not re.fullmatch(r'#[0-9A-F]+|\d+', l.split()[-1]))
    print(f'seed {a.seed}: {a.lines - bad} of {a.lines} equal, {symbolic} written as expressions (the rest as values)')
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
