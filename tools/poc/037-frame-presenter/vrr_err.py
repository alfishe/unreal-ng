"""POC 037: per run, the error of the present intervals and of the on-screen intervals against the source period."""
import csv, sys
for p in sys.argv[1:]:
    meta = {l[2:].split('=')[0]: l.strip().split('=', 1)[1] for l in open(p) if l.startswith('# ')}
    rows = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(l for l in open(p) if not l.startswith('#'))]
    t0 = rows[0]['latch_ns']
    rows = [r for r in rows if r['latch_ns'] - t0 > 2e9]
    per = 1e9 / float(meta['rate_hz'])
    sub = [r['submit_ns'] for r in rows]
    se = [abs((sub[i + 1] - sub[i]) - per) / 1e6 for i in range(len(sub) - 1)]
    on = [r['presented_ns'] for r in rows if r['presented_ns'] > 0]
    de = [abs((on[i + 1] - on[i]) - per) / 1e6 for i in range(len(on) - 1)]
    q = lambda x, f: sorted(x)[int(f * (len(x) - 1))]
    print(f"| {p.split('/')[-1][:-4]} | {meta.get('vblank_interval_ms_p1_p50_p99', '').split(' ')[1]} | "
          f"{q(se, .5):.2f} / {q(se, .99):.2f} / {max(se):.2f} | {q(de, .5):.2f} / {q(de, .99):.2f} / {max(de):.2f} | "
          f"{sum(1 for x in de if x > 1.5)} of {len(de)} | {meta.get('target', 'latch')} |")
