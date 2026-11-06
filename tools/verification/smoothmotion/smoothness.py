#!/usr/bin/env python3
"""Smoothness of a frame rate conversion, measured on its pictures.

Input: the source frames (50 Hz, raw RGBA, as tools/poc/047-smoothing-filters/capture.py writes them) and the pictures
a filter put on the display (60 Hz refreshes, raw RGBA, `FILTERS_RAW=1` of the POC 047 filters tool). Output: per
filter and per region the numbers below, a table and JSON.

Regions are rectangles that move rigidly in the source: from a ground-truth file of the synthetic tests
(testdata/video/smoothmotion/truth/*.json) or, for any capture, 16-line strips of the paper (`--strips`), each over
its active pixels only (changed in at least 10 % of the source pairs: no static panels).

1. Source track. Per region, the displacement from frame k to k+1 is the shift that matches the most pixels (search
   along both axes, then around the best). Its sum is the content's position p[k]; a pair no shift explains (a wrap, a
   cut, too much non-rigid change) splits the track, refreshes near it are not scored.
2. Ideal trajectory. The position a perfect display shows at refresh time t (in source frames): a straight line fitted
   to p over a window around t whose length is a multiple of the region's cadence (the gap between its moving frames),
   so a game drawing every 3rd frame is ideally a steady 1/3 of its step per frame.
3. Oracle A, the mixture. The refresh, over the region, is decomposed (non-negative least squares) into copies of the
   source frames due around t, shifted to positions near the ideal one. The weights give:
     shown position  the weighted mean of the copies' positions; error e = shown - ideal
     ghost           the weighted spread of the positions (a 50/50 blend of a 4 px step: 2 px; a repeat: 0)
     tear            what no copy mix explains, as a share of the source content's variance (broken motion
                     compensation), at most 1
4. Oracle B, the ideal picture. The refresh against the source picture placed at the ideal position rounded to whole
   pixels (the best picture without blending): a normalised squared error. No model of what a filter does.
   Oracle C, synthetic scenes only: every unbroken stretch of the source track must be the program's own trajectory.
5. Static check: pixels outside the regions that do not change in the source around t must be the source's.

Per region, over the scored refreshes:
  jstep   RMS of the change of e from refresh to refresh (px): the jerk the eye sees. A repeat of an 8 px step at 60 Hz
          jumps e by 8 px every 6th refresh. Judged against `jfloor`, the same for the ideal position rounded to whole
          pixels (what any picture without sub-pixel blending can do at best).
  judder  RMS of e about its mean (px); `lag` is the mean, in ms (positive: the picture is behind).
  ghost   mean spread (px); ghost% the share of refreshes with a spread over 0.75 px.
  tear    mean unexplained share (%), and its 95th percentile.
  pic     oracle B, mean (%).

    python3 -I smoothness.py --source scratch/smoothcap/smooth-speeds.rgba --truth testdata/video/smoothmotion/truth/speeds.json \
        --out-dir scratch/smoothcap/speeds-filters --filters off,blend,interpolate --json result.json
"""
import argparse
import json
import math
import os
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from scipy.optimize import nnls

LUMA = np.array([77, 150, 29], dtype=np.float32) / 256.0


PAD = 64  # luma frames carry this much edge padding: a crop moved up to PAD px is a slice


class Frames:
    """A raw RGBA sequence, luma on demand (edge-padded by PAD)."""

    def __init__(self, path, width, height):
        size = os.path.getsize(path)
        self.count = size // (width * height * 4)
        self.data = np.memmap(path, dtype=np.uint8, mode="r", shape=(self.count, height, width, 4))
        self.width, self.height = width, height
        self.cache = {}

    def luma(self, i):
        i = min(max(i, 0), self.count - 1)
        got = self.cache.get(i)
        if got is None:
            if len(self.cache) > 64:
                self.cache.pop(next(iter(self.cache)))
            got = np.pad((self.data[i, :, :, :3].astype(np.float32) @ LUMA).astype(np.float32), PAD, mode="edge")
            self.cache[i] = got
        return got


def crop(img, rect, dx=0, dy=0):
    """A padded luma frame over rect, content moved by (dx, dy): pixel (x, y) shows frame[y - dy, x - dx]."""
    x0, y0, x1, y1 = rect
    return img[y0 + PAD - dy:y1 + PAD - dy, x0 + PAD - dx:x1 + PAD - dx]


# ---------------------------------------------------------------------------------------------------------------------
# Source track

def mismatches(a, b, mask):
    return int(np.count_nonzero((np.abs(a - b) > 8) & mask))


