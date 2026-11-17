#!/usr/bin/env python3
"""The reference tables of rasm's Z80 instruction layer: every instruction form built from a pool of operands (all
documented and undocumented forms, the super-instructions, rejected forms, rasm's own AUTOTEST_INSTRMUSTFAILED) is
assembled alone with rasm, and what rasm wrote or reported goes to two tables that tests/rasminstructions_test.cpp
replays against unrealasm::rasm (src/dialects/rasm/engine/rasminstructions.h).

    rasminstrtable.py --rasm PATH [--out DIR] [--jobs N]

instructions.tsv: form <TAB> hex bytes (org #100) [<TAB> ?first warning], or form <TAB> !error | error ...
timings.tsv:      form <TAB> ticker <TAB> getnop <TAB> gettick <TAB> getsize; ticker is "nops/ticks" from
                  TICKER START / STOP / STOPZX around the form (- when rasm rejects the form), the others are the value
                  of GETNOP("form") ... or !first error

DIR defaults to core/src/3rdparty/unreal-asm/testdata/rasm; work files go to scratch/rasminstrtable.
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
ORG = '#100'

R8 = ['A', 'B', 'C', 'D', 'E', 'H', 'L', 'I', 'R', 'F']
HALVES = ['IXH', 'IXL', 'IYH', 'IYL', 'XH', 'XL', 'YH', 'YL', 'HX', 'LX', 'HY', 'LY']
R16 = ['AF', 'BC', 'DE', 'HL', 'SP', 'IX', 'IY']
MEM = ['(HL)', '(BC)', '(DE)', '(SP)', '(C)', '(IX)', '(IY)', '(IX+5)', '(IX-3)', '(IY+7)', '(IY-128)', '(IX+127)',
       '(IX+200)', '(#1234)', '(5)', '(5)+1']
IMM = ['0', '1', '2', '7', '8', '#12', '#1234', '#38', '#10', '-1', '300', '$+5', '$', "'a'"]
COND = ['NZ', 'Z', 'NC', 'PO', 'PE', 'P', 'M']
ALL = R8 + HALVES + R16 + MEM + IMM + COND

ALU = ['ADD', 'ADC', 'SUB', 'SBC', 'AND', 'OR', 'XOR', 'CP']
ROT = ['RLC', 'RRC', 'RL', 'RR', 'SLA', 'SRA', 'SLL', 'SL1', 'SRL', 'SHL', 'SHR']
NOOP = ['NEG', 'DAA', 'CPL', 'RETI', 'SCF', 'LDDR', 'LDIR', 'CCF', 'CPD', 'CPDR', 'CPI', 'CPIR', 'OTDR', 'OTIR',
        'RETN', 'INDR', 'INIR', 'EXX', 'RLA', 'RRA', 'RLD', 'RRD', 'EXA', 'DI', 'EI', 'RLCA', 'RRCA', 'LDD', 'LDI',
        'OUTD', 'OTD', 'OUTI', 'OTI', 'IND', 'INI', 'HALT', 'NOP']
OTHER = ['LD', 'INC', 'DEC', 'PUSH', 'POP', 'IN', 'OUT', 'EX', 'RET', 'CALL', 'JR', 'JP', 'IM', 'RST', 'DJNZ', 'SRL8',
         'BIT', 'RES', 'SET']
MNEMONICS = ALU + ROT + NOOP + OTHER

SUPER = [
    'PUSH BC,DE,HL,AF', 'PUSH IX,IY', 'POP AF,HL,DE,BC', 'POP IY,IX', 'PUSH BC,B', 'INC A,B,HL,(HL),(IX+3),IY,XL',
    'DEC BC,(IY-2),E', 'INC A,(5)', 'NOP 5', 'NOP 0', 'NOP -1', 'NOP 2,3', 'HALT 3', 'HALT 0', 'HALT -2', 'RLCA 3',
    'RLCA 0', 'RRCA 4', 'RRCA 0', 'RRCA -1', 'LDI 4', 'LDI 0', 'LDD 2', 'OUTI 3', 'OTI 2', 'OUTD 2', 'OTD 1', 'INI 2',
    'IND 3', 'LDI 2,3', 'NOP 2.4', 'NOP 2.5', 'NOP 1+1', 'LD HL,(IX+5)', 'LD DE,(IY-4)', 'LD BC,(IX+127)',
    'LD (IX+5),HL', 'LD (IY-128),DE', 'LD (IX+126),BC', 'LD HL,(IX+$)', 'LD (IX+$),DE', 'LD BC,IX', 'LD DE,IY',
    'LD IX,BC', 'LD IY,DE', 'LD HL,SP', 'LD BC,DE', 'LD HL,BC', 'SRL8 BC', 'SRL8 DE', 'SRL8 HL', 'SRL8 IX', 'SRL8 IY',
    'SRL8 A', 'SRL8 BC,DE', 'RST Z,#38', 'RST NC,#38', 'RST PO,#38', 'RST Z,#30', 'RST C,$', 'EX AF,AF\'',
    "LD A,'\\''", "LD A,'('", 'LD A,(IX+5)*2', 'LD A,(IX+5)+(IX+5)', 'JR $', 'JR $+129', 'JR $-126', 'JR $+130',
    'JR $-127', 'DJNZ $+2', 'JP $', 'CALL NZ,$+3', 'LD (IX+5),(5)', 'LD (IX+5),(5)+1', 'LD (IX+0-4),A',
    'LD A,(IX+#FFFF)', 'LD A,(IX+65532)', 'BIT 1+1,B', 'SET 7,(IX+5),A', 'RES 0,(IY-1),(HL)', 'BIT 0,(IX+1),A',
    'RLC (IX+5),B,C', 'SLA (HL),B', 'LD A,(IX+5),B', 'IM 0', 'IM 1', 'IM 2', 'IM 3', 'IM -1', 'RST #38', 'RST 7',
    'OUT (C),0', 'IN 0,(C)', 'IN F,(C)', 'OUT (#FE),A', 'IN A,(#FE)', 'IN A,#FE', 'OUT #FE,A', 'LD B,(5)+1',
    'LD (HL),(5)', 'LD (HL),(5)+1', 'ADD A,(5)', 'CP (5)+1', 'JP (5)', 'JP (IX+0)', 'LD XH,(5)', 'LD XL,XH',
    'LD XH,YL', 'LD H,XH', 'LD XL,H', 'LD XL,(HL)', 'LD A,(5)+1', 'LD A,(5)', 'LD SP,(5)', 'LD IX,(5)+1', 'LD IX,(5)',
    'LD HL,(5)+1', 'LD BC,(5)+1', 'LD (5)+1,A',
]


def instrmustfailed():
    """The forms of rasm's autotests/AUTOTEST_INSTRMUSTFAILED.asm (one statement there, ':' separated)"""
    forms = ['ld a,b,c', 'ldi a', 'ldir bc', 'exx hl,de', 'exx de', 'ex bc,hl', 'ex hl,bc', 'ex af,bc', 'ex bc,af',
             'ex hl,hl', 'ex hl', 'exx hl', 'neg b', 'push b', 'push', 'pop', 'pop c', 'sub ix', 'add ix', 'add',
             'sub', 'di 2', 'ei 3', 'ld i,c', 'ld r,e', 'rl', 'rr', 'rlca a', 'sla', 'sll', 'ldd e', 'lddr hl',
             'adc ix', 'adc b,a', 'xor 12,13', 'xor b,1', 'xor', 'or 12,13', 'or b,1', 'or', 'and 12,13', 'and b,1',
             'and', 'inc', 'dec']
    return [f.upper() for f in forms]


