#!/usr/bin/env python3
"""Generate one annotated listing (and its symbol file) for a target.

Inputs (all next to this script):
  targets.py            binary, origin, block map source
  <bin>.blocks          data blocks (analyze.py)
  labels-<target>.json  names carried from the reference sources (transfer.py)
  dict_<target>.py      hand-written names, comments and the file header;
                        a hand name wins over a carried one

Output:
  ../<target>/<listing>.asm      z80dasm 1.2.0 listing with comments
  ../../../../../data/symbols/sprinter/<symfile>   "NAME: equ 0xADDR" lines
                                  (one per named address, hand names first)

Every carried label gets a trailing note "src: FILE:LINE" pointing at the
reference source line it came from; "~" marks a name placed by a unique
byte window instead of a long matching run (less certain).

Usage: gen.py <target>
"""
import importlib.util
import json
import os
import re
import subprocess
import sys

import targets

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, '..', '..', '..', '..', '..')
INSTR = re.compile(r'^\t(\S[^\t;]*)\t+;([0-9a-f]{4})\t')
DEFB = re.compile(r'^\s+defb\s+[^\s;]+\s+;([0-9a-f]{4})\s')
LABEL = re.compile(r'^\S.*:$')
BLOCKHDR = re.compile(r'^; BLOCK ')
REFNAMES = {'exp': 'BIOS-TT 0271ac3', 'rom': 'BIOS-PP 1273243', 'setup': 'BIOS-PP 1273243'}


def loadDict(target):
    path = os.path.join(HERE, f'dict_{target}.py')
    spec = importlib.util.spec_from_file_location('dictmod', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def clean(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name.replace('.', '_'))


def symbols(target, mod, org, size):
    """address -> (primary name, [notes]); hand names first, then carried ones."""
    out = {}
    used = set()

    def add(addr, name, note=None):
        name = clean(name)
        if name in used:
            name = f'{name}_{addr:04X}'
        if addr not in out:
            out[addr] = [name, []]
            used.add(name)
        if note:
            out[addr][1].append(note)

    for addr, name in mod.SYMBOLS:
        add(addr, name)
    path = os.path.join(HERE, f'labels-{target}.json')
    if os.path.exists(path):
        for row in json.load(open(path))['labels']:
            addr, name, where, how, ref = row[0], row[1], row[2], row[3], row[4]
            if not org <= addr < org + size or name in getattr(mod, 'DROP', ()):
                continue
            mark = '' if how == 'run' else '~'
            note = f'{mark}{name} (src: {where}, {REFNAMES.get(ref, ref)})'
            if addr in out:
                out[addr][1].append(note)
            else:
                add(addr, name, note)
    return out


def runZ80dasm(binary, org, blocks, symfile, outfile):
    cmd = ['z80dasm', '-a', '-l', '-t', '-g', f'0x{org:04X}',
           '-b', blocks, '--sym-input', symfile, '-o', outfile, binary]
    subprocess.run(cmd, check=True, capture_output=True)


def lineAddr(line):
    m = INSTR.match(line)
    if m:
        return int(m.group(2), 16)
    m = DEFB.match(line)
    return int(m.group(1), 16) if m else None


def renameBlockLabels(lines, blocks, syms):
    """z80dasm names block boundaries <block>_start / <block>_end and uses those
    names in operands.  Use the block name for the start and the real symbol (or
    a plain lXXXXh label) for the end, which is usually code."""
    ren = {}
    for ln in open(blocks):
        m = re.match(r'(\S+): start 0x([0-9A-Fa-f]+) end 0x([0-9A-Fa-f]+)', ln)
        if not m:
            continue
        name, start, end = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        ren[name + '_start'] = syms[start][0] if start in syms else name
        ren[name + '_end'] = syms[end][0] if end in syms else f'l{end:04x}h'
    tok = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(ren, key=len, reverse=True))) + r')\b')
    out, seen = [], set()
    for line in lines:
        line = tok.sub(lambda m: ren[m.group(1)], line)
        if LABEL.match(line):
            if line in seen:
                continue
            seen.add(line)
        elif line.strip() and not line.lstrip().startswith(';'):
            seen = set()
        out.append(line)
    return out


