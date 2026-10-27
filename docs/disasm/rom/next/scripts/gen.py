#!/usr/bin/env python3
"""Generate the annotated listing and the symbol file of one Next ROM image.

  gen.py <target>|all          targets: see targets.py

Steps: (1) code reachability from the vectors, the hand entries (dict_<target>.py ENTRIES), the API jump
tables and the 16-bit code addresses of word tables named there; (2) every byte not reached is a data
block (Fill / Zero / Text / Data); (3) z88dk-dis (-mz80n) decodes each code run (the run starts at a known
instruction boundary, so the linear decode is exact); (4) jump / call targets get labels, hand names and
comments from dict_<target>.py win. The listing rebuilds the binary byte for byte (checkcov.py).

Needs z88dk-dis: set Z88DK_DIS or have it on PATH.
"""
import importlib.util
import os
import re
import shutil
import subprocess
import sys

import targets
import z80n

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', '..'))
OUTDIR = os.path.normpath(os.path.join(HERE, '..'))
SYMDIR = os.path.join(REPO, 'data', 'symbols', 'next')
DIS = os.environ.get('Z88DK_DIS') or shutil.which('z88dk-dis') or 'z88dk-dis'
LINE = re.compile(r'^\s+(\S.*?)\s*;\[([0-9a-f]{4})\] ((?:[0-9a-f]{2} ?)+)$')
BRANCH = re.compile(r'^(jp|jr|call|djnz)(\s+(?:nz|z|nc|c|po|pe|p|m),|\s+)\$([0-9a-f]{4})$')


