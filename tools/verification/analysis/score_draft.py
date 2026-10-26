#!/usr/bin/env python3
"""Score a draft disassembly (SkoolKit ctl or skool) against a reference skool.

  score_draft.py DRAFT REF.skool [--start A] [--end B] [--expected FILE]
                 [--fields FILE] [--geometry FILE] [--json OUT|-]
                 [--split-vars] [--isub]

No agent in the loop: every number comes from the two files (and the optional
side files). Measures, over the program range [start, end):

  1. byte categories (code / data / text / unused / unknown), by entry type and
     by line (sub-block) type, as an agreement percentage and a confusion table;
  2. code entry boundaries: reference `c` entries the draft starts exactly;
  3. graphics regions: reference sections whose comment holds a graphics macro
     (#UDGARRAY, #SPRITE, #SCR, ...), covered by draft data bytes; geometry is
     checked when the draft states it (--geometry, the future export's format);
  4. record field lists against the checked-in expected list (--expected), when
     the draft provides fields (--fields, the future export's format); "n/a"
     otherwise.

Categories: c -> code; b, w, g, s -> data; t -> text; u -> unused; i and bytes
no entry covers -> unknown. --split-vars counts g as its own category "vars"
(the zx-reaper zxcompare.py convention). --isub reads the skool files in @isub
mode (zxcompare.py parses the reference that way, which adds c$7FFD).

Python 3 standard library only.
"""
import argparse
import json
import re
import sys

ENTRY_CTLS = 'bcgistuw'
ENTRY_KIND = {'c': 'code', 'b': 'data', 'w': 'data', 'g': 'data', 's': 'data',
              't': 'text', 'u': 'unused', 'i': 'unknown'}
SUB_KIND = {'B': 'data', 'W': 'data', 'S': 'data', 'T': 'text', 'C': 'code'}
CATEGORIES = ['code', 'data', 'text', 'unused', 'unknown']
CATEGORIES_VARS = ['code', 'data', 'vars', 'text', 'unused', 'unknown']
ASM_BLOCKS = ('isub', 'ssub', 'rsub', 'ofix', 'bfix', 'rfix')
# SkoolKit image macros: a section whose comment holds one of these shows graphics
GRAPHIC_MACROS = re.compile(r'#(UDGARRAY|UDG|FONT|SCR|SPRITE|CAVERN|TILES|PORTAL|ITEM|FRAMES)(?![A-Za-z])')
ADDR = r'(\$[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|\d+)'
SKOOL_INSTR = re.compile(r'^([bcgistuw *!|>])' + ADDR + r'(?:\s+(.*))?$')
CTL_ENTRY = re.compile(r'^([bcgistuw])\s+' + ADDR + r'(?:\s+(.*))?$')
CTL_SUB = re.compile(r'^([BCSTW])\s+' + ADDR + r'(?:,([^\s]*))?(?:\s+(.*))?$')


def parse_int(text):
    text = text.strip()
    if text.startswith('$'):
        return int(text[1:], 16)
    if text.lower().startswith('0x'):
        return int(text[2:], 16)
    if text.startswith('%'):
        return int(text[1:], 2)
    return int(text)


def parse_addr_arg(text):
    return parse_int(text)


def split_operands(text):
    """Split DEFB/DEFM/DEFW/DEFS operands at commas outside quotes."""
    items, cur, quoted, esc = [], '', False, False
    for ch in text:
        if quoted:
            cur += ch
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == '"':
                quoted = False
        elif ch == '"':
            quoted = True
            cur += ch
        elif ch == ',':
            items.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        items.append(cur.strip())
    return items


def strip_comment(text):
    """Return the instruction part of a skool line body (before an unquoted ';')."""
    quoted, esc = False, False
    for n, ch in enumerate(text):
        if quoted:
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == '"':
                quoted = False
        elif ch == '"':
            quoted = True
        elif ch == ';':
            return text[:n].strip(), text[n + 1:].strip()
    return text.strip(), ''


def string_len(item):
    body = item[1:-1] if item.endswith('"') and len(item) >= 2 else item[1:]
    n, esc = 0, False
    for ch in body:
        if esc:
            esc = False
            n += 1
        elif ch == '\\':
            esc = True
        else:
            n += 1
    return n