def insertComments(lines, comments, notes):
    first = {}
    for i, line in enumerate(lines):
        a = lineAddr(line)
        if a is not None and a not in first:
            first[a] = i
    pending = {}
    for a, block in comments.items():
        if a not in first:
            print(f'WARNING: comment at {a:04X} not placed', file=sys.stderr)
            continue
        j = first[a]
        while j > 0 and (LABEL.match(lines[j - 1]) or BLOCKHDR.match(lines[j - 1]) or lines[j - 1].strip() == ''):
            j -= 1
        pending.setdefault(j, []).append(block)
    noteAt = {first[a]: n for a, n in notes.items() if a in first and n}
    out = []
    for i, line in enumerate(lines):
        for block in pending.get(i, []):
            out.append(';' + '-' * 75)
            out.extend('; ' + t if t else ';' for t in block)
            out.append(';' + '-' * 75)
        if i in noteAt:
            for n in noteAt[i]:
                out.append(f'\t\t\t\t; = {n}')
        out.append(line)
    return out


def codeMap(binary, blocks, org, size):
    """address -> True when the byte is code (not inside a data block)."""
    data = [False] * size
    for ln in open(blocks):
        m = re.match(r'\S+: start 0x([0-9A-Fa-f]+) end 0x([0-9A-Fa-f]+)', ln)
        if m:
            for a in range(int(m.group(1), 16) - org, int(m.group(2), 16) - org):
                data[a] = True
    return {org + i: not data[i] for i in range(size)}


def main(target):
    t = targets.TARGETS[target]
    mod = loadDict(target)
    binary = os.path.join(HERE, t['bin'])
    blocks = binary.replace('.bin', '.blocks')
    org = t['org']
    size = os.path.getsize(binary)
    syms = symbols(target, mod, org, size)

    symPath = os.path.join(HERE, f'{target}.sym.tmp')
    with open(symPath, 'w') as f:
        for addr in sorted(syms):
            f.write(f'{syms[addr][0]}: equ 0x{addr:04X}\n')
        for addr, name in getattr(mod, 'EQUATES', []):
            f.write(f'{name}: equ 0x{addr:04X}\n')
    rawPath = os.path.join(HERE, f'{target}.raw.tmp')
    runZ80dasm(binary, org, blocks, symPath, rawPath)
    with open(rawPath) as f:
        lines = f.read().splitlines()
    os.remove(symPath)
    os.remove(rawPath)
    while lines and not lines[0].strip().startswith('org'):
        lines.pop(0)
    lines = renameBlockLabels(lines[1:], blocks, syms)
    notes = {a: v[1] for a, v in syms.items()}
    lines = insertComments(lines, dict(mod.COMMENTS), notes)
    header = list(mod.HEADER) + ['', f'\torg 0{org:04x}h', '']
    outPath = os.path.join(HERE, '..', mod.LISTING)
    with open(outPath, 'w') as f:
        f.write('\n'.join(header + lines) + '\n')
    print(f'{os.path.relpath(outPath, HERE)}: {len(lines)} lines, {len(syms)} labels')

    symDir = os.path.join(REPO, 'data', 'symbols', 'sprinter')
    os.makedirs(symDir, exist_ok=True)
    code = codeMap(binary, blocks, org, size)
    with open(os.path.join(symDir, mod.SYMFILE), 'w') as f:
        f.write(f'; {mod.SYMTITLE}\n')
        f.write('; Generated by docs/disasm/rom/sprinter/scripts/gen.py; see docs/disasm/rom/sprinter/README.md\n')
        f.write('; Format: [ROMn:]ADDR NAME (TYPE) ; comment  (LabelManager MAP format; ROMn = physical ROM page)\n')
        for addr in sorted(syms):
            where = f'{mod.SYMBANK}:{addr:04X}' if mod.SYMBANK else f'{addr:04X}'
            kind = 'CODE' if code.get(addr, False) else 'DATA'
            hand = dict(mod.SYMBOLS).get(addr)
            note = '' if hand else ('; ' + syms[addr][1][0].split(' (src: ')[1].rstrip(')') if syms[addr][1] else '')
            f.write(f'{where:<10} {syms[addr][0]:<28} ({kind}) {note}'.rstrip() + '\n')
    print(f'data/symbols/sprinter/{mod.SYMFILE}: {len(syms)} symbols')


if __name__ == '__main__':
    for nm in sys.argv[1:] or list(targets.TARGETS):
        main(nm)