def forms():
    out = []
    for m in MNEMONICS:
        out.append(m)
        out += ['%s %s' % (m, a) for a in ALL]
    out += ['LD %s,%s' % (a, b) for a in ALL for b in ALL]
    for m in ALU:
        out += ['%s %s,%s' % (m, a, b) for a in ['A', 'HL', 'IX', 'IY', 'B', '12'] for b in ALL]
    out += ['IN %s,%s' % (a, b) for a in R8 + ['0', '(C)', '(5)'] for b in ALL]
    out += ['OUT %s,%s' % (a, b) for a in ['(C)', '(5)', '(#FE)', '5', 'A', '(HL)'] for b in ALL]
    out += ['EX %s,%s' % (a, b) for a in R16 + ['(SP)', 'A', '(HL)'] for b in R16 + ['(SP)', '(HL)', 'A']]
    for m in ['JP', 'JR', 'CALL', 'RET', 'DJNZ']:
        out += ['%s %s,%s' % (m, a, b) for a in COND + ['C', 'A', 'HL'] for b in ['#1234', '$+5', '(IX)', '(HL)', 'HL']]
    out += ['RST %s,%s' % (a, b) for a in COND + ['C', 'A'] for b in ['#38', '#30', '0', '(IX)']]
    for m in ['BIT', 'RES', 'SET']:
        out += ['%s %s,%s' % (m, a, b) for a in ['0', '1', '7', '8', '-1', 'A'] for b in ALL]
        out += ['%s %s,%s,%s' % (m, a, b, c) for a in ['0', '7', '8'] for b in ['(IX+5)', '(IY-3)', '(HL)', 'B']
                for c in R8 + ['(HL)', 'XL']]
    for m in ROT:
        out += ['%s %s,%s' % (m, a, b) for a in ['(IX+5)', '(IY-3)', '(IX)', '(HL)', 'B', 'HL']
                for b in R8 + ['(HL)', 'XL', '5']]
    out += ['IM %s,%s' % (a, b) for a in ['0', '1'] for b in ['1', 'A']]
    out += SUPER + instrmustfailed()
    seen = set()
    unique = []
    for f in out:
        if f not in seen:
            seen.add(f)
            unique.append(f)
    return unique