def loadDict(name):
    path = os.path.join(HERE, f'dict_{name}.py')
    if not os.path.exists(path):
        t = targets.TARGETS[name]
        class D:
            LISTING = t['listing']
            SYMFILE = t['symfile']
            TITLE = t['title']
            HEADER = []
            NAMES = {}
            COMMENTS = {}
            ENTRIES = []
            DATA = {}
            WORDTABLES = []
            FORCEDATA = []
        return D
    spec = importlib.util.spec_from_file_location('d', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for k, v in dict(NAMES={}, COMMENTS={}, ENTRIES=[], DATA={}, WORDTABLES=[], FORCEDATA=[], HEADER=[]).items():
        if not hasattr(mod, k):
            setattr(mod, k, v)
    return mod



_ED_OK = (set(range(0x40, 0x80)) - {0x77, 0x7F} | {0xA0, 0xA1, 0xA2, 0xA3, 0xA8, 0xA9, 0xAA, 0xAB, 0xB0, 0xB1, 0xB2, 0xB3,
          0xB8, 0xB9, 0xBA, 0xBB} | z80n._N0 | z80n._N1 | z80n._N2)


def plausible(data, pos, count=4):
    """The next `count` instructions at pos decode to ordinary-looking Z80 / Z80N code."""
    n = len(data)
    for _ in range(count):
        if pos >= n:
            return False
        b = data[pos]
        if b == 0xED and (pos + 1 >= n or data[pos + 1] not in _ED_OK):
            return False
        if b in (0xDD, 0xFD) and pos + 1 < n and data[pos + 1] in (0xDD, 0xFD, 0xED):
            return False
        ln = z80n.length(data, pos)
        if pos + ln > n:
            return False
        tgt, falls = z80n.flow(data, pos, 0)
        pos += ln
        if not falls:
            return True
    return True


def autoSeeds(data, org, code):
    """Tentative entries from still-unreached bytes: runs of JP nn, and word tables of plausible code addresses."""
    n = len(data)
    seeds = set()
    i = 0
    while i + 3 <= n:
        run = 0
        while i + 3 * (run + 1) <= n and not any(code[i + 3 * run:i + 3 * run + 3]) \
                and data[i + 3 * run] == 0xC3 and org <= (data[i + 3 * run + 1] | data[i + 3 * run + 2] << 8) < org + n:
            run += 1
        if run >= 3:
            for k in range(run):
                seeds.add(org + i + 3 * k)
            i += 3 * run
        else:
            i += 1
    i = 0
    while i + 2 <= n:
        run = 0
        while i + 2 * (run + 1) <= n and not any(code[i + 2 * run:i + 2 * run + 2]):
            w = data[i + 2 * run] | data[i + 2 * run + 1] << 8
            if org <= w < org + n and plausible(data, w - org):
                run += 1
            else:
                break
        if run >= 4:
            for k in range(run):
                seeds.add(data[i + 2 * k] | data[i + 2 * k + 1] << 8)
            i += 2 * run
        else:
            i += 1
    return seeds


MAPLINE = re.compile(r'^([0-9A-Fa-f]{4})\s+(\S+)\s+\((CODE|DATA)\)\s*;?\s*(.*)$')


def carried(data, org, specs):
    """Names carried from a classic ROM's symbol map. CODE: a unique 10-byte window of the routine matches.
    DATA: the bytes up to the next symbol are identical at the same address (a data block that did not move).
    Returns (code {addr: (name, comment, map)}, data {addr: (name, comment, map, end)})."""
    code, dat = {}, {}
    for rom, mp in specs:
        classic = open(os.path.join(REPO, 'data', 'rom', rom), 'rb').read()
        rows = []
        for ln in open(os.path.join(REPO, 'data', 'symbols', mp)):
            m = MAPLINE.match(ln.strip())
            if m:
                rows.append((int(m.group(1), 16), re.sub(r'[^A-Za-z0-9_]', '_', m.group(2)), m.group(3), m.group(4).strip()))
        rows.sort()
        for k, (a, nm, ty, cm) in enumerate(rows):
            if ty == 'CODE':
                win = classic[a:a + 10]
                if len(win) < 10 or len(set(win)) < 4:
                    continue
                hits = [p for p in range(len(data) - 9) if data[p:p + 10] == win]
                if len(hits) == 1:
                    code[org + hits[0]] = (nm, cm, mp)
            else:
                nxt = next((r[0] for r in rows[k + 1:] if r[0] > a), len(classic))
                if org + nxt <= org + len(data) and data[a:nxt] == classic[a:nxt] and nxt - a >= 2:
                    dat[org + a] = (nm, cm, mp, org + nxt)
    return code, dat

def walk(data, org, entries, blocked, code=None, starts=None, strict=False):
    """Mark the code reached from entries. Instructions never overlap: a path that meets bytes owned by an
    instruction with another start stops there; with strict=True (tentative seeds) it is dropped completely,
    as is a path that decodes something implausible."""
    n = len(data)
    if code is None:
        code = [False] * n
        starts = set()
        for s, e in blocked:
            for a in range(s - org, e - org):
                code[a] = True
    stack = list(entries)
    while stack:
        entry = stack.pop() - org
        pos = entry
        path = []
        nested = []
        ok = True
        while 0 <= pos < n:
            if code[pos]:
                if pos not in starts and pos not in {p for p, _ in path}:
                    ok = False        # lands inside another instruction
                break
            ln = z80n.length(data, pos)
            if pos + ln > n or any(code[pos + i] for i in range(ln)):
                ok = False
                break
            if strict and not plausible(data, pos, 1):
                ok = False
                break
            path.append((pos, ln))
            tgt, falls = z80n.flow(data, pos, org)
            nested += [t for t in tgt if org <= t < org + n]
            if not falls:
                break
            pos += ln
        if strict and not ok:
            continue
        for p, ln in path:
            starts.add(p)
            for i in range(ln):
                code[p + i] = True
        stack += nested
    for s, e in blocked:
        for a in range(s - org, e - org):
            code[a] = False
    return code, starts


def classify(seg):
    if all(c == 0xFF for c in seg):
        return 'Fill'
    if all(c == 0 for c in seg):
        return 'Zero'
    printable = sum(1 for c in seg if 32 <= c < 127 or c & 0x80 and 32 <= (c & 0x7F) < 127)
    if len(seg) >= 6 and printable >= 0.8 * len(seg):
        return 'Text'
    return 'Data'


def decode(path, org, start, end):
    """z88dk-dis over [start, end): list of (addr, mnemonic, bytes)."""
    out = subprocess.run([DIS, '-mz80n', '-o', f'0x{org:x}', '-s', f'0x{start:x}', '-e', f'0x{end:x}', path],
                         capture_output=True, text=True).stdout
    rows = []
    for ln in out.splitlines():
        m = LINE.match(ln)
        if m:
            mn = re.sub(r'\s+', ' ', m.group(1)).replace(', ', ',')
            by = m.group(3).split()
            addr = int(m.group(2), 16)
            if by[:2] == ['ed', 'b6'] and len(by) == 2:
                mn = 'ldirscale'
            rows.append((addr, mn, by))
    return rows


def generate(name):
    t = targets.TARGETS[name]
    d = loadDict(name)
    path = os.path.join(HERE, t['bin'])
    data = open(path, 'rb').read()
    org, n = t['org'], len(data)
    entries = set(targets.VECTORS) | set(t['entries']) | set(d.ENTRIES)
    for start, count, step in t['tables']:
        for i in range(count):
            p = start - org + i * step
            if data[p] == 0xC3:
                entries.add(start + i * step)
    for start, count in d.WORDTABLES:
        for i in range(count):
            p = start - org + 2 * i
            entries.add(data[p] | (data[p + 1] << 8))
    entries = {e for e in entries if org <= e < org + n}
    cuts0 = {a - org for a in d.DATA}
    carry, carryData = carried(data, org, t.get('carry', []))
    entries |= set(carry)
    forcedata = list(d.FORCEDATA) + [(a, v[3]) for a, v in carryData.items()]
    cuts0 |= {a - org for a in carryData}
    code, starts = walk(data, org, entries, forcedata)
    tentative = set()
    for _ in range(6):
        seeds = sorted(s for s in autoSeeds(data, org, code) if org <= s < org + n and not code[s - org])
        if not seeds:
            break
        before = sum(code)
        for sd in seeds:
            walk(data, org, [sd], forcedata, code, starts, strict=True)
            if code[sd - org]:
                tentative.add(sd)
        if sum(code) == before:
            break
    # gap sweep: an unreached run that starts like code (six plausible instructions) and ends at a terminator
    for _ in range(4):
        changed = False
        i = 0
        while i < n:
            if code[i] or i in cuts0:
                i += 1
                continue
            j = i
            while j < n and not code[j] and j not in cuts0 - {i}:
                j += 1
            seg = data[i:j]
            if len(seg) >= 6 and classify(seg) == 'Data' and plausible(data, i, 6):
                before = sum(code)
                walk(data, org, [org + i], forcedata, code, starts, strict=True)
                if code[i]:
                    tentative.add(org + i)
                    changed = changed or sum(code) != before
            i = j
        if not changed:
            break
    # split points: hand-named data starts and code run boundaries
    cuts = {a - org for a in d.DATA} | {a - org for a in carryData}
    blocks = []                       # (kind, start, end) kind in code / data
    i = 0
    while i < n:
        j = i + 1
        while j < n and code[j] == code[i] and j not in cuts and (code[i] is False or j not in starts or True):
            j += 1
        blocks.append(('code' if code[i] else 'data', i, j))
        i = j
    rows = {}
    for kind, s, e in blocks:
        if kind == 'code':
            for addr, mn, by in decode(path, org, org + s, org + e):
                rows[addr] = (mn, by)
    # code runs must decode exactly onto the walker's boundaries
    for kind, s, e in blocks:
        if kind == 'code':
            pos = s
            while pos < e:
                a = org + pos
                if a not in rows:
                    raise SystemExit(f'{name}: decode gap at #{a:04X}')
                pos += len(rows[a][1])
            assert pos == e, (name, hex(org + s), hex(org + e), hex(org + pos))
    # labels: branch targets inside the image, vectors, entries, hand names
    labels = {}
    for addr, (mn, by) in rows.items():
        m = BRANCH.match(mn)
        if m:
            tgt = int(m.group(3), 16)
            if org <= tgt < org + n:
                labels.setdefault(tgt, f'L{tgt:04X}')
        if mn.startswith('rst '):
            pass
    for e in entries:
        labels.setdefault(e, f'L{e:04X}')
    for a, (nm, cm, mp) in carry.items():
        labels[a] = nm
    for a, v in carryData.items():
        labels[a] = v[0]
    for a in tentative:
        if a in labels and labels[a] == f'L{a:04X}':
            labels[a] = f'T{a:04X}'
    for a, nm in d.NAMES.items():
        labels[a] = nm
    for a, nm in d.DATA.items():
        labels[a] = nm
    lines = []
    lines += d.HEADER or [';' + '=' * 75, f'; {d.TITLE}', f'; image: {t["bin"]}, {n} bytes, origin #{org:04X}', ';' + '=' * 75]
    lines.append('')
    lines.append(f'\torg 0x{org:04x}')
    lines.append('')
    codeBytes = 0
    for kind, s, e in blocks:
        a0 = org + s
        if kind == 'code':
            codeBytes += e - s
            pos = s
            while pos < e:
                a = org + pos
                mn, by = rows[a]
                if a in labels:
                    lines.append('')
                    if a in d.COMMENTS:
                        for c in d.COMMENTS[a].split('\n'):
                            lines.append('; ' + c)
                    elif a in carry and carry[a][1]:
                        lines.append('; ' + carry[a][1] + '   (' + carry[a][2] + ')')
                    elif a in tentative:
                        lines.append('; tentative entry (word / jump table scan)')
                    lines.append(f'{labels[a]}:')
                m = BRANCH.match(mn)
                if m and int(m.group(3), 16) in labels:
                    mn = f'{m.group(1)}{m.group(2)}{labels[int(m.group(3), 16)]}'
                    mn = mn.replace(' ', ' ', 1)
                lines.append('\t%-24s;%04x\t%s' % (mn.replace(',', ', ') if False else mn, a, ' '.join(by)))
                pos += len(by)
        else:
            label = labels.get(a0) or f'{classify(data[s:e])}{a0:04X}'
            if a0 in d.COMMENTS:
                lines.append('')
                for c in d.COMMENTS[a0].split('\n'):
                    lines.append('; ' + c)
            lines.append(f'{label}:')
            for k in range(s, e, 8):
                chunk = data[k:min(e, k + 8)]
                asc = ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)
                lines.append('\tdefb %-40s;%04x\t%s\t%s' % (','.join(f'0x{c:02x}' for c in chunk), org + k,
                                                            ' '.join(f'{c:02x}' for c in chunk), asc))
    text = '\n'.join(lines) + '\n'
    outpath = os.path.join(OUTDIR, d.LISTING)
    os.makedirs(os.path.dirname(outpath), exist_ok=True)
    open(outpath, 'w').write(text)
    # symbol file: name: equ 0xADDR (hand names and entries only)
    os.makedirs(SYMDIR, exist_ok=True)
    with open(os.path.join(SYMDIR, d.SYMFILE), 'w') as f:
        f.write(f'; {d.TITLE}\n')
        for a in sorted(labels):
            if a in d.NAMES or a in d.DATA or a in entries:
                f.write(f'{labels[a]}: equ 0x{a:04X}\n')
    print(f'{name}: {codeBytes}/{n} code bytes ({100 * codeBytes / n:.1f}%), '
          f'{sum(1 for b in blocks if b[0] == "data")} data blocks, {len(labels)} labels -> {d.LISTING}')


if __name__ == '__main__':
    names = sys.argv[1:] or ['all']
    if names == ['all']:
        names = list(targets.TARGETS)
    for nm in names:
        generate(nm)