def activity_mask(src, rect, share=0.1):
    """The pixels of rect that change in at least `share` of the source pairs: the moving content of a strip without
    its static panels and rarely updated digits (all of rect when nothing qualifies)."""
    changed = np.zeros((rect[3] - rect[1], rect[2] - rect[0]), dtype=np.int32)
    for k in range(src.count - 1):
        changed += np.abs(crop(src.luma(k), rect) - crop(src.luma(k + 1), rect)) > 8
    mask = changed >= share * (src.count - 1)
    return mask if mask.any() else np.ones_like(mask)


def track(src, rect, search, mask):
    """Per pair k -> k+1 over mask: (dx, dy, ok, rigid). ok: the shift explains the change; rigid: it explains every
    pixel."""
    out = []
    for k in range(src.count - 1):
        a, b = src.luma(k), src.luma(k + 1)
        still = mismatches(crop(a, rect), crop(b, rect), mask)
        if still == 0:
            out.append((0, 0, True, True))
            continue
        best = (still, 0, 0)
        for d in range(-search, search + 1):
            if d:
                for dx, dy in ((d, 0), (0, d)):
                    m = mismatches(crop(a, rect, dx, dy), crop(b, rect), mask)
                    if m < best[0] or (m == best[0] and abs(dx) + abs(dy) < abs(best[1]) + abs(best[2])):
                        best = (m, dx, dy)
        m0, bx, by = best
        for dy in range(by - 2, by + 3):
            for dx in range(bx - 2, bx + 3):
                if (dx or dy) and (dx, dy) != (bx, by) and max(abs(dx), abs(dy)) <= search:
                    m = mismatches(crop(a, rect, dx, dy), crop(b, rect), mask)
                    if m < best[0]:
                        best = (m, dx, dy)
        m, bx, by = best
        ok = m <= 0.25 * still
        out.append((bx, by, ok, m == 0) if ok else (0, 0, False, False))
    return out


class Trajectory:
    def __init__(self, steps, mask, pairwise=False):
        self.mask = mask
        self.pairwise = pairwise
        n = len(steps) + 1
        self.p = np.zeros((n, 2))
        self.ok = np.ones(n - 1, dtype=bool)
        self.rigid = np.ones(n - 1, dtype=bool)
        for k, (dx, dy, ok, rigid) in enumerate(steps):
            self.p[k + 1] = self.p[k] + (dx, dy)
            self.ok[k] = ok
            self.rigid[k] = rigid
        moving = [k for k, s in enumerate(steps) if s[2] and (s[0] or s[1])]
        gaps = np.diff(moving)
        self.cadence = int(np.bincount(gaps).argmax()) if len(gaps) else 1
        self.cadence = max(1, min(self.cadence, 8))

    def window(self, tau):
        """The frames of the fit around tau, or None when the track breaks inside."""
        length = self.cadence * max(2, math.ceil(10 / self.cadence))
        first = int(round(tau - length / 2))
        last = first + length
        if first < 0 or last + 2 >= len(self.p):
            return None
        if not self.ok[first:last + 2].all():
            return None
        return first, last

    def ideal(self, tau):
        if self.pairwise:
            # motion that changes speed (a bounce): the straight line from frame k to k + 1
            k = int(math.floor(tau))
            if k < 1 or k + 2 >= len(self.p) or not self.ok[k - 1:k + 2].all():
                return None
            v = self.p[k + 1] - self.p[k]
            return self.p[k] + (tau - k) * v, v
        w = self.window(tau)
        if w is None:
            return None
        ks = np.arange(w[0], w[1] + 1)
        px = np.polyfit(ks, self.p[ks, 0], 1)
        py = np.polyfit(ks, self.p[ks, 1], 1)
        return np.array([np.polyval(px, tau), np.polyval(py, tau)]), np.array([px[0], py[0]])


# ---------------------------------------------------------------------------------------------------------------------
# Oracles

def mixture(out, src, traj, rect, k, ideal, velocity, axis=None):
    """Oracle A: (shown position along the axis, ideal along it, spread, unexplained share) or None."""
    if axis is None:
        axis = 0 if abs(velocity[0]) >= abs(velocity[1]) else 1
    reach = int(math.ceil(2 * abs(velocity[axis]))) + 3
    mask = traj.mask
    var = content_energy(src, traj, rect, k)
    if var is None:
        return None
    y = crop(out, rect)[mask]
    refs = [k] if traj.rigid[k] else [k, k + 1]
    cols, pos = [], []
    for j in refs:
        rel = ideal - traj.p[j]
        centre = int(round(rel[axis]))
        minor = int(round(rel[1 - axis]))
        for s in range(centre - reach, centre + reach + 1):
            d = (s, minor) if axis == 0 else (minor, s)
            cols.append(crop(src.luma(j), rect, d[0], d[1])[mask])
            pos.append(traj.p[j][axis] + s)
    a = np.stack(cols, axis=1)
    try:
        w, residual = nnls(a, y, maxiter=50 * a.shape[1])
    except RuntimeError:  # no convergence: the refresh is not scored
        return None
    total = w.sum()
    if total < 1e-6:
        return None
    pos = np.array(pos)
    mean = float((w * pos).sum() / total)
    spread = float(math.sqrt(max(0.0, (w * (pos - mean) ** 2).sum() / total)))
    return mean, float(ideal[axis]), spread, min(1.0, residual * residual / var)


