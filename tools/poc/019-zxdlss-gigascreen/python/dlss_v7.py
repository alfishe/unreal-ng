"""POC v7 - independent detectors per period and per signal, final mask by consensus.

Render rule (unchanged from v6): outside the mask the pixel comes from the
current raw frame only; inside it is the average of the last P raw frames
(P = 2..5). Detectors are analysis only and may keep any history.

Detectors, each on its own:
  D-P (P = 2, 3, 4, 5)  cell key (plane B: bitmap byte + attribute the beam used;
                        border: the pixel color) repeats strictly with period P
                        over the last 3 periods (P2: 6 frames .. P5: 15), and is
                        not constant
  D-luma                the P frames of the series differ in brightness: equal
                        brightness does not flicker to the eye -> not mixed
  D-motion              the raw picture moved: frame t is frame t-1 translated
                        by some v != 0 in the pixel's 32x32 tile (at least 4x
                        better than v = 0) and the pixel changed by it. A texture moving
                        at a constant speed repeats with a period per pixel (the
                        pageflip checker grid gave period 5) - D-P alone takes
                        it for flicker. The veto covers every frame of the
                        window D-P decided on (flicker test: a bouncing square
                        lights a pixel once every 5 frames).

Consensus: the smallest P whose D-P holds, D-luma holds, and D-motion does not
veto. D-motion does not veto P = 2: A,B,A,B at 50 Hz is seen as the mix, moving
or not (the quality oracle's rule too).

Class map (per 8-pixel segment): P2..P5 colors of v1 for the mixing period,
red where D-motion vetoed a period candidate, gray where D-luma did.
SIMD-CANDIDATE: key compares over the ring; the tile search is the GPU/SIMD
hot spot (O-series backlog).
"""
import numpy as np

from common.zxscreen import ZX_RGB
from python.dlss_v1 import P2, PASS

MOTION_VETO = 8     # class code: red in run.py
LUMA_VETO = 1       # class code: CONST (dark gray) reused for "no brightness flicker"


