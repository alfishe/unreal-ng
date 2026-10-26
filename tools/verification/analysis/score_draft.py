#!/usr/bin/env python3
"""Score a draft disassembly (SkoolKit ctl or skool) against a reference skool.

  score_draft.py DRAFT REF.skool [--start A] [--end B] [--expected FILE]
                 [--fields FILE] [--geometry FILE] [--ref FILE ...]
                 [--json OUT|-] [--split-vars] [--isub]

No agent in the loop: every number comes from the two files (and the optional
side files). Measures, over the program range [start, end):

  1. byte categories (code / data / text / unused / unknown), by entry type and
     by line (sub-block) type, as an agreement percentage and a confusion table;
  2. code entry boundaries: reference `c` entries the draft starts exactly;
  3. graphics regions: reference sections whose comment holds a graphics macro
     (#UDGARRAY, #SPRITE, #SCR, ...), covered by draft data bytes; geometry
     (width, height, frames) checked against the draft's --geometry JSON. The
     reference geometry comes from the section's own image macros (#UDG,
     #UDGARRAY, #SCR, #FONT, after expanding the game's #DEF macros from the
     .ref next to the reference skool or --ref), or from the expected file's
     `graphics` list when it names the section;
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
        self.graphic_text = {}      # start -> the section's comment text
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
                d.graphic_text[start] = text
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


# ---------------------------------------------------------------- reference geometry from image macros

def balanced(text, i):
    """The end (exclusive) of the parenthesised group starting at text[i] == '('; None when unbalanced."""
    depth = 0
    for k in range(i, len(text)):
        if text[k] == '(':
            depth += 1
        elif text[k] == ')':
            depth -= 1
            if depth == 0:
                return k + 1
    return None


def split_top(text, sep):
    """Split at `sep` outside parentheses."""
    out, depth, cur = [], 0, ''
    for ch in text:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


def eval_int(expr):
    """An integer arithmetic expression (decimal, $hex, #hex); None when it holds anything else."""
    e = re.sub(r'\$([0-9A-Fa-f]+)', lambda m: str(int(m.group(1), 16)), expr.strip())
    e = re.sub(r'#([0-9A-Fa-f]+)\b', lambda m: str(int(m.group(1), 16)), e)
    if not e or not re.fullmatch(r'[0-9+\-*/%()&|<> ]+', e):
        return None
    try:
        value = eval(e.replace('/', '//'), {'__builtins__': {}}, {})  # noqa: S307 - digits and operators only
    except Exception:  # noqa: BLE001
        return None
    return int(value)


class MacroDef:
    def __init__(self, name, params, body):
        self.name = name
        self.params = params        # [(name, default or None)]
        self.body = body


def parse_defs(text):
    """The #DEF(#NAME(params)(string params) body) definitions in `text`."""
    defs = {}
    k = 0
    while True:
        k = text.find('#DEF(', k)
        if k < 0:
            return defs
        end = balanced(text, k + 4)
        if end is None:
            return defs
        inner = text[k + 5:end - 1]
        k = end
        m = re.match(r'#([A-Z][A-Z0-9]*)', inner)
        if not m:
            continue
        name, j = m.group(1), m.group(0).__len__()
        params = []
        for _ in range(2):          # numeric params, then string params
            if j < len(inner) and inner[j] == '(':
                e = balanced(inner, j)
                if e is None:
                    break
                for item in split_top(inner[j + 1:e - 1], ','):
                    item = item.strip()
                    if item:
                        n, _, d = item.partition('=')
                        params.append((n.strip(), d.strip() if _ else None))
                j = e
        defs[name] = MacroDef(name, params, inner[j:].strip())


def load_defs(paths, skool_path=None):
    """#DEFs from .ref files ([Expand] and anything else) and the skool's own @expand lines."""
    text = ''
    for p in paths or []:
        with open(p, encoding='utf-8', errors='replace') as f:
            text += f.read() + '\n'
    if skool_path:
        with open(skool_path, encoding='utf-8', errors='replace') as f:
            for line in f:
                if line.startswith('@expand='):
                    text += line[len('@expand='):] + '\n'
    return parse_defs(text)


def expand_for(text):
    """#FOR(start,stop[,step])(var,body[,sep]) and the delimiter form #FOR(a,b)//var/body/sep//."""
    while True:
        m = re.search(r'#FOR\(', text)
        if not m:
            return text
        e = balanced(text, m.end() - 1)
        if e is None:
            return text
        bounds = [eval_int(x) for x in split_top(text[m.end():e - 1], ',')]
        if len(bounds) < 2 or None in bounds:
            return text[:m.start()] + text[e:]
        step = bounds[2] if len(bounds) > 2 else 1
        rest = text[e:]
        if rest.startswith('('):
            e2 = balanced(rest, 0)
            parts = split_top(rest[1:e2 - 1], ',') if e2 else []
            tail = rest[e2:] if e2 else ''
        elif rest:
            delim = rest[0]
            fields = rest[1:].split(delim)
            # //n/body/sep// -> '', 'n', 'body', 'sep', '', ...
            parts = fields[1:4] if fields and fields[0] == '' else fields[:3]
            consumed = 1 + len(delim.join(fields[:5])) + 1 if fields and fields[0] == '' else 0
            tail = rest[consumed:]
        else:
            parts, tail = [], ''
        var = parts[0].strip() if parts else 'n'
        body = parts[1] if len(parts) > 1 else ''
        sep = parts[2] if len(parts) > 2 else ''
        items = [re.sub(r'\b%s\b' % re.escape(var), str(v), body)
                 for v in range(bounds[0], bounds[1] + 1, step or 1)]
        text = text[:m.start()] + sep.join(items) + tail


def expand_macros(text, defs, depth=0):
    """Replace calls of the defined macros with their bodies (arguments and #LET values substituted)."""
    if depth > 8 or not defs:
        return expand_for(text)
    names = sorted(defs, key=len, reverse=True)
    pattern = re.compile(r'#(%s)(?![A-Z])' % '|'.join(re.escape(n) for n in names))
    out, k = '', 0
    for m in pattern.finditer(text):
        if m.start() < k:
            continue
        d = defs[m.group(1)]
        j = m.end()
        args = []
        if j < len(text) and text[j] == '(':
            e = balanced(text, j)
            if e:
                args = [a.strip() for a in split_top(text[j + 1:e - 1], ',')]
                j = e
        else:
            a = re.match(r'[0-9$#,]+', text[j:])
            if a:
                args = [x for x in a.group(0).split(',')]
                j += a.end()
        if j < len(text) and text[j] == '(':          # the string arguments (file names)
            e = balanced(text, j)
            if e:
                j = e
        values = {}
        for n, (pname, default) in enumerate(d.params):
            if n < len(args) and args[n] != '':
                values[pname] = args[n]
            elif default is not None:
                values[pname] = default
        body = d.body
        for pname in sorted(values, key=len, reverse=True):
            body = body.replace('$' + pname, values[pname])
        lets = {}
        for lm in re.finditer(r'#LET\((\w+)=([^)]*(?:\([^)]*\)[^)]*)*)\)', body):
            v = eval_int(re.sub(r'\{(\w+)\}', lambda x: str(lets.get(x.group(1), x.group(0))), lm.group(2)))
            if v is not None:
                lets[lm.group(1)] = v
        body = re.sub(r'\{(\w+)\}', lambda x: str(lets[x.group(1)]) if x.group(1) in lets else x.group(0), body)
        body = re.sub(r'#EVAL(\d+)', r'\1', body)
        out += text[k:m.start()] + expand_macros(body, defs, depth + 1)
        k = j
    return expand_for(out + text[k:])


def macro_params(text, j):
    """The parameters of an image macro at text[j:] (bare or in parentheses) and where they end."""
    if j < len(text) and text[j] == '(':
        e = balanced(text, j)
        if e is None:
            return [], j
        return split_top(text[j + 1:e - 1], ','), e
    m = re.match(r'[0-9$#,a-z=]*', text[j:])
    return (m.group(0).split(',') if m.group(0) else []), j + m.end()


def udg_count(spec, width=1):
    """How many UDGs a #UDGARRAY spec list names, as SkoolKit's parse_address_range counts them
    (addr; a-b; a-b-c; a-b-c-d with c the step across and d the step down a row of `width`; xN repeats)."""
    count = 0
    width = max(width, 1)
    for item in split_top(spec, ';'):
        item = split_top(item, ':')[0]          # a mask spec
        item = split_top(item, ',')[0].strip()  # attribute, step, inc
        if not item:
            continue
        num = 1
        rep_m = re.fullmatch(r'(.+)x(\d+)', item)
        if rep_m:
            item, num = rep_m.group(1), int(rep_m.group(2))
        parts = [eval_int(p) for p in split_top(item, '-')][:4]
        if not parts or None in parts:
            count += num
            continue
        if len(parts) < 2:
            parts.append(parts[0])
        if len(parts) < 3:
            parts.append(1)
        if len(parts) < 4:
            parts.append(parts[2] * width)
        address, end_address, h_step, v_step = parts
        n = 0
        while address <= end_address and n < 65536:
            n += 1
            address += h_step if n % width else v_step - (width - 1) * h_step
        count += n * num
    return count


def image_macros(text):
    """The images of an expanded comment: [(macro, width px, height px, extra)] in order."""
    images = []
    for m in re.finditer(r'#(UDGARRAY|UDG|SCR|FONT)(?![A-Za-z])', text):
        name = m.group(1)
        params, j = macro_params(text, m.end())
        if name == 'UDGARRAY':
            if j >= len(text) or text[j] != '(':
                continue
            e = balanced(text, j)
            if e is None:
                continue
            width = eval_int(params[0]) if params else None
            n = udg_count(text[j + 1:e - 1], width or 1)
            if not width or not n:
                continue
            images.append(('UDGARRAY', width * 8, -(-n // width) * 8, {'cells': width}))
        elif name == 'UDG':
            images.append(('UDG', 8, 8, {}))
        elif name == 'FONT':
            chars = None
            pos = [p for p in params if '=' not in p]
            kw = dict(p.split('=', 1) for p in params if '=' in p)
            if len(pos) > 1:
                chars = eval_int(pos[1])
            chars = chars or eval_int(kw.get('chars', '') or '0') or 96
            images.append(('FONT', 8, 8, {'frames': chars}))
        else:  # SCR scale,x,y,w,h,df,af
            names = ['scale', 'x', 'y', 'w', 'h', 'df', 'af']
            values = {'scale': 1, 'x': 0, 'y': 0, 'w': 32, 'h': 24}
            for n, p in enumerate(params):
                key, eq, val = p.partition('=')
                if eq:
                    v = eval_int(val)
                    if v is not None:
                        values[key.strip()] = v
                elif n < len(names) and p.strip():
                    v = eval_int(p)
                    if v is not None:
                        values[names[n]] = v
            images.append(('SCR', values['w'] * 8, values['h'] * 8, {'y': values['y']}))
    return images


def section_geometry(text, size, defs):
    """The reference geometry of one graphics section: {"kind", "width", "height", "frames"} or None.

    One kind of image repeated n times: n frames of its width x height (pixels). #SCR parts stacked
    vertically are one picture. A composite picture (#OVER / #COPY onto a background #UDGARRAY of w cells)
    is a map of one byte per cell: w x size / w cells. Only the first image table counts when there is one."""
    expanded = expand_macros(text, defs)
    # The first image table describes the data; later ones are extra views (unused patterns revealed in
    # another colour, a cavern drawn with its guardians).
    first_table = re.search(r'#UDGTABLE(.*?)TABLE#', expanded, re.S)
    if first_table and image_macros(first_table.group(1)):
        expanded = first_table.group(1)
    images = image_macros(expanded)
    if not images:
        return None
    if re.search(r'#(OVER|COPY)\(', expanded):
        cells = [x for x in images if x[0] == 'UDGARRAY' and x[3].get('cells', 0) >= 16]
        if cells:
            w = cells[0][3]['cells']
            return {'kind': 'map', 'width': w, 'height': size // w, 'frames': 1}
    first = images[0]
    if first[0] == 'SCR':
        scr = [x for x in images if x[0] == 'SCR']
        top = min(x[3]['y'] for x in scr)
        bottom = max(x[3]['y'] * 8 + x[2] for x in scr)
        return {'kind': 'screen', 'width': max(x[1] for x in scr), 'height': bottom - top * 8, 'frames': 1}
    if first[0] == 'FONT':
        return {'kind': 'font', 'width': 8, 'height': 8, 'frames': first[3]['frames']}
    same = [x for x in images if (x[1], x[2]) == (first[1], first[2])]
    return {'kind': 'graphic', 'width': first[1], 'height': first[2], 'frames': len(same)}


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
    """The draft's stated geometry for a reference section [start, end).

    `geometry` is a list of {"start", "end", "kind", "width", "height", "frames"}
    (graphics: width in pixels, height in pixel rows; level maps: cells; "end"
    exclusive). A region belongs to the section when its start lies inside it; of
    several, the one covering most of the section (the first of equals).
    """
    if not geometry:
        return None
    best, best_overlap = None, -1
    for g in geometry:
        gs = addr_of(g['start'])
        if not start <= gs < end:
            continue
        ge = addr_of(g['end']) if 'end' in g else gs + 1
        overlap = min(ge, end) - gs
        if overlap > best_overlap:
            best, best_overlap = g, overlap
    return best


def geometry_matches(draft_g, ref_g):
    """Width, height and frames equal (a map: width and height in cells)."""
    keys = ['width', 'height'] if ref_g.get('kind') in ('map', 'attribute_map') else ['width', 'height', 'frames']
    return all(draft_g.get(k) == ref_g[k] for k in keys if k in ref_g)


def graphics_scores(draft, ref, lo, hi, geometry, expected_geometry, defs=None):
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
        from_macros = section_geometry(ref.graphic_text.get(start, ''), size, defs)
        if exp is None:
            exp = from_macros
        dg = geometry_lookup(geometry, start, end)
        geo = 'n/a'
        if exp is not None and dg is not None:
            geo = 'match' if geometry_matches(dg, exp) else 'mismatch'
        elif exp is not None:
            geo = 'not stated'
        row = {'start': '$%04X' % start, 'end': '$%04X' % (end - 1), 'size': size, 'macro': macro,
               'data_bytes': data, 'line_data_bytes': line_data, 'status': status,
               'draft_boundary': bool(boundary), 'geometry': geo}
        if exp is not None:
            row['reference_geometry'] = {k: exp[k] for k in ('kind', 'width', 'height', 'frames') if k in exp}
        if from_macros is not None:
            row['macro_geometry'] = from_macros
        if dg is not None:
            row['draft_geometry'] = {k: dg[k] for k in ('start', 'kind', 'width', 'height', 'frames') if k in dg}
        regions.append(row)
    covered = sum(1 for r in regions if r['status'] == 'covered')
    bounded = sum(1 for r in regions if r['draft_boundary'])
    geo_checked = [r for r in regions if r['geometry'] in ('match', 'mismatch')]
    geo_known = [r for r in regions if r['geometry'] != 'n/a']
    return {
        'regions': regions,
        'total': len(regions),
        'covered': covered,
        'partial': sum(1 for r in regions if r['status'] == 'partial'),
        'missed': sum(1 for r in regions if r['status'] == 'missed'),
        'with_draft_boundary': bounded,
        'geometry_checked': len(geo_checked),
        'geometry_match': sum(1 for r in geo_checked if r['geometry'] == 'match'),
        'geometry_known': len(geo_known),
        'geometry_match_pct': round(100.0 * sum(1 for r in geo_checked if r['geometry'] == 'match') / len(geo_known), 2)
        if geo_known else None,
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
                  ('%d of %d right (%s %%; %d stated by the draft)' % (
                      g['geometry_match'], g['geometry_known'], g['geometry_match_pct'], g['geometry_checked']))
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
        out.write('| start | end | size | macro | draft data bytes | status | boundary | geometry | reference | draft |\n')
        out.write('| --- | --- | ---: | --- | ---: | --- | --- | --- | --- | --- |\n')

        def shape(x):
            return '%sx%s/%s' % (x.get('width'), x.get('height'), x.get('frames')) if x else '-'
        for r in g['regions']:
            out.write('| %s | %s | %d | %s | %d | %s | %s | %s | %s | %s |\n' % (
                r['start'], r['end'], r['size'], r['macro'], r['data_bytes'], r['status'],
                'yes' if r['draft_boundary'] else 'no', r['geometry'], shape(r.get('reference_geometry')),
                shape(r.get('draft_geometry'))))
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


def default_refs(ref_path):
    """The .ref files next to a reference skool with its stem (mm.skool -> mm.ref)."""
    stem = re.sub(r'\.skool$', '', ref_path)
    return [stem + '.ref'] if stem != ref_path and __import__('os').path.exists(stem + '.ref') else []


def score(draft_path, ref_path, start=None, end=None, expected_path=None, fields_path=None,
          geometry_path=None, split_vars=False, isub=False, ref_files=None, macro_geometry=False):
    draft = load_draft(draft_path, isub)
    ref = build_skool(ref_path, isub)
    defs = load_defs(default_refs(ref_path) if ref_files is None else ref_files, ref_path)
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
                                    expected.get('graphics') if expected and not macro_geometry else None, defs),
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
    ap.add_argument('--geometry', help="draft's graphics geometry JSON (analysis export geometry)")
    ap.add_argument('--ref', action='append', help="the reference's .ref files with its #DEF macros "
                                                    "(default: REF.ref next to REF.skool)")
    ap.add_argument('--macro-geometry', action='store_true',
                    help="take the reference geometry from the image macros only (ignore --expected graphics)")
    ap.add_argument('--json', metavar='OUT', help='write the report as JSON to OUT (- = stdout)')
    ap.add_argument('--split-vars', action='store_true', help='count g entries as their own category')
    ap.add_argument('--isub', action='store_true', help='read skool files in @isub mode')
    ap.add_argument('-v', '--verbose', action='store_true', help='list every graphics region')
    a = ap.parse_args(argv)
    rep = score(a.draft, a.reference, a.start, a.end, a.expected, a.fields, a.geometry, a.split_vars, a.isub, a.ref,
                macro_geometry=a.macro_geometry)
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
