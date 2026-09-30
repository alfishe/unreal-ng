#!/usr/bin/env python3
"""The contention probe's compatibility matrix: every emulator x machine x check, from the harness's dumps.

    matrix.py [--out-dir OUT] [--html FILE] [--md FILE] [--probe DIR]

Reads OUT/<emulator>/<machine>.result and .bin (default: out/ next to this script, what run-all.sh writes),
compares every check of every dump with the probe's expected table (as ctprobe-compare.py does) and writes
  - an HTML page (default OUT/matrix.html): a summary grid and, per machine, every check by emulator, with the
    measured and expected rows on hover;
  - optionally a Markdown file with the summary grid.
The probe's source (ctprobe.asm) gives each check its description. Only the contention probe is supported.
"""
import argparse, datetime, html, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

RECORD = 19
MACHINES = ['48k', '128k', 'plus2', 'plus2a', 'plus3', 'pentagon', 'scorpion', 'profscorp', 'atm710', 'atm3', 'profi']
MNAME = {'48k': '48K', '128k': '128K', 'plus2': '+2', 'plus2a': '+2A', 'plus3': '+3', 'pentagon': 'Pentagon',
         'scorpion': 'Scorpion', 'profscorp': 'Scorpion ProfROM', 'atm710': 'ATM Turbo 2+', 'atm3': 'ZX-Evo',
         'profi': 'Profi'}
EMUS = ['unreal-ng', 'fuse', 'skoolkit', 'xpeccy-plus', 'xpeccy', 'mame', 'zesarux', 'zxmak2', 'kozynax',
        'zx-m8xxx', 'spec-chum']
ENAME = {'unreal-ng': 'unreal-ng', 'fuse': 'FUSE', 'skoolkit': 'SkoolKit', 'xpeccy-plus': 'xpeccy-plus',
         'xpeccy': 'Xpeccy', 'mame': 'MAME', 'zesarux': 'ZEsarUX', 'zxmak2': 'ZXMAK2', 'kozynax': 'Kozynax',
         'zx-m8xxx': 'ZX-M8XXX', 'spec-chum': 'spec_chum'}
CLASSES = ['ULA 48K', 'ULA 128K', 'gate array', 'no contention', 'no contention, attr bus',
           'no contention, attr bus, Even M1', 'no contention, Even M1']
GROUPS = [('M1', 'Opcode fetches (M1)'), ('D', 'Data reads and writes'), ('N', 'Internal ticks (no memory access)'),
          ('P', 'Ports and the floating bus'), ('X', 'Negative checks')]


def case_notes(asm):
    """name -> the comment of its record in the CASES table"""
    notes, cur = {}, []
    inside = False
    for line in open(asm, encoding='latin-1'):
        if line.startswith('CASES:'):
            inside = True
            continue
        if not inside:
            continue
        if re.match(r'^\w', line) and not line.startswith('CASES'):
            break
        code, _, comment = line.partition(';')
        if re.match(r'\s+db \d+,', code):
            cur = []
        if comment.strip():
            cur.append(comment.strip())
        m = re.match(r'\s+db "([^"]+)"', code)
        if m:
            notes[m.group(1).strip()] = ' '.join(cur)
    return notes