class DeflickerV7:
    PAPER_Y0, PAPER_X0 = 48, 48
    TILE, RADIUS = 32, 8

    def __init__(self, shape, mixer, periods=(2, 3, 4, 5), confirm=6, confirm_periods=3):
        self.h, self.w = shape
        self.mixer = mixer
        self.periods = tuple(sorted(periods))
        self.confirm = confirm
        # each D-P holds for the same number of periods (P2: 6 frames, P5: 15):
        # two periods of P3 matched short chance patterns (a border blinking
        # 1,0,0,1,0,0 once)
        self.confirm_periods = confirm_periods
        self.depth = max(self._span(p) for p in self.periods)
        self.keys, self.planes = [], []
        self.paper = np.zeros(shape, bool)
        self.paper[self.PAPER_Y0:self.PAPER_Y0 + 192, self.PAPER_X0:self.PAPER_X0 + 256] = True
        # brightness per palette index (Rec. 601 on the sRGB values)
        self.luma = (ZX_RGB.astype(np.float64) @ np.array([0.299, 0.587, 0.114])).astype(np.float32)
        self.motion_hist = []           # per-frame motion masks, newest first (analysis only)
        self.last_motion = None         # run.py paints it red (per segment)

    # ---- detectors ------------------------------------------------------
    def _key(self, plane, attr, ink):
        bits = ink.reshape(self.h, self.w // 8, 8).astype(np.int32)
        byte = np.zeros(bits.shape[:2], np.int32)
        for k in range(8):
            byte |= bits[:, :, k] << (7 - k)
        cell = (1 << 20) | (np.repeat(byte, 8, axis=1) << 8) | attr.astype(np.int32)
        return np.where(self.paper, cell, plane.astype(np.int32))

    def _span(self, p):
        return max(2 * p, self.confirm, self.confirm_periods * p)

    def _period(self, p):
        k = self.keys
        span = self._span(p)
        if len(k) < span:
            return np.zeros(k[0].shape, bool)
        ok = np.ones(k[0].shape, bool)
        for j in range(span - p):
            ok &= k[j] == k[j + p]
        const = np.ones(k[0].shape, bool)
        for j in range(1, p):
            const &= k[j] == k[0]
        return ok & ~const

    def _luma_flicker(self, p):
        lum = [self.luma[self.planes[j]] for j in range(p)]
        return np.any([lum[j] != lum[0] for j in range(1, p)], axis=0)

    def _shift_costs(self, cur, ref):
        """Mismatching pixels per 32x32 tile for every shift v: (2R+1)^2 x th x tw."""
        t, r = self.TILE, self.RADIUS
        th, tw = self.h // t, self.w // t
        costs = np.empty(((2 * r + 1) ** 2, th, tw), np.float32)
        k = 0
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                costs[k] = (cur != np.roll(ref, (dy, dx), axis=(0, 1)))[:th * t, :tw * t].reshape(th, t, tw, t).sum(axis=(1, 3))
                k += 1
        return costs

    def _expand(self, tiles):
        t = self.TILE
        th, tw = tiles.shape
        full = np.zeros((self.h, self.w), tiles.dtype)
        full[:th * t, :tw * t] = np.repeat(np.repeat(tiles, t, 0), t, 1)
        return full

    @staticmethod
    def _sample(plane, dy, dx):
        h, w = plane.shape
        yy, xx = np.indices((h, w))
        return plane[(yy - dy) % h, (xx - dx) % w]

    def _motion(self):
        """Pixels changed by a translation: frame t is frame t-1 shifted by some
        v != 0 in the pixel's 32x32 tile (v explains the tile at least 4x better
        than v = 0) and the pixel equals the shifted t-1.

        No constant velocity needed: the pageflip checker grid moves in uneven
        steps ((0,-8), (8,-8), then a frame that is no shift), so no single v
        explained two steps, while every step is still a translation."""
        if len(self.planes) < 2:
            return np.zeros((self.h, self.w), bool)
        cur, prev = self.planes[0], self.planes[1]
        costs = self._shift_costs(cur, prev)                # cur(x) = prev(x - v)
        r = self.RADIUS
        zero = (2 * r + 1) * r + r                          # index of v = (0, 0)
        nz = costs.copy()
        nz[zero] = np.inf
        best = np.argmin(nz, axis=0)
        best_cost = np.take_along_axis(nz, best[None], axis=0)[0]
        moving = (best_cost < 0.25 * costs[zero]) & (costs[zero] > 0.02 * self.TILE * self.TILE)
        dy = self._expand(best // (2 * r + 1) - r)
        dx = self._expand(best % (2 * r + 1) - r)
        return self._expand(moving) & (cur == self._sample(prev, dy, dx)) & (cur != prev)

    # ---- per frame ------------------------------------------------------
    def process(self, plane, attr, ink):
        for hist, v in ((self.keys, self._key(plane, attr, ink)), (self.planes, plane)):
            hist.insert(0, v)
            del hist[self.depth:]

        self.motion_hist.insert(0, self._motion())
        del self.motion_hist[self.depth:]
        period = np.zeros(plane.shape, np.uint8)
        vetoed_motion = np.zeros(plane.shape, bool)
        vetoed_luma = np.zeros(plane.shape, bool)
        for p in self.periods:
            cand = self._period(p) & (period == 0)
            if not cand.any():
                continue
            luma_ok = self._luma_flicker(p)
            # the veto covers the window D-P decided on: a square bouncing across
            # a pixel lights it once every 5 frames - period 5 on that pixel, while
            # the pixel itself is static in the frame being mixed
            span = self._span(p)
            motion_veto = np.any(self.motion_hist[:span], axis=0) if p > 2 else np.zeros(plane.shape, bool)
            vetoed_luma |= cand & ~luma_ok
            vetoed_motion |= cand & luma_ok & motion_veto
            period[cand & luma_ok & ~motion_veto] = p
            # a vetoed pixel may not take a longer period instead
            period[cand & ~(luma_ok & ~motion_veto)] = 255
        period[period == 255] = 0

        # render: the last P raw frames, nothing older
        pmax = max(self.periods)
        planes = np.stack(self.planes[:pmax] + [self.planes[0]] * (pmax - len(self.planes[:pmax])))
        weights = np.zeros(planes.shape)
        weights[0][period == 0] = 1.0
        for p in self.periods:
            m = period == p
            for j in range(p):
                weights[j][m] = 1.0 / p
        out = self.mixer.mix(planes, weights)

        seg = period.reshape(self.h, self.w // 8, 8).max(axis=2)
        cls = np.where(seg > 0, P2 + seg.astype(np.int16) - 2, PASS).astype(np.uint8)
        seg_luma = vetoed_luma.reshape(self.h, self.w // 8, 8).any(axis=2)
        cls[(seg == 0) & seg_luma] = LUMA_VETO
        self.last_motion = vetoed_motion.reshape(self.h, self.w // 8, 8).any(axis=2)
        return out, cls