ANSI = re.compile(r'\x1b\[[0-9;]*m')
MESSAGE = re.compile(r'\[[^\]]*:\d+\] (.*)$')


def run(rasm, work, name, source):
    """rasm's output lines (colour codes removed) and the bytes it wrote (None when it wrote nothing)"""
    asm = os.path.join(work, name + '.asm')
    binary = os.path.join(work, name + '.bin')
    with open(asm, 'w') as f:
        f.write(source)
    if os.path.exists(binary):
        os.remove(binary)
    p = subprocess.run([rasm, name + '.asm', '-o', name], cwd=work, capture_output=True, text=True,
                       errors='replace', timeout=60)
    lines = [ANSI.sub('', l) for l in (p.stdout + p.stderr).splitlines()]
    data = open(binary, 'rb').read() if os.path.exists(binary) else None
    errors, warnings = [], []
    for l in lines:
        m = MESSAGE.search(l)
        if not m:
            continue
        text = m.group(1).strip()
        if text.startswith('Warning: '):
            warnings.append(text[len('Warning: '):])
        else:
            errors.append(text)
    failed = bool(errors) or any(re.match(r'^\d+ errors?$', l.strip()) for l in lines)
    return lines, data, errors, warnings, failed


def encode(rasm, work, index, form):
    name = 'f%05d' % index
    _, data, errors, warnings, failed = run(rasm, work, name, ' org %s\n %s\n' % (ORG, form))
    if failed:
        return '!' + ' | '.join(errors or ['error']), False
    result = (data or b'').hex().upper()
    if warnings:
        result += '\t?' + warnings[0]
    return result, True


def printed(lines, tag):
    for l in lines:
        if l.startswith(tag):
            return l[len(tag):].strip()
    return None


def number(text):
    try:
        v = float(text)
    except (TypeError, ValueError):
        return None
    return str(int(v)) if v == int(v) else str(v)