def def_size(op):
    """Size in bytes of a DEFB/DEFM/DEFW/DEFS statement; None for an instruction."""
    m = re.match(r'(DEF[BMWS])\b\s*(.*)$', op, re.I)
    if not m:
        return None
    kind, args = m.group(1).upper(), split_operands(m.group(2))
    if kind == 'DEFS':
        try:
            return parse_int(args[0]) if args else 0
        except ValueError:
            return None
    if kind == 'DEFW':
        return 2 * len(args)
    size = 0
    for item in args:
        size += string_len(item) if item.startswith('"') else 1
    return size


def line_kind(entry_ctl, op):
    """Category of one skool line: code instruction, DEFM text, other DEF* data."""
    if entry_ctl == 'u':
        return 'unused'
    if entry_ctl == 'i':
        return 'unknown'
    word = op.split(None, 1)[0].upper() if op else ''
    if word == 'DEFM':
        return 'text'
    if word in ('DEFB', 'DEFW', 'DEFS'):
        return 'data'
    return 'code'


class Entry:
    def __init__(self, ctl, address, title=''):
        self.ctl = ctl
        self.address = address
        self.title = title
        self.header = []            # comment lines above the entry
        self.lines = []             # (address, op) for skool, sub-blocks for ctl
        self.sections = []          # (start address, comment text) split at mid-block comments
        self.end = None


class Draft:
    """A disassembly as byte maps plus its entries."""

    def __init__(self, path, fmt):
        self.path = path
        self.format = fmt
        self.entries = []
        self.entry_cat = {}         # address -> category by entry type
        self.line_cat = {}          # address -> category by line / sub-block type
        self.entry_ctl = {}         # address -> entry ctl char
        self.graphic_claims = []    # (start, end, macro text) from draft comments
        self.boundaries = set()     # entry starts, sub-block starts (ctl), mid-block comment starts (skool)


def include_state(stack, modes):
    return all(sign == modes[name] for name, sign in stack)


def parse_skool(path, isub=False):
    modes = {name: '-' for name in ASM_BLOCKS}
    if isub:
        modes['isub'] = '+'
    stack = []
    entries = []
    pending = []
    entry = None
    in_entry_body = False
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            line = raw.rstrip('\n').rstrip()
            if line.startswith('@'):
                m = re.match(r'@(' + '|'.join(ASM_BLOCKS) + r')([+-])(begin|else|end)\b', line)
                if m:
                    name, sign, what = m.groups()
                    if what == 'begin':
                        stack.append((name, sign))
                    elif what == 'else' and stack:
                        stack[-1] = (name, sign)
                    elif what == 'end' and stack:
                        stack.pop()
                continue
            if not include_state(stack, modes):
                continue
            if not line:
                entry = None
                in_entry_body = False
                pending = []
                continue
            if line.startswith(';'):
                text = line[1:].strip()
                if entry is not None and in_entry_body:
                    pending.append(text)          # mid-block comment
                else:
                    pending.append(text)          # entry header
                continue
            if line[0] in ' \t' and line.strip().startswith(';'):
                continue                          # instruction comment continuation
            m = SKOOL_INSTR.match(line)
            if not m:
                continue
            ctl, addr_text, body = m.group(1), m.group(2), m.group(3) or ''
            try:
                address = parse_int(addr_text)
            except ValueError:
                continue
            op, _comment = strip_comment(body)
            if ctl in ENTRY_CTLS:
                entry = Entry(ctl, address, pending[0] if pending else '')
                entry.header = pending
                entry.sections.append((address, '\n'.join(pending)))
                entries.append(entry)
                pending = []
                in_entry_body = True
            elif entry is None:
                continue
            elif pending:
                entry.sections.append((address, '\n'.join(pending)))
                pending = []
            entry.lines.append((address, op))
    return entries