def analyse(dump, sym):
    base = sym['START']
    peek = lambda a: dump[a - base]
    word = lambda a: peek(a) | peek(a + 1) << 8
    if len(dump) < sym['PROBEEND'] - base or peek(sym['DONE']) != 1:
        return None
    cls, caps = peek(sym['CLASS']), peek(sym['CAPS'])
    size = sym['RESULTSEND'] - sym['RESULTS']
    onset = word(sym['ONSET'])
    rows = {}
    at = sym['CASES']
    while peek(at):
        flags = peek(at + 1)
        offset = word(at + 9)
        offset = offset - 65536 if offset > 32767 else offset
        count, results = peek(at + 11), word(at + 12)
        name = bytes(dump[at + 14 - base:at + 19 - base]).decode().strip()
        at += RECORD
        if flags & 3 & ~caps or (flags & 32 and cls in (3, 6)):
            rows[name] = ('na', None, None, None)
            continue
        got = list(dump[results - base:results - base + count])
        e = sym['EXPECTED'] + cls * size + (results - sym['RESULTS']) - base
        exp = list(dump[e:e + count])
        if got == exp:
            rows[name] = ('ok', got, exp, onset + offset)
            continue
        shift = None
        for k in sorted([k for k in range(-8, 9) if k], key=lambda k: (abs(k), k > 0)):
            pairs = [(got[i], exp[i + k]) for i in range(count) if 0 <= i + k < count]
            if len(pairs) * 2 >= count and all(g == x for g, x in pairs):
                shift = -k
                break
        rows[name] = ('shift' if shift else 'bad', got, exp, onset + offset, shift)
    return {'class': CLASSES[cls] if cls < len(CLASSES) else str(cls), 'onset': onset, 'rows': rows}


def scrub(text):
    """No machine-specific absolute paths in a report: a path under the repository becomes relative to it"""
    root = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
    return re.sub(r'/[^\s()]+', lambda m: os.path.relpath(m.group(0), root) if m.group(0).startswith(root)
                  else os.path.basename(m.group(0)), text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out-dir', default=os.path.join(HERE, 'out'), help='the harness results (default: out/)')
    ap.add_argument('--html', help='the HTML page (default: <out-dir>/matrix.html)')
    ap.add_argument('--md', help='also write the summary grid as Markdown')
    ap.add_argument('--probe', default=os.path.join(HERE, '..', 'contention', 'ctprobe'),
                    help='the probe folder (ctprobe.asm, ctprobe.sym)')
    args = ap.parse_args()
    probe, outdir = args.probe, args.out_dir
    target = args.html or os.path.join(outdir, 'matrix.html')
    sym = {}
    for line in open(os.path.join(probe, 'ctprobe.sym')):
        m = re.match(r'(\w+) equ #([0-9A-F]+)', line)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)
    notes = case_notes(os.path.join(probe, 'ctprobe.asm'))
    names = list(notes)
    data = {}
    for emu in EMUS:
        for m in MACHINES:
            res = os.path.join(outdir, emu, m + '.result')
            status, detail = 'missing', 'not run'
            if os.path.exists(res):
                status, _, detail = open(res).read().strip().partition(' ')
                detail = scrub(detail)
            a = None
            binf = os.path.join(outdir, emu, m + '.bin')
            if status in ('ok', 'wrong') and os.path.exists(binf):
                a = analyse(open(binf, 'rb').read(), sym)
            data[emu, m] = (status, detail, a)
    emus = [e for e in EMUS if any(data[e, m][0] != 'missing' for m in MACHINES)]
    open(target, 'w').write(render(emus, names, notes, data))
    print(f'matrix: {target}')
    if args.md:
        open(args.md, 'w').write(render_md(emus, data))
        print(f'matrix: {args.md}')


def render_md(emus, data):
    """The summary grid as Markdown"""
    lines = ['| Emulator | ' + ' | '.join(MNAME[m] for m in MACHINES) + ' |',
             '|:--|' + ':-:|' * len(MACHINES)]
    for e in emus:
        cells = []
        for m in MACHINES:
            status, _, a = data[e, m]
            if status == 'ok':
                cells.append('✓')
            elif status == 'wrong' and a:
                ran = sum(1 for r in a['rows'].values() if r[0] != 'na')
                bad = sum(1 for r in a['rows'].values() if r[0] in ('bad', 'shift'))
                cells.append(f'{ran - bad}/{ran}')
            elif status in ('skipped', 'missing'):
                cells.append('—')
            else:
                cells.append('error')
        lines.append(f'| {ENAME[e]} | ' + ' | '.join(cells) + ' |')
    return '\n'.join(lines) + '\n'


def esc(s):
    return html.escape(str(s), quote=True)


