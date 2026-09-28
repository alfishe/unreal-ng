"""POC v5 - classification on plane B (exact bitmap + the attribute the beam used).

v4 finding: pixel colors alone cannot tell a moving grid edge or the halo of
a bouncing ball from GigaScreen. Plane B gives, per 8x1 paper segment, the
bitmap byte (ink bits) and the attribute actually used for it, multicolor
included. v5:

  - Paper segment signature = (bitmap, attribute) from plane B.
      joint (bitmap, attr) strict period P / near-period-2  -> mix in place (C1/C2/C3)
      attribute periodic, bitmap not                        -> SPLIT (C4): current ink
                                                                bit, ink/paper colors
                                                                averaged over the attr cycle
      else                                                  -> raw
  - Border segments: 8 color indices (as v1-v4).
  - Sustain: a segment mixes only after its pattern held for `sustain`
    consecutive frames; a never-seen value drops it at once (a bouncing ball
    alternates for a few frames only; GigaScreen holds for dozens).
  - Optional v4 tile motion rule (`tile_motion=True`).
"""
import numpy as np

from python.dlss_v1 import CLASS_COLORS, CONST, HOLD, P2, P5, PASS, RECUR  # noqa: F401

SPLIT = 9          # class code for the class map
PAPER_Y0, PAPER_X0 = 48, 48