def build_skool(path, isub=False):
    d = Draft(path, 'skool')
    entries = parse_skool(path, isub)
    d.entries = entries
    flat = []
    for e in entries:
        for address, op in e.lines:
            flat.append((address, op, e))
    flat.sort(key=lambda x: x[0])
    for n, (address, op, e) in enumerate(flat):
        nxt = flat[n + 1][0] if n + 1 < len(flat) else None
        size = def_size(op)
        if size is None:
            # an instruction: its size is the distance to the next line when that
            # line is close (skool lines are contiguous); a last line counts 1 byte
            size = (nxt - address) if nxt is not None and 0 < nxt - address <= 4 else 1
        end = address + size
        if nxt is not None and nxt > address:
            end = min(end, nxt)
        e.end = max(e.end or 0, end)
        ecat = ENTRY_KIND[e.ctl]
        lcat = line_kind(e.ctl, op)
        for a in range(address, min(end, 0x10000)):
            d.entry_cat[a] = ecat
            d.line_cat[a] = lcat
            d.entry_ctl[a] = e.ctl
    for e in entries:
        d.boundaries.update(s for s, _ in e.sections)
        starts = [s for s, _ in e.sections] + [e.end or e.address]
        for k, (start, text) in enumerate(e.sections):
            m = GRAPHIC_MACROS.search(text)
            if m:
                d.graphic_claims.append((start, starts[k + 1], m.group(0)))
    return d


def build_ctl(path, end_default=0x10000):
    d = Draft(path, 'ctl')
    entries = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            line = raw.rstrip()
            m = CTL_ENTRY.match(line)
            if m:
                try:
                    e = Entry(m.group(1), parse_int(m.group(2)), m.group(3) or '')
                except ValueError:
                    continue
                entries.append(e)
                continue
            m = CTL_SUB.match(line)
            if m and entries:
                try:
                    address = parse_int(m.group(2))
                except ValueError:
                    continue
                length = None
                if m.group(3):
                    first = m.group(3).split(',')[0]
                    try:
                        length = parse_int(first)
                    except ValueError:
                        length = None
                entries[-1].lines.append((address, m.group(1), length))
    entries.sort(key=lambda e: e.address)
    d.entries = entries
    for n, e in enumerate(entries):
        e.end = entries[n + 1].address if n + 1 < len(entries) else end_default
        ecat = ENTRY_KIND[e.ctl]
        for a in range(e.address, min(e.end, 0x10000)):
            d.entry_cat[a] = ecat
            d.line_cat[a] = ecat if e.ctl != 'c' else 'code'
            d.entry_ctl[a] = e.ctl
        d.boundaries.add(e.address)
        if e.ctl in 'ui':
            continue
        subs = sorted(e.lines, key=lambda s: s[0])
        d.boundaries.update(s[0] for s in subs)
        for k, (address, sub, length) in enumerate(subs):
            stop = subs[k + 1][0] if k + 1 < len(subs) else e.end
            if length is not None:
                stop = min(stop, address + length)
            for a in range(max(address, e.address), min(stop, e.end, 0x10000)):
                d.line_cat[a] = SUB_KIND[sub]
    return d


def load_draft(path, isub=False):
    with open(path, encoding='utf-8', errors='replace') as f:
        head = f.read(65536)
    fmt = 'ctl'
    for line in head.splitlines():
        if SKOOL_INSTR.match(line) and not CTL_ENTRY.match(line):
            fmt = 'skool'
            break
        if re.match(r'^[bcgistuw]\$?[0-9]', line):
            fmt = 'skool'
            break
    if path.endswith('.skool'):
        fmt = 'skool'
    elif path.endswith('.ctl'):
        fmt = 'ctl'
    return build_skool(path, isub) if fmt == 'skool' else build_ctl(path)


# ---------------------------------------------------------------- measures

