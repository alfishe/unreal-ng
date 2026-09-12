#!/usr/bin/env python3
"""Generate the annotated monitor disassembly for one Scorpion ROM page.

Pipeline:
  1. Build a z80dasm symbol file from the SYMBOLS list (address -> label).
  2. Run z80dasm with the block-definition file (code/data segmentation).
  3. Post-process the listing: prepend the educational HEADER and insert the
     COMMENTS blocks above the code at each key address.
"""
import importlib.util
import re
import subprocess
import sys
import os

INSTR = re.compile(r'^\t(\S[^\t;]*)\t+;([0-9a-f]{4})\t')
DEFB = re.compile(r'^\s+defb\s+[^\s;]+\s+;([0-9a-f]{4})\s')
LABEL = re.compile(r'^\S.*:$|^blk\S+:$')
BLOCKHDR = re.compile(r"^; BLOCK ")


def loadDict(path):
    spec = importlib.util.spec_from_file_location('dictmod', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def runZ80dasm(binary, blocks, symfile, outfile):
    cmd = ['z80dasm', '-a', '-l', '-t', '-g', '0x0000',
           '-b', blocks, '--sym-input', symfile, '-o', outfile, binary]
    subprocess.run(cmd, check=True, capture_output=True)


def insertComments(lines, comments):
    """comments: dict addr -> list of text lines (without leading ';')."""
    if not comments:
        return lines
    want = {}
    for addr, block in comments.items():
        want.setdefault(addr, [])
        want[addr].extend(block)
    # locate first line index for each wanted address
    first = {}
    for i, line in enumerate(lines):
        m = INSTR.match(line)
        if m:
            a = int(m.group(2), 16)
        else:
            m = DEFB.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
        if a in want and a not in first:
            first[a] = i
    out = []
    pending = {}   # line index -> blocks to emit before it
    for a, i in first.items():
        # walk up over block headers / labels / blank lines
        j = i
        while j > 0 and (LABEL.match(lines[j-1]) or BLOCKHDR.match(lines[j-1]) or lines[j-1].strip() == ''):
            j -= 1
        pending.setdefault(j, [])
        pending[j].append(want[a])
    for i, line in enumerate(lines):
        if i in pending:
            for block in pending[i]:
                out.append('; ' + block[0] if False else ';-' * 38)
                for text in block:
                    out.append('; ' + text if text else ';')
                out.append(';-' * 38)
        out.append(line)
    missed = [hex(a) for a in want if a not in first]
    if missed:
        print('WARNING: comments not placed:', missed, file=sys.stderr)
    return out


def normaliseBlockNames(lines, blocksPath, symbols):
    """Replace blkNNN_start/blkNNN_end references by user symbols when the
    addresses coincide; drop the resulting duplicate label lines."""
    symByAddr = {a: n for a, n in symbols}
    blkmap = {}
    bl = re.compile(r'^(\S+): start 0x([0-9a-f]+) end 0x([0-9a-f]+)')
    with open(blocksPath) as f:
        for line in f:
            m = bl.match(line)
            if m:
                name = m.group(1)
                blkmap[name + '_start'] = int(m.group(2), 16)
                blkmap[name + '_end'] = int(m.group(3), 16)
    ren = {bn: symByAddr[a] for bn, a in blkmap.items() if a in symByAddr}
    if not ren:
        return lines
    tok = re.compile(r'\b(' + '|'.join(sorted(ren)) + r')\b')
    out = [tok.sub(lambda m: ren[m.group(1)], l) for l in lines]
    # drop duplicate label lines created by the rewrite: a block label
    # renamed onto a symbol may repeat a label that appeared just above
    # (only comment/blank lines, e.g. the '; BLOCK' header, in between)
    dedup = []
    seen = set()
    for line in out:
        if line.endswith(':') and LABEL.match(line):
            if line in seen:
                continue
            seen.add(line)
        elif line.strip() and not line.lstrip().startswith(';'):
            seen = set()
        dedup.append(line)
    return dedup


def main():
    binary, blocks, dictPath, outPath = sys.argv[1:5]
    mod = loadDict(dictPath)

    symPath = outPath + '.sym.tmp'
    with open(symPath, 'w') as f:
        for addr, name in mod.SYMBOLS:
            f.write(f'{name}: equ 0x{addr:04X}\n')
    rawPath = outPath + '.raw.tmp'
    runZ80dasm(binary, blocks, symPath, rawPath)

    with open(rawPath) as f:
        lines = f.read().splitlines()
    # drop z80dasm command banner, keep from "org" on
    while lines and not lines[0].strip().startswith('org'):
        lines.pop(0)

    lines = normaliseBlockNames(lines, blocks, mod.SYMBOLS)

    lines = insertComments(lines, dict(mod.COMMENTS))
    header = list(mod.HEADER)
    header.append('')
    header.append('\torg 00000h')
    lines[0] = ''  # original org line replaced by header copy
    with open(outPath, 'w') as f:
        f.write('\n'.join(header + lines[1:]) + '\n')
    os.remove(symPath)
    os.remove(rawPath)
    print(f'{outPath}: {len(lines)} lines')


if __name__ == '__main__':
    main()