class DeflickerV5:
    def __init__(self, shape, mixer, window=10, max_period=5, sustain=8, alt_min=0.7, change_min=0.5,
                 max_distinct=3, tile_motion=False, tile=32, radius=8, tile_memory=6):
        self.h, self.w = shape
        self.mixer = mixer
        self.window, self.max_period, self.sustain = window, max_period, sustain
        self.alt_min, self.change_min, self.max_distinct = alt_min, change_min, max_distinct
        self.tile_motion, self.tile, self.radius, self.tile_memory = tile_motion, tile, radius, tile_memory
        self.sw = self.w // 8
        self.sig_hist, self.attr_hist, self.plane_hist, self.ink_hist = [], [], [], []
        self.streak = np.zeros((self.h, self.sw), np.int32)
        self.th, self.tw = self.h // tile, self.w // tile
        self.tile_left = np.zeros((self.th, self.tw), np.uint8)
        self.paper_seg = np.zeros((self.h, self.sw), bool)
        self.paper_seg[PAPER_Y0:PAPER_Y0 + 192, PAPER_X0 // 8:PAPER_X0 // 8 + 32] = True

    # ---- signatures -----------------------------------------------------
    def _signatures(self, plane, attr, ink):
        """uint64 per 8x1 segment: paper = (1 << 40) | bitmap << 8 | attr; border = 8 color nibbles."""
        h, sw = self.h, self.sw
        col = plane.reshape(h, sw, 8).astype(np.uint64) & 0xF
        border_sig = np.zeros((h, sw), np.uint64)
        for k in range(8):
            border_sig |= col[:, :, k] << np.uint64(4 * k)
        bits = ink.reshape(h, sw, 8).astype(np.uint64)
        bitmap = np.zeros((h, sw), np.uint64)
        for k in range(8):
            bitmap |= bits[:, :, k] << np.uint64(7 - k)
        seg_attr = attr.reshape(h, sw, 8)[:, :, 0].astype(np.uint64)
        paper_sig = (np.uint64(1) << np.uint64(40)) | (bitmap << np.uint64(8)) | seg_attr
        return np.where(self.paper_seg, paper_sig, border_sig), np.where(self.paper_seg, seg_attr, border_sig)

    @staticmethod
    def _periodic(h, max_period):
        """-> (period per segment 0 = none, near-period-2 mask) over history h (newest first)."""
        k = h.shape[0]
        period = np.zeros(h.shape[1:], np.uint8)
        const = np.all(h == h[0], axis=0)
        for p in range(2, max_period + 1):
            if k < 2 * p:
                break
            ok = np.all(h[:k - p] == h[p:], axis=0) & ~const & (period == 0)
            period[ok] = p
        return period, const

    def _near_p2(self, h):
        k = h.shape[0]
        if k < 5:
            return np.zeros(h.shape[1:], bool)
        seen = np.any(h[1:] == h[0], axis=0)
        alt = np.mean(h[:-2] == h[2:], axis=0)
        chg = np.mean(h[:-1] != h[1:], axis=0)
        distinct = np.ones(h.shape[1:], np.int32)
        for i in range(1, k):
            distinct += np.all(h[:i] != h[i], axis=0)
        return seen & (distinct >= 2) & (distinct <= self.max_distinct) & (alt >= self.alt_min) & (chg >= self.change_min)

    # ---- motion (v4 rule, optional) ------------------------------------
    def _moving_tiles(self):
        if len(self.plane_hist) < 3:
            return np.zeros((self.th, self.tw), bool)
        cur, ref = self.plane_hist[0], self.plane_hist[2]
        t, r = self.tile, self.radius
        best = np.full((self.th, self.tw), np.inf)
        best_v = np.zeros((self.th, self.tw), bool)
        zero = None
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                c = (cur != np.roll(ref, (dy, dx), axis=(0, 1)))[:self.th * t, :self.tw * t]
                c = c.reshape(self.th, t, self.tw, t).sum(axis=(1, 3)) + 1e-3 * (abs(dy) + abs(dx))
                if dy == 0 and dx == 0:
                    zero = c
                better = c < best
                best[better] = c[better]
                best_v[better] = (dy != 0 or dx != 0)
        return best_v & (best < 0.5 * zero) & (zero > 0.02 * t * t)

    # ---- per frame ------------------------------------------------------
    def process(self, plane, attr, ink):
        sig, attr_sig = self._signatures(plane, attr, ink)
        for hist, v in ((self.sig_hist, sig), (self.attr_hist, attr_sig), (self.plane_hist, plane), (self.ink_hist, ink)):
            hist.insert(0, v)
            del hist[self.window:]
        hs, ha = np.stack(self.sig_hist), np.stack(self.attr_hist)

        period, const = self._periodic(hs, self.max_period)
        near = self._near_p2(hs) & (period == 0) & ~const
        a_period, a_const = self._periodic(ha, self.max_period)
        a_near = self._near_p2(ha) & (a_period == 0) & ~a_const
        split = self.paper_seg & (period == 0) & ~near & ~const & ((a_period > 0) | a_near)

        cls = np.full(sig.shape, PASS, np.uint8)
        cls[const] = CONST
        per = (period > 0)
        cls[per] = P2 + np.clip(period[per].astype(np.int16) - 2, 0, 3).astype(np.uint8)
        cls[near] = RECUR
        cls[split] = SPLIT

        if self.tile_motion:
            moving = self._moving_tiles()
            self.tile_left = np.where(moving, self.tile_memory, np.maximum(self.tile_left.astype(np.int16) - 1, 0)).astype(np.uint8)
            t = self.tile
            recent = np.zeros(sig.shape, bool)
            recent[:self.th * t, :self.tw * t // 8] = np.repeat(np.repeat(self.tile_left > 0, t, 0), t // 8, 1)
            cls[recent & (cls != CONST) & (cls != P2)] = PASS

        # sustain: count consecutive frames in a mixing class, reset on anything else
        mixing = (cls != PASS) & (cls != CONST)
        self.streak = np.where(mixing, self.streak + 1, 0)
        active = mixing & (self.streak >= self.sustain)
        shown = np.where(active | (cls == CONST), cls, PASS).astype(np.uint8)

        # ---- compose: planes + weights per pixel ----
        k = len(self.plane_hist)
        planes = np.stack(self.plane_hist).copy()
        weights = np.zeros((k, self.h, self.w))
        cls_px = np.repeat(shown, 8, axis=1)
        per_px = np.repeat(np.where(shown == SPLIT, np.maximum(a_period, 2), period), 8, axis=1)
        raw = (cls_px == PASS) | (cls_px == CONST)
        weights[0][raw] = 1.0
        strict = (~raw) & (cls_px != RECUR) & (cls_px != SPLIT)
        for p in range(2, self.max_period + 1):
            m = strict & (per_px == p)
            for j in range(min(p, k)):
                weights[j][m] = 1.0 / min(p, k)
        weights[:, cls_px == RECUR] = 1.0 / k

        sp = cls_px == SPLIT
        if sp.any():
            # current ink bit, colors of the attribute cycle: ink or paper of attr(t-j)
            ink_now = self.ink_hist[0]
            a_hist = np.stack([np.repeat(a.astype(np.int64), 8, axis=1) for a in self.attr_hist])
            bright = (a_hist >> 6) & 1
            ink_c = (a_hist & 7) + 8 * bright
            paper_c = ((a_hist >> 3) & 7) + 8 * bright
            cycle = np.where(np.repeat(a_near, 8, axis=1), k, per_px)
            for j in range(k):
                col = np.where(ink_now, ink_c[j], paper_c[j]).astype(np.uint8)
                use = sp & (j < cycle)
                planes[j][use] = col[use]
                weights[j][use] = 1.0 / np.maximum(cycle[use], 1)
        return self.mixer.mix(planes, weights), shown