def category_scores(draft, ref, lo, hi, which, split_vars):
    dmap = draft.entry_cat if which == 'entry' else draft.line_cat
    rmap = ref.entry_cat if which == 'entry' else ref.line_cat
    cats = CATEGORIES_VARS if split_vars else CATEGORIES

    def cat(d, a, m):
        c = m.get(a, 'unknown')
        if split_vars and c == 'data' and d.entry_ctl.get(a) == 'g':
            return 'vars'
        return c

    table = {r: {c: 0 for c in cats} for r in cats}
    agree = scored = 0
    code_agree = 0
    for a in range(lo, hi):
        dc, rc = cat(draft, a, dmap), cat(ref, a, rmap)
        table[dc][rc] += 1
        if (dc == 'code') == (rc == 'code'):
            code_agree += 1
        if rc == 'unknown':
            continue
        scored += 1
        if dc == rc:
            agree += 1
    return {
        'level': which,
        'categories': cats,
        'confusion': table,          # table[draft][reference]
        'bytes_in_range': hi - lo,
        'bytes_scored': scored,
        'bytes_agree': agree,
        'agree_pct': round(100.0 * agree / scored, 2) if scored else None,
        'code_vs_noncode_pct': round(100.0 * code_agree / (hi - lo), 2) if hi > lo else None,
    }


def code_entry_scores(draft, ref, lo, hi):
    rc = sorted(e.address for e in ref.entries if e.ctl == 'c' and lo <= e.address < hi)
    dc = sorted(e.address for e in draft.entries if e.ctl == 'c' and lo <= e.address < hi)
    dset = set(dc)
    rset = set(rc)
    matched = [a for a in rc if a in dset]
    return {
        'reference': len(rc),
        'draft': len(dc),
        'matched': len(matched),
        'misses': ['$%04X' % a for a in rc if a not in dset],
        'extra': ['$%04X' % a for a in dc if a not in rset],
    }


def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def addr_of(value):
    return parse_int(value) if isinstance(value, str) else int(value)


def geometry_lookup(geometry, start, end):
    """Hook for the future export: the draft's stated geometry for a region.

    `geometry` is a list of {"start", "end", "kind", "width", "height", "frames"}
    (width in pixels, height in pixel rows); a region matches when its start lies
    inside [start, end) of the reference section.
    """
    if not geometry:
        return None
    for g in geometry:
        gs = addr_of(g['start'])
        if start <= gs < end:
            return g
    return None


def graphics_scores(draft, ref, lo, hi, geometry, expected_geometry):
    regions = []
    for start, end, macro in sorted(ref.graphic_claims):
        if not (lo <= start < hi):
            continue
        end = min(end, hi)
        size = end - start
        if size <= 0:
            continue
        data = sum(1 for a in range(start, end) if draft.entry_cat.get(a) == 'data')
        line_data = sum(1 for a in range(start, end) if draft.line_cat.get(a) == 'data')
        boundary = start in draft.boundaries
        status = 'covered' if data == size else ('partial' if data else 'missed')
        exp = None
        for g in expected_geometry or []:
            if addr_of(g['start']) == start:
                exp = g
        dg = geometry_lookup(geometry, start, end)
        geo = 'n/a'
        if exp is not None and dg is not None:
            keys = [k for k in ('width', 'height', 'frames') if k in exp]
            geo = 'match' if all(dg.get(k) == exp[k] for k in keys) else 'mismatch'
        elif exp is not None:
            geo = 'not stated'
        regions.append({'start': '$%04X' % start, 'end': '$%04X' % (end - 1), 'size': size, 'macro': macro,
                        'data_bytes': data, 'line_data_bytes': line_data, 'status': status,
                        'draft_boundary': bool(boundary), 'geometry': geo})
    covered = sum(1 for r in regions if r['status'] == 'covered')
    bounded = sum(1 for r in regions if r['draft_boundary'])
    geo_checked = [r for r in regions if r['geometry'] in ('match', 'mismatch')]
    return {
        'regions': regions,
        'total': len(regions),
        'covered': covered,
        'partial': sum(1 for r in regions if r['status'] == 'partial'),
        'missed': sum(1 for r in regions if r['status'] == 'missed'),
        'with_draft_boundary': bounded,
        'geometry_checked': len(geo_checked),
        'geometry_match': sum(1 for r in geo_checked if r['geometry'] == 'match'),
        'geometry_source': 'draft --geometry' if geometry else 'n/a (draft states no geometry)',
    }


