#!/usr/bin/env python3
"""Multi-reference oracle for scenes the per-pixel oracle cannot judge (two-page
textures that move: the irregular spiral, tunnel, floor).

References, built offline from raw frames t-1, t, t+1 (and the strict-period
mix of quality.Oracle where it is defined):
  avg3     1/4 t-1 + 1/2 t + 1/4 t+1          simple averaging over the period
  twopage  1/2 t + 1/2 other page at t         motion compensated (twopage.py)
  strict   the per-pixel oracle's period mix   only where it calls flicker
Consensus reference per pixel: the component-wise median in OKLab of the
references present there; spread = the largest distance of a reference from
it (low spread = the references agree = a confident pixel).

Dimensions (per frame, averaged over the clip):
  color_ok       share of confident changed pixels whose output is within
                 dE 0.02 (OKLab, ~2 %) of the consensus reference (higher is better)
  color_ok_any   same, but within 0.02 of ANY reference
  de_p50, de_p95 dE output vs consensus on those pixels
  edge_p95       95th percentile distance (px) from each output edge pixel to
                 the nearest consensus-reference edge, and back (max of both)
  shimmer        mean dE between consecutive outputs where the consensus
                 reference itself does not change (flicker left over)
Targets for the spiral: edge_p95 <= 3 px, color_ok close to 1 with dE p95 <= 0.02.

  python3 python/oracle2.py --clip data/clip_v2 --alg mod --from 12100 --to 12300 --name ate-irregular --out out/oracle2
run.py --oracle2 computes the same dimensions in its own pass.
"""
import argparse
import json
import os
import sys

import numpy as np
from scipy import ndimage

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import ClipV2  # noqa: E402
from common.zxscreen import ZX_RGB  # noqa: E402
from python.mixers import MIXERS  # noqa: E402
from python.quality import Oracle, _oklab  # noqa: E402
from python.twopage import two_page_mix  # noqa: E402

DE_OK, SPREAD_OK, EDGE_T = 0.02, 0.05, 0.08
WARMUP = 8


def lab(img):
    return _oklab(img.reshape(-1, 3)).reshape(img.shape)


def edges(l):
    """Edge pixels of an OKLab image: a lightness/chroma step to a neighbor."""
    dy = np.linalg.norm(np.diff(l, axis=0), axis=2)
    dx = np.linalg.norm(np.diff(l, axis=1), axis=2)
    e = np.zeros(l.shape[:2], bool)
    e[:-1] |= dy > EDGE_T
    e[1:] |= dy > EDGE_T
    e[:, :-1] |= dx > EDGE_T
    e[:, 1:] |= dx > EDGE_T
    return e


