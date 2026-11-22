#!/usr/bin/env python3
"""Compare two regress.py runs title by title.

    compare.py <A dir> <B dir> [--out DIR] [--nondet <file or run dir> ...]

Per job: same (every shot's raw-frame SHA-1 equal), changed (a shot differs), newly-failing (A started, B did not:
picture -> stuck-os / flat / no-load / crash ...), newly-starting (the reverse), verdict-changed (other verdict moves).
Jobs listed as nondeterministic (--nondet: a nondet.json written by `compare.py <run1> <run2> --baseline`, or a run dir
holding one) are reported apart: their pictures differ between two runs of the same binary.
--baseline: A and B are two runs of the SAME binary; writes <out>/nondet.json (the jobs whose pictures differ).
Writes <out>/report.md and, for every changed job, <out>/sheets/<job>.png (A row over B row, the 4 shots).
The realboard programs are compared by verdict and by picture hash.
"""
import argparse, json, os, sys

STARTED = ('picture',)


def load(run):
    with open(os.path.join(run, 'results.json')) as f:
        return json.load(f)


def shots_of(res):
    return {s['label']: s for s in res.get('shots', [])}


def sheet(a_dir, b_dir, a, b, path, title):
    from PIL import Image, ImageDraw
    labels = [l for l in ('cursor', 'f250', 'f750', 'f1500', 'keys', 'f50', 'f150', 'f250', 'f350', 'welcome', 'menu', 'browser',
                          'dir-nextzxos', 'dir-up') if l in shots_of(a) or l in shots_of(b)]
    labels = list(dict.fromkeys(labels))
    w, h = 240, 192
    im = Image.new('RGB', (len(labels) * (w + 4) + 40, 2 * (h + 18) + 20), (30, 30, 30))
    d = ImageDraw.Draw(im)
    d.text((4, 2), title[:120], fill=(255, 255, 0))
    for row, (run, res, tag) in enumerate(((a_dir, a, 'A'), (b_dir, b, 'B'))):
        y = 18 + row * (h + 18)
        d.text((4, y + h // 2), tag, fill=(255, 255, 255))
        s = shots_of(res)
        for j, label in enumerate(labels):
            x = 40 + j * (w + 4)
            if label in s:
                im.paste(Image.open(os.path.join(run, 'shots', s[label]['png'])).convert('RGB').resize((w, h)), (x, y + 12))
                other = shots_of(b if row == 0 else a).get(label)
                mark = '' if other and other['sha1'] == s[label]['sha1'] else ' *'
                d.text((x, y), '%s %s%s' % (label, res['verdict'] if label == 'f1500' else '', mark), fill=(200, 200, 200))
    im.save(path)


def classify(a, b):
    if a is None:
        return 'only-in-B'
    if b is None:
        return 'only-in-A'
    va, vb = a['verdict'], b['verdict']
    if va in STARTED and vb not in STARTED:
        return 'newly-failing'
    if va not in STARTED and vb in STARTED:
        return 'newly-starting'
    if va != vb:
        return 'verdict-changed'
    sa, sb = shots_of(a), shots_of(b)
    if set(sa) != set(sb) or any(sa[k]['sha1'] != sb[k]['sha1'] for k in sa):
        return 'changed'
    return 'same'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--out')
    ap.add_argument('--nondet', nargs='*', default=[])
    ap.add_argument('--baseline', action='store_true', help='A and B are two runs of the same binary: write nondet.json')
    args = ap.parse_args()
    out = args.out or os.path.join(args.b, 'compare-vs-' + os.path.basename(os.path.normpath(args.a)))
    os.makedirs(os.path.join(out, 'sheets'), exist_ok=True)
    A, B = load(args.a), load(args.b)
    nondet = {}
    for n in args.nondet:
        p = os.path.join(n, 'nondet.json') if os.path.isdir(n) else n
        if os.path.exists(p):
            nondet.update(json.load(open(p)))
    ra, rb = A['results'], B['results']
    rows = {}
    for job in sorted(set(ra) | set(rb)):
        a, b = ra.get(job), rb.get(job)
        kind = classify(a, b)
        res = b or a
        diff = []
        if a and b:
            sa, sb = shots_of(a), shots_of(b)
            diff = [k for k in sa if k in sb and sa[k]['sha1'] != sb[k]['sha1']]
        rows[job] = {'kind': kind, 'title': res.get('title', job), 'a': (a or {}).get('verdict'), 'b': (b or {}).get('verdict'),
                     'diff': diff, 'nondet': job in nondet}
        if kind != 'same' and a and b:
            sheet(args.a, args.b, a, b, os.path.join(out, 'sheets', job + '.png'), '%s %s: %s' % (job, res.get('title', ''), kind))
    # realboard
    rb_rows = []
    pa = {p['program']: p for p in ((A.get('realboard') or {}).get('programs') or [])}
    pb = {p['program']: p for p in ((B.get('realboard') or {}).get('programs') or [])}
    for prog in sorted(set(pa) | set(pb)):
        x, y = pa.get(prog, {}), pb.get(prog, {})
        if x.get('verdict') != y.get('verdict') or x.get('sha1') != y.get('sha1'):
            rb_rows.append((prog, x.get('verdict'), y.get('verdict'), 'picture' if x.get('sha1') != y.get('sha1') else '',
                            x.get('detail', ''), y.get('detail', '')))
    if args.baseline:
        nd = {j: {'title': r['title'], 'shots': r['diff'], 'a': r['a'], 'b': r['b']} for j, r in rows.items() if r['kind'] != 'same'}
        with open(os.path.join(out, 'nondet.json'), 'w') as f:
            json.dump(nd, f, indent=1, sort_keys=True)
    counts = {}
    for r in rows.values():
        key = r['kind'] + (' (nondeterministic)' if r['nondet'] and r['kind'] != 'same' else '')
        counts[key] = counts.get(key, 0) + 1
    lines = ['# Next regression compare', '', 'A: `%s` (%s)  ' % (args.a, A['meta']['binary']), 'B: `%s` (%s)' % (args.b, B['meta']['binary']), '',
             '| result | jobs |', '|:--|--:|'] + ['| %s | %d |' % kv for kv in sorted(counts.items())] + ['']
    order = ['newly-failing', 'newly-starting', 'verdict-changed', 'changed', 'only-in-A', 'only-in-B']
    for kind in order:
        for nd in (False, True):
            sel = [(j, r) for j, r in rows.items() if r['kind'] == kind and r['nondet'] == nd]
            if not sel:
                continue
            lines += ['## %s%s (%d)' % (kind, ' - known nondeterministic' if nd else '', len(sel)), '',
                      '| job | title | A | B | shots that differ | sheet |', '|:--|:--|:--|:--|:--|:--|']
            for j, r in sel:
                lines.append('| %s | %s | %s | %s | %s | [png](sheets/%s.png) |' % (j, r['title'], r['a'], r['b'], ' '.join(r['diff']), j))
            lines.append('')
    if rb_rows:
        lines += ['## realboard differences', '', '| program | A | B | | A detail | B detail |', '|:--|:--|:--|:--|:--|:--|']
        lines += ['| %s | %s | %s | %s | %s | %s |' % r for r in rb_rows]
    else:
        lines += ['## realboard: no differences']
    with open(os.path.join(out, 'report.md'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines[:12 + len(counts)]))
    print('report: ' + os.path.join(out, 'report.md'))


if __name__ == '__main__':
    main()