def record_scores(expected, fields):
    """Score the draft's record field lists (--fields) against the expected list.

    Draft format (future export): {"records": [{"name", "base", "layout"?,
    "fields": [{"offset", "size", "name"?}]}]}. A draft record belongs to an
    expected layout when its base is the layout's base or one of its instances,
    or when its name and layout name equal the expected ones. A field matches on
    offset and size; names are not scored.
    """
    if not expected:
        return {'status': 'n/a (no --expected)', 'records': [], 'complete': None, 'total': 0}
    draft_records = (fields or {}).get('records', [])
    out = []
    for rec in expected['records']:
        layouts_out = []
        for layout in rec['layouts']:
            base = layout.get('base', rec['base'])
            bases = {addr_of(base)} | {addr_of(x) for x in layout.get('instances', rec.get('instances', []))}
            exp = [(f['offset'], f['size'], f['name']) for f in layout['fields']]
            if fields is None:
                layouts_out.append({'layout': layout['name'], 'expected_fields': len(exp), 'matched': None})
                continue
            cand = [r for r in draft_records
                    if ('base' in r and addr_of(r['base']) in bases)
                    or (r.get('name') == rec['name'] and r.get('layout', layout['name']) == layout['name'])]
            got = {(int(f['offset']), int(f['size'])) for r in cand for f in r.get('fields', [])}
            missing = ['+%d/%d %s' % (o, n, name) for o, n, name in exp if (o, n) not in got]
            layouts_out.append({'layout': layout['name'], 'expected_fields': len(exp),
                                'matched': len(exp) - len(missing), 'found': bool(cand), 'missing': missing})
        total = sum(x['expected_fields'] for x in layouts_out)
        matched = None if fields is None else sum(x['matched'] for x in layouts_out)
        out.append({'record': rec['name'], 'expected_fields': total, 'matched': matched, 'layouts': layouts_out})
    complete = None if fields is None else sum(1 for r in out if r['matched'] == r['expected_fields'])
    return {'status': 'n/a (draft provides no fields)' if fields is None else 'scored',
            'records': out, 'complete': complete, 'total': len(out)}


# ---------------------------------------------------------------- output

def print_table(res, out):
    cats = res['categories']
    out.write('| draft \\ reference | %s |\n' % ' | '.join(cats))
    out.write('| --- |%s\n' % (' ---: |' * len(cats)))
    for r in cats:
        out.write('| %s | %s |\n' % (r, ' | '.join(str(res['confusion'][r][c]) for c in cats)))


def report_text(rep, out, verbose=False):
    out.write('Draft: %s (%s)\nReference: %s\nRange: $%04X-$%04X (%d bytes)\n\n' % (
        rep['draft'], rep['draft_format'], rep['reference'], rep['range'][0], rep['range'][1] - 1,
        rep['range'][1] - rep['range'][0]))
    for key, title in (('categories_entry', 'Byte categories by entry type'),
                       ('categories_line', 'Byte categories by line / sub-block type')):
        res = rep[key]
        out.write('## %s: %s %% agree (%d / %d bytes), code vs non-code %s %%\n\n' % (
            title, res['agree_pct'], res['bytes_agree'], res['bytes_scored'], res['code_vs_noncode_pct']))
        print_table(res, out)
        out.write('\n')
    ce = rep['code_entries']
    out.write('## Code entries: %d of %d reference `c` entries matched (draft has %d)\n\n' % (
        ce['matched'], ce['reference'], ce['draft']))
    out.write('Misses: %s\nDraft only: %s\n\n' % (' '.join(ce['misses']) or '-', ' '.join(ce['extra']) or '-'))
    g = rep['graphics']
    out.write('## Graphics regions: %d of %d covered by draft data (%d partial, %d missed), '
              '%d start at a draft boundary; geometry: %s\n\n' % (
                  g['covered'], g['total'], g['partial'], g['missed'], g['with_draft_boundary'],
                  ('%d of %d match' % (g['geometry_match'], g['geometry_checked']))
                  if g['geometry_checked'] else g['geometry_source']))
    groups = {}
    for r in g['regions']:
        x = groups.setdefault(r['macro'], {'n': 0, 'covered': 0, 'partial': 0, 'missed': 0, 'boundary': 0,
                                           'geo': 0, 'geo_ok': 0})
        x['n'] += 1
        x[r['status']] += 1
        x['boundary'] += r['draft_boundary']
        x['geo'] += r['geometry'] in ('match', 'mismatch')
        x['geo_ok'] += r['geometry'] == 'match'
    out.write('| reference macro | regions | covered | partial | missed | start at a draft boundary | geometry match |\n')
    out.write('| --- | ---: | ---: | ---: | ---: | ---: | ---: |\n')
    for macro in sorted(groups):
        x = groups[macro]
        out.write('| %s | %d | %d | %d | %d | %d | %s |\n' % (
            macro, x['n'], x['covered'], x['partial'], x['missed'], x['boundary'],
            '%d / %d' % (x['geo_ok'], x['geo']) if x['geo'] else 'n/a'))
    out.write('\n')
    if verbose:
        out.write('| start | end | size | macro | draft data bytes | status | boundary | geometry |\n')
        out.write('| --- | --- | ---: | --- | ---: | --- | --- | --- |\n')
        for r in g['regions']:
            out.write('| %s | %s | %d | %s | %d | %s | %s | %s |\n' % (
                r['start'], r['end'], r['size'], r['macro'], r['data_bytes'], r['status'],
                'yes' if r['draft_boundary'] else 'no', r['geometry']))
        out.write('\n')
    rs = rep['records']
    out.write('## Record field lists: %s\n\n' % (
        rs['status'] if rs.get('complete') is None else '%d of %d complete' % (rs['complete'], rs['total'])))
    if rs['records']:
        out.write('| record | expected fields | matched |\n| --- | ---: | --- |\n')
        for r in rs['records']:
            out.write('| %s | %d | %s |\n' % (r['record'], r['expected_fields'],
                                             'n/a' if r['matched'] is None else r['matched']))
        out.write('\n')