def cell_summary(status, detail, a):
    if status == 'ok':
        return '<td class="c ok" title="all values as expected">&#10003;</td>'
    if status == 'wrong' and a:
        bad = sum(1 for r in a['rows'].values() if r[0] in ('bad', 'shift'))
        ran = sum(1 for r in a['rows'].values() if r[0] != 'na')
        return f'<td class="c bad" title="{esc(detail)}">{ran - bad}/{ran}</td>'
    if status == 'skipped':
        return f'<td class="c na" title="{esc(detail)}">&mdash;</td>'
    if status == 'missing':
        return '<td class="c na" title="not run">&middot;</td>'
    return f'<td class="c err" title="{esc(detail)}">error</td>'


def cell_case(status, detail, a, name):
    if status == 'skipped' or status == 'missing':
        return f'<td class="c na" title="{esc(detail)}">&mdash;</td>'
    if status not in ('ok', 'wrong') or not a:
        return f'<td class="c err" title="{esc(detail)}">!</td>'
    r = a['rows'].get(name)
    if r is None or r[0] == 'na':
        return '<td class="c na" title="not applicable on this machine">&middot;</td>'
    got, exp, t = r[1], r[2], r[3]
    tip = f'from T{t}&#10;got {" ".join(map(str, got))}&#10;exp {" ".join(map(str, exp))}'
    if r[0] == 'ok':
        return f'<td class="c ok" title="{tip}">&#10003;</td>'
    if r[0] == 'shift':
        k = r[4]
        return f'<td class="c shift" title="{tip}&#10;the expected row {abs(k)} tick(s) {"later" if k > 0 else "earlier"}">{"+" if k > 0 else "&minus;"}{abs(k)}</td>'
    n = sum(g != e for g, e in zip(got, exp))
    return f'<td class="c bad" title="{tip}">&#10007;{n}</td>'


def render(emus, names, notes, data):
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    h = [HEAD.replace('%NOW%', now)]
    # summary
    h.append('<section><h2>Summary</h2><p class="lead">Each cell: the whole probe on one machine. &#10003; every '
             'value as expected; <b>n/m</b> checks right out of those that ran; &mdash; the emulator has no such '
             'machine (hover for the reason).</p><div class="scroll"><table class="grid"><thead><tr><th></th>')
    h += [f'<th>{MNAME[m]}</th>' for m in MACHINES]
    h.append('</tr></thead><tbody>')
    for e in emus:
        h.append(f'<tr><th class="rowh">{ENAME[e]}</th>' + ''.join(cell_summary(*data[e, m]) for m in MACHINES) + '</tr>')
    h.append('</tbody></table></div></section>')
    # per machine
    h.append('<section><h2>Check by check</h2><p class="lead">Rows are the probe\'s checks, columns the emulators. '
             '&#10003; right at every T-state; <b>+n</b> / <b>&minus;n</b> the whole row n ticks later / earlier '
             'than on real hardware; <b>&#10007;n</b> n values wrong in another way; &middot; not applicable on '
             'this machine. Hover a cell for the measured and expected rows.</p><div class="tabs">')
    for i, m in enumerate(MACHINES):
        h.append(f'<button class="tab{" on" if i == 0 else ""}" data-m="{m}">{MNAME[m]}</button>')
    h.append('</div>')
    for i, m in enumerate(MACHINES):
        h.append(f'<div class="pane{" on" if i == 0 else ""}" id="p-{m}"><div class="scroll"><table class="grid cases"><thead><tr><th>Check</th><th class="what">What it times</th>')
        h += [f'<th>{ENAME[e]}</th>' for e in emus]
        h.append('</tr><tr class="cls"><th></th><th class="what">detected as</th>')
        for e in emus:
            a = data[e, m][2]
            h.append(f'<th>{esc(a["class"]) if a else ""}</th>')
        h.append('</tr></thead><tbody>')
        for g, title in GROUPS:
            group = [n for n in names if re.match(g + r'-', n)]
            if not group:
                continue
            h.append(f'<tr class="grp"><td colspan="{2 + len(emus)}">{title}</td></tr>')
            for n in group:
                h.append(f'<tr><th class="rowh">{n}</th><td class="what">{esc(notes[n])}</td>' +
                         ''.join(cell_case(*data[e, m], n) for e in emus) + '</tr>')
        h.append('</tbody></table></div></div>')
    h.append('</section>' + FOOT)
    return '\n'.join(h)