def timing(rasm, work, index, form, accepted):
    name = 't%05d' % index
    ticker = '-'
    if accepted:
        lines, _, _, _, failed = run(rasm, work, name, ' org %s\n ticker start,va\n ticker start,vb\n %s\n'
                                     ' ticker stop,va\n ticker stopzx,vb\n print "T=",va,"/",vb\n' % (ORG, form))
        text = printed(lines, 'T=')
        if not failed and text and '/' in text:
            a, b = text.split('/', 1)
            ticker = '%s/%s' % (number(a.strip()), number(b.strip()))
    values = [ticker]
    for function in ['getnop', 'gettick', 'getsize']:
        lines, _, errors, _, failed = run(rasm, work, name, ' org %s\n print "G=",%s("%s")\n' % (ORG, function, form))
        text = printed(lines, 'G=')
        if failed:
            values.append('!' + (errors[0] if errors else 'error'))
        else:
            values.append(number(text) or '?')
    return values


TIMING_EXTRA = ['nop:nop', 'ld a,b : ld (ix+5),a', 'ex af,af\':exx', '  push  ix :pop iy', 'ld hl,(ix+5)',
                'nop:', ':nop', 'call nz,#1234', 'call c,#1234', 'call z,#1234', 'call #1234', 'jp (ix)', 'jp (hl)',
                'ld a', 'ld', 'add a,ixl', 'add ixh', 'sub a,b', 'adc a,b', 'sbc a,(hl)', 'cp xl',
                'xor (iy-3)', 'out (c),a', 'out (5),a', 'in a,(c)', 'srl8 ix', 'srl8 hl', 'exa', 'halt', 'ret nz',
                'ret', 'jr nz,$', 'rst #38', 'im 1', 'inc (ix)', 'inc ixh', 'dec yl', 'ld ix,(5)', 'ld (5),ix',
                'ld (5),iy', 'ld sp,iy', 'ld a,ly', 'ld hx,5', 'ld xl,b', 'set 1,(iy+3)', 'bit 1,(hl)', 'res 1,a',
                'ld i,a', 'ld a,i', 'ld b,i', 'ld (bc),a', 'ld (bc),b', 'ld b,(de)', 'ld b,(5)', 'ld (5),b',
                'ld r,b', 'ld c,ixl', 'ld e,(ix+1)']


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--rasm', required=True)
    parser.add_argument('--out', default=os.path.join(ROOT, 'core', 'src', '3rdparty', 'unreal-asm', 'testdata', 'rasm'))
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()
    rasm = os.path.abspath(args.rasm)
    work = os.path.join(ROOT, 'scratch', 'rasminstrtable')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    os.makedirs(args.out, exist_ok=True)

    table = forms()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        encoded = list(pool.map(lambda p: encode(rasm, work, p[0], p[1]), enumerate(table)))
    with open(os.path.join(args.out, 'instructions.tsv'), 'w') as f:
        for form, (result, _) in zip(table, encoded):
            f.write('%s\t%s\n' % (form, result))
    accepted = sum(1 for _, ok in encoded if ok)
    print('instructions.tsv: %d forms, %d accepted, %d rejected' % (len(table), accepted, len(table) - accepted))

    # timings: the accepted forms, the rejected forms of AUTOTEST_INSTRMUSTFAILED and spellings of GETNOP strings
    timed = [(f, ok) for f, (_, ok) in zip(table, encoded) if ok and '"' not in f]
    timed += [(f, False) for f in instrmustfailed()] + [(f, False) for f in TIMING_EXTRA]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        timings = list(pool.map(lambda p: timing(rasm, work, p[0], p[1][0], p[1][1]), enumerate(timed)))
    with open(os.path.join(args.out, 'timings.tsv'), 'w') as f:
        for (form, _), values in zip(timed, timings):
            f.write('%s\t%s\n' % (form, '\t'.join(values)))
    print('timings.tsv: %d forms' % len(timed))
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