def content_energy(src, traj, rect, k):
    """The variance (sum) of source frame k over the region: the scale of the shares, the same for every filter.
    None for a nearly flat region (less than 4 luma levels RMS), which is not scored."""
    content = crop(src.luma(k), rect)[traj.mask]
    var = float(((content - content.mean()) ** 2).sum())
    return var if var >= 16.0 * content.size else None


def picture_error(out, src, traj, rect, k, ideal):
    """Oracle B: the refresh against the source frame k placed at the ideal position rounded to whole pixels."""
    rel = np.round(ideal - traj.p[k]).astype(int)
    want = crop(src.luma(k), rect, rel[0], rel[1])[traj.mask]
    var = content_energy(src, traj, rect, k)
    if var is None:
        return None
    return float(((crop(out, rect)[traj.mask] - want) ** 2).sum()) / var


# ---------------------------------------------------------------------------------------------------------------------

def refresh_times(count, src_hz, disp_hz, phase_ms):
    """Refresh r at t (in source frames), r = 0.. until the filters tool stops (count - 3 frames)."""
    t, step = phase_ms * src_hz / 1000.0, src_hz / disp_hz
    times = []
    while t < count - 3:
        times.append(t)
        t += step
    return times


def score_region(name, rect, src, traj, outs, times, truth_v, frame_ms, axis=None):
    """Every filter on one region: per filter a dict of the numbers. `axis` (0 x, 1 y): the region's axis, given for
    a pairwise trajectory, whose refreshes are scored while it holds still too."""
    result = {}
    for fname, out in outs.items():
        e, ghost, tear, pic, speeds, lags, trace = [], [], [], [], [], [], []
        last = None
        for r, tau in enumerate(times):
            if r >= out.count:
                break
            got = traj.ideal(tau)
            if got is None:
                last = None
                continue
            ideal, velocity = got
            if truth_v is not None:
                velocity = truth_v
            if abs(velocity).max() < 0.05 and not traj.pairwise:
                last = None
                continue
            k = int(math.floor(tau))
            frame = out.luma(r)
            mix = mixture(frame, src, traj, rect, k, ideal, velocity, axis)
            if mix is None:
                last = None
                continue
            shown, want, spread, unexplained = mix
            err = shown - want
            v_axis = velocity[0] if abs(velocity[0]) >= abs(velocity[1]) else velocity[1]
            speeds.append(abs(v_axis))
            if abs(v_axis) >= 0.05:
                lags.append(-err / v_axis * frame_ms)
            if last is not None and last[0] == r - 1:
                e.append((err, err - last[1], round(want) - want - last[2]))
            last = (r, err, round(want) - want)
            ghost.append(spread)
            tear.append(unexplained)
            b = picture_error(frame, src, traj, rect, k, ideal)
            if b is not None:
                pic.append(b)
            trace.append([r, round(err, 3), round(spread, 3), round(unexplained, 4), None if b is None else round(b, 4)])
        if not e:
            continue
        errs = np.array([x[0] for x in e])
        steps = np.array([x[1] for x in e])
        floors = np.array([x[2] for x in e])
        result[fname] = {
            "refreshes": len(e),
            "jstep": float(np.sqrt((steps ** 2).mean())),
            "jfloor": float(np.sqrt((floors ** 2).mean())),
            "judder": float(errs.std()),
            "speed": float(np.mean(speeds)),
            "lag_ms": float(np.mean(lags)) if lags else 0.0,
            "ghost": float(np.mean(ghost)),
            "ghost_share": float(np.mean(np.array(ghost) > 0.75)),
            "tear": float(np.mean(tear)),
            "tear_p95": float(np.percentile(tear, 95)),
            "pic": float(np.mean(pic)) if pic else 0.0,
            "trace": trace,
        }
    return result