def score(draft_path, ref_path, start=None, end=None, expected_path=None, fields_path=None,
          geometry_path=None, split_vars=False, isub=False):
    draft = load_draft(draft_path, isub)
    ref = build_skool(ref_path, isub)
    expected = load_json(expected_path) if expected_path else None
    if start is None:
        start = addr_of(expected['range'][0]) if expected and 'range' in expected else \
            max(min(draft.entry_cat), min(ref.entry_cat))
    if end is None:
        end = addr_of(expected['range'][1]) if expected and 'range' in expected else \
            min(max(draft.entry_cat), max(ref.entry_cat)) + 1
    fields = load_json(fields_path) if fields_path else None
    geometry = load_json(geometry_path).get('regions', []) if geometry_path else None
    return {
        'draft': draft_path,
        'draft_format': draft.format,
        'reference': ref_path,
        'range': [start, end],
        'split_vars': split_vars,
        'isub': isub,
        'categories_entry': category_scores(draft, ref, start, end, 'entry', split_vars),
        'categories_line': category_scores(draft, ref, start, end, 'line', split_vars),
        'code_entries': code_entry_scores(draft, ref, start, end),
        'graphics': graphics_scores(draft, ref, start, end, geometry,
                                    expected.get('graphics') if expected else None),
        'records': record_scores(expected, fields),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('draft', help='draft .ctl or .skool')
    ap.add_argument('reference', help='reference .skool')
    ap.add_argument('--start', type=parse_addr_arg, help='program range start (default: --expected range)')
    ap.add_argument('--end', type=parse_addr_arg, help='program range end, exclusive')
    ap.add_argument('--expected', help='expected records / graphics / range JSON')
    ap.add_argument('--fields', help="draft's record field lists JSON (future export)")
    ap.add_argument('--geometry', help="draft's graphics geometry JSON (future export)")
    ap.add_argument('--json', metavar='OUT', help='write the report as JSON to OUT (- = stdout)')
    ap.add_argument('--split-vars', action='store_true', help='count g entries as their own category')
    ap.add_argument('--isub', action='store_true', help='read skool files in @isub mode')
    ap.add_argument('-v', '--verbose', action='store_true', help='list every graphics region')
    a = ap.parse_args(argv)
    rep = score(a.draft, a.reference, a.start, a.end, a.expected, a.fields, a.geometry, a.split_vars, a.isub)
    if a.json == '-':
        json.dump(rep, sys.stdout, indent=1)
        sys.stdout.write('\n')
        return 0
    report_text(rep, sys.stdout, a.verbose)
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)
            f.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