HEAD = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ctprobe Compatibility Matrix</title>
<style>
:root{--bg:#fbfaf7;--fg:#1d1c1a;--muted:#6b675f;--line:#e2ded5;--head:#f1eee7;--ok:#dff1e2;--okf:#1f6b33;--bad:#fbe0dc;--badf:#9b2a1c;--shift:#fdf0cf;--shiftf:#7a5900;--na:transparent;--naf:#b3ada2;--err:#efd9f3;--errf:#6d2a80}
@media (prefers-color-scheme:dark){:root{--bg:#171614;--fg:#ece9e2;--muted:#a09a8f;--line:#34312c;--head:#211f1c;--ok:#1d3a24;--okf:#8fd9a2;--bad:#46201b;--badf:#f3a498;--shift:#3d3215;--shiftf:#f0cf6e;--naf:#5b5750;--err:#3a2242;--errf:#d9a6ea}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 -apple-system,BlinkMacSystemFont,"Inter","Segoe UI",sans-serif}
main{max-width:1500px;margin:0 auto;padding:28px 16px 60px}h1{font-size:24px;margin:0 0 4px}h2{font-size:18px;margin:34px 0 6px}
.sub,.lead{color:var(--muted);margin:0 0 12px;max-width:900px}.scroll{overflow-x:auto;border:1px solid var(--line);border-radius:8px}
table.grid{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}.grid th,.grid td{border-bottom:1px solid var(--line);padding:5px 8px;white-space:nowrap}
.grid thead th{background:var(--head);font-weight:600;font-size:12.5px;position:sticky;top:0}.grid .rowh{text-align:left;font-weight:600;background:var(--head)}
.c{text-align:center;font-weight:600;font-size:13px}.ok{background:var(--ok);color:var(--okf)}.bad{background:var(--bad);color:var(--badf)}.shift{background:var(--shift);color:var(--shiftf)}
.na{color:var(--naf);font-weight:400}.err{background:var(--err);color:var(--errf)}td.what,th.what{white-space:normal;min-width:260px;max-width:420px;color:var(--muted);font-size:12.5px;text-align:left}
tr.cls th{font-weight:400;font-size:11px;color:var(--muted);white-space:normal;max-width:110px;top:31px}tr.grp td{background:var(--bg);font-weight:600;padding-top:14px}
.tabs{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0}.tab{border:1px solid var(--line);background:var(--head);color:var(--fg);border-radius:6px;padding:5px 11px;font:inherit;cursor:pointer}
.tab.on{background:var(--fg);color:var(--bg);border-color:var(--fg)}.pane{display:none}.pane.on{display:block}.foot{color:var(--muted);font-size:12.5px;margin-top:28px}
</style></head><body><main>
<h1>ctprobe compatibility matrix</h1>
<p class="sub">The contention probe (tools/verification/contention/ctprobe) run by the co-emulation harness on every emulator it found, each with its stock settings, %NOW%. Expected values come from the probe's hardware-derived tables for the machine class it detects.</p>'''

FOOT = '''<p class="foot">Generated from the harness dumps (tools/verification/coemu). A check is the timing of one code fragment at consecutive T-states; "M1" rows time opcode fetches, "D" data accesses, "N" internal ticks, "P" ports.</p>
</main><script>
document.querySelectorAll('.tab').forEach(b=>b.addEventListener('click',()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('on',x===b));document.querySelectorAll('.pane').forEach(p=>p.classList.toggle('on',p.id==='p-'+b.dataset.m));}));
</script></body></html>'''

if __name__ == '__main__':
    main()
