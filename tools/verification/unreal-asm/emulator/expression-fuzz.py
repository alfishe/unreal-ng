#!/usr/bin/env python3
"""Random sjasmplus expressions judged by a retro assembler itself: each `DW expr` built by sjasmplus and, written into
the running assembler through its backend (inject-oracle.py --save), built by the assembler in the emulator; the words
must be equal. A line that differs is printed with the target's text of it (`zxasm convert --to <dialect>`).
The emulator converts the source with its own backend (POST /asm/sync/inject): after a backend change, rebuild and
restart unreal-qt, or it judges the old code.

    expression-fuzz.py <target> <assembler disk> --seed 1 [--lines 100] [--ops +,-,*,&] [--port 8395]
        [--sjasmplus PATH] [--zxasm PATH] [--work scratch/expression-fuzz]

Targets: storm (STORM_13.TRD), storm13i (STORM13I.SCL), tasm (TASM_412.SCL), alasm (ALASM509.TRD), xas (XAS7_447.trd).
Labels (EQU above, one defined below), numbers, unary - ~ high low !, binary + - * / % & | ^ << >> and the
comparisons and && || by default (--ops narrows them). Exit 0 when all words agree.
"""
import argparse, os, random, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../../../..'))
OPS = ['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>', '<', '>', '<=', '>=', '==', '!=', '&&', '||']
UNARY = ['-', '~', 'high ', 'low ', '!']
# target: inject-oracle profile, its options, the code's address, the dialect and its options for zxasm
TARGETS = {
    'storm': ('storm', [], 0x8000, ['storm']),
    'storm13i': ('storm13i', ['--boot', 'STORMimp'], 0x8000, ['storm']),
    'tasm': ('tasm412', ['--wait', '15'], 0x6200, ['tasm', '4.12']),   # TASM's stack is at #617C
    'alasm': ('alasm509', [], 0x6000, ['alasm']),                         # ALASM compiles #8000-#BFFF into its page
    'xas': ('xas', ['--boot', 'XAS7.447', '--assembler', 'xas-7.447', '--wait', '10'], 0x6000, ['xas']),   # 100 lines take XAS more than 3 s
}
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
    p.add_argument('target', choices=sorted(TARGETS))
    p.add_argument('disk')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--lines', type=int, default=100)
    p.add_argument('--ops', help='comma-separated binary operators (default: all)')
    p.add_argument('--port', type=int, default=8395)
    p.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    p.add_argument('--zxasm', default=os.path.join(ROOT, 'cmake-build-agent-release/bin/zxasm'))
    p.add_argument('--work', default=os.path.join(ROOT, 'scratch/expression-fuzz'))
    a = p.parse_args()
    profile, options, address, dialect = TARGETS[a.target]
    os.makedirs(a.work, exist_ok=True)
    rnd = random.Random(a.seed)
    ops = a.ops.split(',') if a.ops else OPS
    exprs = [generate(rnd, ops, 3) for _ in range(a.lines)]
    stem = os.path.join(a.work, f'{a.target}{a.seed}')
    lines = [f'{k} equ {v}' for k, v in LABELS.items()] + [f'        org #{address:04X}'] + [f'        dw {e}' for e in exprs] + ['fwd     nop']
    open(stem + '.asm', 'w').write('\n'.join(lines) + '\n')
    subprocess.run([a.sjasmplus, f'--raw={stem}.bin', stem + '.asm'], capture_output=True, cwd=a.work, check=True)
    want = open(stem + '.bin', 'rb').read()
    # the target's text of each line, for the report
    version = ['--to-version', dialect[1]] if len(dialect) > 1 else []
    conv = subprocess.run([a.zxasm, 'convert', stem + '.asm', '--from', 'sjasmplus', '--to', dialect[0]] + version, capture_output=True)
    open(stem + '.target', 'wb').write(conv.stdout)
    decode = [a.zxasm, 'decode', stem + '.target', '--codec', dialect[0]] + (['--version', dialect[1]] if len(dialect) > 1 else [])
    text = subprocess.run(decode, capture_output=True, text=True).stdout
    open(stem + '.txt', 'w').write(text)
    words = [l for l in text.splitlines() if re.search(r'\bD(W|EFW)\b', l, re.I)]
    subprocess.run([sys.executable, os.path.join(HERE, 'inject-oracle.py'), profile, a.disk, stem + '.asm', hex(address), '--save', stem + '.out',
                    '--sjasmplus', a.sjasmplus, '--zxasm', a.zxasm, '--port', str(a.port)] + options, capture_output=True, timeout=600)
    got = open(stem + '.out', 'rb').read() if os.path.exists(stem + '.out') else b''
    bad = 0
    for i, e in enumerate(exprs):
        w, g = want[2 * i:2 * i + 2], got[2 * i:2 * i + 2]
        if w != g:
            bad += 1
            print(f'line {i}: sjasmplus {w.hex()} {a.target} {g.hex()}: {e}  =>  {words[i].strip() if i < len(words) else "?"}')
    symbolic = sum(1 for l in words if not re.fullmatch(r'#[0-9A-F]+|\d+|[0-9A-F]+H|\$[0-9A-F]+', l.split()[-1], re.I))
    print(f'{a.target} seed {a.seed}: {a.lines - bad} of {a.lines} equal, {symbolic} written as expressions (the rest as values)'
          + ('' if got else ' (nothing saved: see the oracle\'s screenshots)'))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
