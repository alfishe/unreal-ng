#!/usr/bin/env python3
"""POC 037: metrics of present logs (design.md section 3).

    python3 -I analyze.py <run.csv> [...]            one markdown row per run
    python3 -I analyze.py --detail <run.csv>         plus the histograms

A log has one row per present. The screen time of a present is presented_ns (the system's feedback) when the run has
it (Windows: the first kernel vblank after the present, marked vblank), else a proxy: submit_ns (Qt GL: frameSwapped)
or tick_ns (SDL_GPU: acquire return); the table marks proxies.
"""
import csv
import math
import statistics
import sys


def load(path):
    meta, rows = {}, []
    with open(path) as f:
        lines = [l for l in f]
    for l in lines:
        if l.startswith('# '):
            k, _, v = l[2:].strip().partition('=')
            meta[k] = v
    body = [l for l in lines if not l.startswith('#')]
    for r in csv.DictReader(body):
        rows.append({k: int(v) for k, v in r.items()})
    return meta, rows


def pct(xs, p):
    if not xs:
        return float('nan')
    xs = sorted(xs)
    k = (len(xs) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def drc_model(tick_ns, rate, max_trim):
    """The audio ring under display-locked pacing: one machine frame of audio (48000 / rate samples) per tick, the
    device drains 48000 / s; SoundManager's PI controller (target 40 ms, KP 0.08, KI 0.0008, EMA 0.05) trims the
    resampler within +-max_trim. Returns (the worst |occupancy - target| in ms after a 10 s settle, the final trim)."""
    dev = 48000.0
    occ = 40.0  # ms
    filt = occ
    integ = 0.0
    trim = 0.0
    worst = 0.0
    t0 = tick_ns[0]
    settle = min(10e9, (tick_ns[-1] - t0) / 4)
    for i in range(1, len(tick_ns)):
        dt = (tick_ns[i] - tick_ns[i - 1]) / 1e9
        produced_ms = 1000.0 / rate * (1.0 + trim)  # one machine frame, resampled by the trim
        occ += produced_ms - dt * 1000.0
        filt += 0.05 * (occ - filt)
        err = (filt - 40.0) / 40.0
        eff = math.copysign(max(0.0, abs(err) - 0.02), err) if abs(err) < 0.04 else err
        new_integ = integ + eff
        trim_raw = -(0.08 * eff + 0.0008 * new_integ)
        trim = max(-max_trim, min(max_trim, trim_raw))
        if trim == trim_raw:
            integ = new_integ
        if (tick_ns[i] - t0) > settle:
            worst = max(worst, abs(occ - 40.0))
    return worst, trim


def analyze(path, detail=False):
    meta, rows = load(path)
    rate = float(meta.get('rate_hz', '50'))
    period = 1e9 / rate
    variant = meta.get('variant', '?')
    # Skip the first 2 s (window creation, the display link settling)
    if rows:
        t0 = rows[0]['latch_ns']
        rows = [r for r in rows if r['latch_ns'] - t0 > 2e9]
    feedback = any(r['presented_ns'] >= 0 for r in rows)
    if feedback:
        key, kind = 'presented_ns', ('vblank' if 'VerticalBlank' in meta.get('presented_source', '') else 'presented')
    elif any(r['submit_ns'] > 0 for r in rows):
        key, kind = 'submit_ns', 'proxy:swap'
    else:
        key, kind = 'tick_ns', 'proxy:acquire'
    shown = [r for r in rows if r[key] > 0]
    dropped_presents = sum(1 for r in rows if feedback and r['presented_ns'] == 0)
    disp_hz0 = float(meta.get('display_hz', '0') or 0) or 60.0
    late = sum(1 for r in rows if feedback and r['target_ns'] > 0 and r['presented_ns'] > r['target_ns'] + 0.5e9 / disp_hz0)

    # The screen timeline: collapse consecutive presents of the same frame
    runs = []
    for r in shown:
        if runs and runs[-1]['frame'] == r['frame']:
            runs[-1]['refreshes'] += 1
            continue
        runs.append({'frame': r['frame'], 'latch': r['latch_ns'], 'on': r[key], 'refreshes': 1})
    durations = [(runs[i + 1]['on'] - runs[i]['on']) for i in range(len(runs) - 1)]
    latencies = [(x['on'] - x['latch']) for x in runs]
    frames = [x['frame'] for x in runs]
    first, last = (frames[0], frames[-1]) if frames else (0, 0)
    never = (last - first + 1) - len(set(frames)) if frames else 0
    backwards = sum(1 for i in range(1, len(frames)) if frames[i] < frames[i - 1])

    # The refresh: the median interval between distinct screen times
    times = sorted(set(r[key] for r in shown))
    gaps = [times[i + 1] - times[i] for i in range(len(times) - 1)]
    refresh = statistics.median(gaps) if gaps else float('nan')
    disp_hz = float(meta.get('display_hz', '0') or 0)
    vfix = 1e9 / disp_hz if disp_hz > 0 else refresh
    # Refreshes per frame on a fixed display: the beat allows floor / ceil of V-periods per source period
    per = period / vfix
    lo, hi = math.floor(per), math.ceil(per)
    counts = [round(d / vfix) for d in durations]
    irregular = sum(1 for c in counts if c < lo or c > hi or (lo == 0 and c == 0))
    dur_err = [abs(d - period) / 1e6 for d in durations]
    lat_ms = [l / 1e6 for l in latencies]
    span = pct(lat_ms, 99) - pct(lat_ms, 1)
    latches = sorted(set(r['latch_ns'] for r in rows))
    lg = [(latches[i + 1] - latches[i]) / 1e6 for i in range(len(latches) - 1)]
    prod_jitter = float(meta['producer_jitter_p99_ms']) if 'producer_jitter_p99_ms' in meta else pct([abs(x - period / 1e6) for x in lg], 99)
    cpu = [r['cpu_ns'] / 1e6 for r in rows if r['cpu_ns'] > 0]
    seconds = (rows[-1]['latch_ns'] - rows[0]['latch_ns']) / 1e9 if len(rows) > 1 else 1

    out = {
        'run': path.rsplit('/', 1)[-1].replace('.csv', ''),
        'kind': kind,
        'rate': rate,
        'refresh_hz': 1e9 / refresh if refresh == refresh and refresh > 0 else float('nan'),
        'frames': len(runs),
        'never_per_min': never / seconds * 60,
        'irregular_per_min': irregular / seconds * 60,
        'backwards': backwards,
        'dropped_presents': dropped_presents,
        'late': late,
        'dur_err_p50': pct(dur_err, 50),
        'dur_err_p99': pct(dur_err, 99),
        'lat_p50': pct(lat_ms, 50),
        'lat_p99': pct(lat_ms, 99),
        'lat_span': span,
        'v_ms': vfix / 1e6,
        'cpu_p50': pct(cpu, 50),
        'prod_jitter': prod_jitter,
    }
    if variant == 'd':
        ticks = sorted(set(r['tick_ns'] for r in rows if r['tick_ns'] > 0))
        tg = [(ticks[i + 1] - ticks[i]) / 1e6 for i in range(len(ticks) - 1)]
        med = statistics.median(tg)
        out['tick_hz'] = 1000.0 / statistics.mean(tg)
        out['tick_jitter_p99'] = pct([abs(x - med) for x in tg], 99)
        out['ratio_pct'] = (out['tick_hz'] / rate - 1) * 100
        out['drc_05'] = drc_model(ticks, rate, 0.005)
        out['drc_4'] = drc_model(ticks, rate, 0.04)
    if detail:
        hist = {}
        for c in counts:
            hist[c] = hist.get(c, 0) + 1
        out['hist'] = dict(sorted(hist.items()))
        out['meta'] = meta
    return out


def main():
    args = sys.argv[1:]
    detail = '--detail' in args
    paths = [a for a in args if not a.startswith('--')]
    print('| run | time | rate | refresh | frames | never/min | irregular/min | back | dropped / late | |dur-1/R| p50/p99 ms | '
          'latency p50/p99 ms | latency span ms (V) | producer jitter p99 ms | cpu ms |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    extra = []
    for p in paths:
        o = analyze(p, detail)
        print(f"| {o['run']} | {o['kind']} | {o['rate']:.3f} | {o['refresh_hz']:.2f} | {o['frames']} | {o['never_per_min']:.1f} | "
              f"{o['irregular_per_min']:.1f} | {o['backwards']} | {o['dropped_presents']} / {o['late']} | "
              f"{o['dur_err_p50']:.2f} / {o['dur_err_p99']:.2f} | {o['lat_p50']:.1f} / {o['lat_p99']:.1f} | "
              f"{o['lat_span']:.1f} ({o['v_ms']:.1f}) | {o['prod_jitter']:.2f} | {o['cpu_p50']:.2f} |")
        if 'tick_hz' in o or detail:
            extra.append(o)
    for o in extra:
        if 'tick_hz' in o:
            print(f"\n{o['run']}: ticks {o['tick_hz']:.3f} Hz, jitter p99 {o['tick_jitter_p99']:.3f} ms, machine runs "
                  f"{o['ratio_pct']:+.2f} %; audio ring worst |occ-40| ms: DRC +-0.5 % {o['drc_05'][0]:.1f} "
                  f"(trim {o['drc_05'][1] * 100:+.2f} %), DRC +-4 % {o['drc_4'][0]:.1f} (trim {o['drc_4'][1] * 100:+.2f} %)")
        if 'hist' in o:
            print(f"\n{o['run']}: refreshes per frame {o['hist']}; meta {o['meta']}")


if __name__ == '__main__':
    main()