def static_job(job, margin=2):
    """One filter (a worker process): mean and max count of static pixels (outside the regions, unchanged in the
    source around the refresh) that differ from the source."""
    w, h = job["size"]
    src = Frames(job["source"], w, h)
    out = Frames(job["out"], w, h)
    times = job["times"]
    mask = np.ones((h, w), dtype=bool)
    for x0, y0, x1, y1 in job["rects"]:
        mask[max(0, y0 - margin):y1 + margin, max(0, x0 - margin):x1 + margin] = False
    bad = []
    for r, tau in enumerate(times):
        if r >= out.count or r % 3:
            continue
        k = int(math.floor(tau))
        if k + 2 >= src.count:
            break
        core = (slice(PAD, PAD + h), slice(PAD, PAD + w))
        a, b, c = src.luma(k)[core], src.luma(k + 1)[core], src.luma(max(k - 1, 0))[core]
        steady = mask & (np.abs(a - b) <= 8) & (np.abs(a - c) <= 8) & (np.abs(a - src.luma(k + 2)[core]) <= 8)
        bad.append(int(np.count_nonzero(steady & (np.abs(out.luma(r)[core] - a) > 8))))
    return {"mean": float(np.mean(bad)) if bad else 0.0, "max": int(max(bad)) if bad else 0}