class Oracle2Accumulator:
    def __init__(self, clip, mixer, strict=None):
        self.clip, self.mixer = clip, mixer
        self.strict = strict if strict is not None else Oracle(clip, ZX_RGB)
        self.acc = {k: [] for k in ("color_ok", "color_ok_any", "de_p50", "de_p95", "edge_p95", "shimmer")}
        self.prev_out = self.prev_ref = None
        self.seen = 0

    def add(self, i, out, warrant=None):
        """Clip frame index i, the algorithm's output for it. -> error map (H x W x 3) or None.
        warrant: pixels where mixing is warranted (the per-pixel oracle or oracle3
        sees flicker). This oracle judges HOW to mix, not WHETHER: its references
        all mix, so outside the warrant it would penalize raw flashes and moving
        single-page textures (pageflip grid: 48 % / 7.6 px on accepted v10)."""
        clip = self.clip
        self.seen += 1
        if i - 1 < 0 or i + 1 >= len(clip) or self.seen <= WARMUP:
            self.prev_out, self.prev_ref = out, None
            return None
        cur, prv, nxt = clip.plane(i), clip.plane(i - 1), clip.plane(i + 1)
        raw = ZX_RGB[cur]
        refs = [self.mixer.mix(np.stack([prv, cur, nxt]), np.stack([np.full(cur.shape, w) for w in (0.25, 0.5, 0.25)])),
                two_page_mix(self.mixer, cur, prv, nxt)]
        labs = [lab(r) for r in refs]
        present = [np.ones(cur.shape, bool), np.ones(cur.shape, bool)]
        o = self.strict.at(i)
        if o is not None:
            flick, _, want = o
            labs.append(lab(want))
            present.append(flick)
        stack = np.stack(labs)
        pres = np.stack(present)[..., None]
        masked = np.where(pres, stack, np.nan)
        med = np.nanmedian(masked, axis=0)
        spread = np.nanmax(np.linalg.norm(masked - med, axis=3), axis=0)
        out_lab = lab(out)
        de = np.linalg.norm(out_lab - med, axis=2)
        de_any = np.nanmin(np.where(pres[..., 0], np.linalg.norm(stack - out_lab, axis=3), np.nan), axis=0)
        roi = np.linalg.norm(lab(raw) - med, axis=2) > DE_OK        # where mixing matters
        near = None
        if warrant is not None:
            roi &= warrant
            near = ndimage.binary_dilation(warrant, iterations=4)
        conf = roi & (spread <= SPREAD_OK)
        a = self.acc
        if conf.any():
            a["color_ok"].append(np.mean(de[conf] <= DE_OK))
            a["color_ok_any"].append(np.mean(de_any[conf] <= DE_OK))
            a["de_p50"].append(np.percentile(de[conf], 50))
            a["de_p95"].append(np.percentile(de[conf], 95))
        e_out, e_ref = edges(out_lab), edges(med)
        if near is not None:
            e_out &= near
            e_ref &= near
        far = None
        if e_out.any() and e_ref.any():
            dist_ref = ndimage.distance_transform_edt(~e_ref)
            d_to_ref = dist_ref[e_out]
            d_to_out = ndimage.distance_transform_edt(~e_out)[e_ref]
            a["edge_p95"].append(max(np.percentile(d_to_ref, 95), np.percentile(d_to_out, 95)))
            far = e_out & (dist_ref > 3)
        if self.prev_out is not None and self.prev_ref is not None:
            still = np.linalg.norm(med - self.prev_ref, axis=2) < 0.005
            if near is not None:
                still &= near
            if still.any():
                d = np.linalg.norm(out_lab - lab(self.prev_out), axis=2)
                a["shimmer"].append(float(d[still].mean()))
        self.prev_out, self.prev_ref = out, med
        err = np.zeros_like(raw)
        err[conf & (de > DE_OK)] = (255, 0, 255)
        if far is not None:
            err[far] = (255, 220, 0)
        self.last_reference = refs[1]
        return err

    def report(self):
        return {k: round(float(np.mean(v)), 4) if v else None for k, v in self.acc.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clip", required=True)
    ap.add_argument("--alg", required=True)
    ap.add_argument("--from", dest="first", type=int, required=True)
    ap.add_argument("--to", dest="last", type=int, required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--video", action="store_true")
    args = ap.parse_args()

    from python.run_algorithms import ALGORITHMS
    from python.video import SideBySide
    clip = ClipV2(args.clip)
    mixer = MIXERS["linear-mean"](ZX_RGB)
    alg = ALGORITHMS[args.alg]((clip.h, clip.w), mixer)
    delay = getattr(alg, "delay", 0)
    acc = Oracle2Accumulator(clip, mixer)
    os.makedirs(args.out, exist_ok=True)
    video = SideBySide(os.path.join(args.out, f"{args.name}-{args.alg}.mp4"), clip.h, clip.w, panels=4) if args.video else None

    def feed(j):
        i = clip.index_of_frame(j)
        pb = ClipV2.decode_planeb(clip.planeb(i))
        return alg.process(clip.plane(i), pb["attr"], pb["ink"])

    for j in range(args.first, args.first + delay):
        feed(j)
    for f in range(args.first, args.last + 1):
        out, _ = feed(f + delay)
        i = clip.index_of_frame(f)
        err = acc.add(i, out)
        if video and err is not None:
            video.add(ZX_RGB[clip.plane(i)], out, acc.last_reference, err)
    if video:
        video.close()
    res = acc.report()
    res.update(name=args.name, alg=args.alg, frames=[args.first, args.last])
    json.dump(res, open(os.path.join(args.out, f"{args.name}-{args.alg}.json"), "w"), indent=1)
    print(f"{args.name:22s} {args.alg:10s} color_ok {res['color_ok']}  any {res['color_ok_any']}  "
          f"dE p50/p95 {res['de_p50']}/{res['de_p95']}  edge_p95 {res['edge_p95']}px  shimmer {res['shimmer']}")


if __name__ == "__main__":
    main()
