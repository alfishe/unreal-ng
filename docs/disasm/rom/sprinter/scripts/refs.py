#!/usr/bin/env python3
"""Turn a sjasmplus listing of a reference-source build into a label file.

The reference builds (see build-refs.sh) assemble the closest public sources:
  exp    Sprinter-BIOS (Tolik-Trek) 0271ac3 + Shared_Includes 66d8b07: bios/exp/*
  rom    sprinter-computer/bios 1273243: SETUP/EXTENDED.ASM (+ drivers)
  setup  sprinter-computer/bios 1273243: SETUP/DSETUP.ASM (+ KEY, VIDEO_IO, AUTOIDE)

Output JSON (refs-<name>.json):
  {"labels": [[addr, name, "file:line"], ...],
   "insns":  [[addr, length], ...]}       instruction boundaries (code only)
Only lines from the files whose names match the filter are used (the BIOS-TT
build assembles all ROM pages in one run).

Usage: refs.py <name> <listing.lst> <filter-regex>
"""
import json
import re
import sys

LINE = re.compile(r'^\s*(\d+)([+~]*)\s*([0-9A-F]{4})\s((?:[0-9A-F]{2} ?)*)(?:\.\.\.)?')
OPENED = re.compile(r'^# file opened: (.*)$')
CLOSED = re.compile(r'^# file closed: (.*)$')
LABEL = re.compile(r'^(\.?[A-Za-z_@][\w.@?]*):?(?=\s|$)')
DATA = re.compile(r'^\s*(?:DB|DW|DEFB|DEFW|DEFM|DM|DZ|DC|BLOCK|DS|DEFS|INCBIN|ALIGN|BYTE|WORD)\b', re.I)
NONLABEL = {'IF', 'ELSE', 'ENDIF', 'MACRO', 'ENDM', 'MODULE', 'ENDMODULE', 'DISP', 'ENT', 'ORG',
            'INCLUDE', 'DEVICE', 'MMU', 'OUTPUT', 'OUTEND', 'DEFINE', 'END', 'LUA', 'ENDLUA', 'STRUCT', 'ENDS'}


def parse(path, filt):
    keep = re.compile(filt, re.I)
    stack = []
    labels, insns = [], []
    emitted = set()
    major = ''
    with open(path, 'rb') as f:
        raw = f.read().decode('cp866', 'replace').splitlines()
    for lineNo, ln in enumerate(raw, 1):
        m = OPENED.match(ln)
        if m:
            stack.append(m.group(1))
            continue
        m = CLOSED.match(ln)
        if m:
            if stack:
                stack.pop()
            continue
        if not stack or not keep.search(stack[-1].replace('\\', '/')):
            continue
        m = LINE.match(ln)
        if not m:
            continue
        srcLine = int(m.group(1))
        col = m.end(3) + 14                      # the bytes field is 13 columns wide
        if len(ln) > col - 1 and ln[col - 1] == '>':   # macro expansion body
            continue
        addr = int(m.group(3), 16)
        hexBytes = m.group(4).split()
        src = ln[col:] if len(ln) > col else ''
        lm = LABEL.match(src)
        if lm and not src[:1].isspace():
            name = lm.group(1)
            if name.upper() not in NONLABEL and not re.match(r'^\w+\s+(EQU|=)', src, re.I):
                if name.startswith('.'):
                    name = major + name
                else:
                    major = name
                where = f"{stack[-1].replace(chr(92), '/').split('/')[-1]}:{srcLine}"
                labels.append([addr, name, where])
            rest = src[lm.end():]
        else:
            rest = src
        if hexBytes:
            emitted.add(addr)
        if hexBytes and rest.strip() and not DATA.match(rest) and not rest.lstrip().startswith(';'):
            insns.append([addr, len(hexBytes)])
    # keep only labels that sit on emitted bytes (drops EQU-like labels of
    # structures and constants, which have an address but no code or data)
    labels = [l for l in labels if l[0] in emitted]
    return {'labels': labels, 'insns': insns}


if __name__ == '__main__':
    name, lst, filt = sys.argv[1:4]
    out = parse(lst, filt)
    with open(f'refs-{name}.json', 'w') as f:
        json.dump(out, f, indent=0)
    print(f'refs-{name}.json: {len(out["labels"])} labels, {len(out["insns"])} instructions')