def truth_segments(traj, axis, reg):
    """Oracle C: every unbroken stretch of the track (at least 2 periods long) must be the program's own trajectory,
    dir * floor(floor(n / period) * step16 / 16) px, for some frame phase n0. Returns (stretches, matched)."""
    sign = reg["dir"]
    if "offsets" in reg:
        offsets = reg["offsets"]
        period = len(offsets)

        def f(n):
            return sign * offsets[n % period]
    else:
        period, step16 = reg["period"], reg["step16"]

        def f(n):
            return sign * ((n // period) * step16 // 16)

    stretches = matched = 0
    k = 0
    while k < len(traj.ok):
        if not traj.ok[k]:
            k += 1
            continue
        end = k
        while end < len(traj.ok) and traj.ok[end]:
            end += 1
        if end - k >= 2 * period:
            q = traj.p[k:end + 1, axis] - traj.p[k, axis]
            stretches += 1
            for n0 in range(period * (1 if "offsets" in reg else 16)):
                if all(f(n0 + i) - f(n0) == q[i] for i in range(len(q))):
                    matched += 1
                    break
        k = end
    return stretches, matched


def region_job(job):
    """One region, every filter (a worker process): (name, truth check or None, scores, cadence, rect)."""
    width, height = job["size"]
    src = Frames(job["source"], width, height)
    outs = {n: Frames(path, width, height) for n, path in job["outs"].items()}
    name, rect, reg = job["name"], job["rect"], job["reg"]
    tv = np.array(job["tv"]) if job["tv"] is not None else None
    mask = activity_mask(src, rect) if job["strip"] else np.ones((rect[3] - rect[1], rect[2] - rect[0]), dtype=bool)
    pairwise = reg is not None and "offsets" in reg
    traj = Trajectory(track(src, rect, job["search"], mask), mask, pairwise)
    check = None
    if reg is not None:
        # Oracle C: the source track against the program's own motion
        ax = 0 if reg["axis"] == "x" else 1
        steps = [traj.p[k + 1][ax] - traj.p[k][ax] for k in range(len(traj.ok)) if traj.ok[k]]
        measured = float(np.mean(steps)) if steps else 0.0
        segments = truth_segments(traj, ax, reg)
        check = {"expected": float(tv[ax]) if tv is not None else 0.0, "measured": measured, "breaks": int((~traj.ok).sum()),
                 "segments": segments[0], "matched": segments[1], "ok": segments[0] > 0 and segments[0] == segments[1]}
    axis = None if reg is None else (0 if reg["axis"] == "x" else 1)
    scored = score_region(name, rect, src, traj, outs, job["times"], None if pairwise else tv, job["frame_ms"],
                          axis if pairwise else None)
    return name, check, scored, traj.cadence, rect


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", required=True, help="source frames, raw RGBA (a .json next to it gives the size)")
    ap.add_argument("--out-dir", required=True, help="the filters' pictures, <filter>.rgba")
    ap.add_argument("--filters", help="comma list (default: every .rgba in --out-dir)")
    ap.add_argument("--truth", help="ground truth of a synthetic scene")
    ap.add_argument("--strips", type=int, default=16, help="without --truth: strip height over the paper")
    ap.add_argument("--src-hz", type=float, default=50.0)
    ap.add_argument("--disp-hz", type=float, default=60.0)
    ap.add_argument("--phase-ms", type=float, default=3.0)
    ap.add_argument("--times", help="each refresh's time in source frames, one a line (a live trace, live/replay): "
                                    "instead of --disp-hz and --phase-ms")
    ap.add_argument("--search", type=int, default=12, help="source track search, px")
    ap.add_argument("--json", help="write the result here")
    ap.add_argument("--trace", action="store_true", help="keep [refresh, error, ghost, tear, pic] of every refresh")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2), help="worker processes")
    args = ap.parse_args()

    meta = json.load(open(os.path.splitext(args.source)[0] + ".json"))
    width, height = meta["width"], meta["height"]
    src = Frames(args.source, width, height)
    names = args.filters.split(",") if args.filters else sorted(
        f[:-5] for f in os.listdir(args.out_dir)
        if f.endswith(".rgba") and os.path.join(args.out_dir, f) != os.path.normpath(args.source))
    outs = {n: Frames(os.path.join(args.out_dir, n + ".rgba"), width, height) for n in names}
    if args.times:
        times = [float(line) for line in open(args.times) if line.strip()]
    else:
        times = refresh_times(src.count, args.src_hz, args.disp_hz, args.phase_ms)

    regions = []
    if args.truth:
        truth = json.load(open(args.truth))
        ox, oy = truth.get("paper", [48, 48])
        for reg in truth["regions"]:
            x0, y0, x1, y1 = reg["rect"]
            if "offsets" in reg:
                tv = None
            else:
                v = reg["dir"] * reg["step16"] / 16.0 / reg["period"]
                tv = np.array([v, 0.0]) if reg["axis"] == "x" else np.array([0.0, v])
            regions.append((reg["name"], (x0 + ox, y0 + oy, x1 + ox, y1 + oy), tv, reg))
    else:
        for y in range(48, 48 + 192, args.strips):
            regions.append((f"y{y - 48}", (48, y, 48 + 256, y + args.strips), None, None))

    report = {"source": args.source, "filters": names, "regions": {}, "truth_check": {}}
    jobs = [{"size": (width, height), "source": args.source, "name": name, "rect": rect, "reg": reg,
             "outs": {n: os.path.join(args.out_dir, n + ".rgba") for n in names},
             "tv": None if tv is None else tv.tolist(), "search": args.search, "times": times,
             "frame_ms": 1000.0 / args.src_hz, "strip": reg is None} for name, rect, tv, reg in regions]
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for name, check, scored, cadence, rect in pool.map(region_job, jobs):
            if check is not None:
                report["truth_check"][name] = check
                if not check["ok"]:
                    print(f"TRUTH MISMATCH {name}: {check['matched']} of {check['segments']} stretches follow the "
                          f"program's motion ({check['expected']:.3f} px/frame, tracked {check['measured']:.3f})",
                          file=sys.stderr)
            if scored:
                report["regions"][name] = {"rect": rect, "cadence": cadence, "filters": scored}
            print(f"  region {name}: cadence {cadence}, {len(scored)} filters scored", file=sys.stderr, flush=True)
        statics = [{"size": (width, height), "source": args.source, "out": os.path.join(args.out_dir, n + ".rgba"),
                    "times": times, "rects": [r[1] for r in regions]} for n in names]
        report["static"] = dict(zip(names, pool.map(static_job, statics)))

    # Summary per filter: the mean over the scored regions
    keys = ["jstep", "jfloor", "judder", "lag_ms", "ghost", "ghost_share", "tear", "tear_p95", "pic"]
    summary = {}
    for n in names:
        rows = [reg["filters"][n] for reg in report["regions"].values() if n in reg["filters"]]
        if rows:
            summary[n] = {key: float(np.mean([row[key] for row in rows])) for key in keys}
            summary[n]["regions"] = len(rows)
            summary[n]["static_px"] = report["static"][n]["mean"]
    report["summary"] = summary
    if not args.trace:
        for region in report["regions"].values():
            for scores in region["filters"].values():
                scores.pop("trace", None)

    print(f"{'filter':14} {'jstep':>6} {'floor':>6} {'judder':>6} {'lag ms':>7} {'ghost':>6} {'ghost%':>6} "
          f"{'tear%':>6} {'tear95':>6} {'pic%':>6} {'static':>7}")
    for n, s in summary.items():
        print(f"{n:14} {s['jstep']:6.2f} {s['jfloor']:6.2f} {s['judder']:6.2f} {s['lag_ms']:7.1f} {s['ghost']:6.2f} "
              f"{100 * s['ghost_share']:6.1f} {100 * s['tear']:6.2f} {100 * s['tear_p95']:6.2f} {100 * s['pic']:6.1f} "
              f"{s['static_px']:7.1f}")
    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=1)


if __name__ == "__main__":
    main()
