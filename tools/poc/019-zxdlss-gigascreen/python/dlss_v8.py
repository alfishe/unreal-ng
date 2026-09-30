"""POC v8 - mix only established cycles; everything else is the current raw frame.

v7 finding (Across the Edge balls): a ball shaded with GigaScreen and drawn
into one screen page at a time alternates at any given pixel for a few frames
only - it moves on. D-P detectors with a 3-period confirmation still catch it
at the top of a bounce (6 frames still), and mix pieces of the ball; the strip
the ball vacates waits for a new confirmation (a raw cap trailing the ball).

Static GigaScreen holds the same cycle on a pixel for dozens of frames. v8
mixes a pixel only when its series matches the cycle ESTABLISHED on that
pixel:
  established  the cell key repeated with period P for at least
               max(establish, 3P) frames (P = 2..5, independent per P; the
               brightness and translation vetoes of v7 apply), stored per
               pixel as the P keys of one cycle and kept while a sprite covers
               the pixel (analysis memory; dropped when the pixel shows a
               cycle value out of order, i.e. the pattern stopped)
  series       a window of P consecutive raw frames that contains t (with one
               frame of look-ahead: ending at t or at t+1) and is a rotation of
               the established cycle -> the average of that window
  otherwise    the raw frame t
The ball, its shading and its ghosts never match an established background
cycle, so they stay raw; the background next to them mixes from the second
frame it is visible again (the first, with t+1).
"""
import numpy as np

from common.zxscreen import ZX_RGB
from python.dlss_v1 import P2, PASS
from python.dlss_v7 import MOTION_VETO, DeflickerV7  # noqa: F401


class DeflickerV8(DeflickerV7):
    NEIGHBORS = [(0, -8), (0, 8), (0, -16), (0, 16), (0, -24), (0, 24), (0, -32), (0, 32), (-1, 0), (1, 0)]

    def __init__(self, shape, mixer, periods=(2, 3, 4, 5), establish=16, confirm_periods=3, adopt_age=32):
        super().__init__(shape, mixer, periods=periods, confirm=6, confirm_periods=confirm_periods)
        self.establish = establish
        self.delay = 1
        self.depth = max(self._span_est(p) for p in self.periods) + 1
        pmax = max(self.periods)
        self.cycle = np.zeros((pmax,) + shape, np.int32)     # established cycle keys, cycle[0..P-1]
        self.cycle_p = np.zeros(shape, np.uint8)            # its period, 0 = none
        self.cycle_age = np.zeros(shape, np.int32)          # frames the same cycle has been held
        self.adopt_age = adopt_age

    def _span_est(self, p):
        return max(self.establish, self.confirm_periods * p, 2 * p)

    def _periodic_from(self, first, p, span):
        """keys[first .. first+span-1] repeat with period p, not constant."""
        k = self.keys
        ok = np.ones(k[0].shape, bool)
        for j in range(first, first + span - p):
            ok &= k[j] == k[j + p]
        const = np.ones(k[0].shape, bool)
        for j in range(first + 1, first + p):
            const &= k[j] == k[first]
        return ok & ~const

    def _luma_flicker_from(self, first, p):
        lum = [self.luma[self.planes[first + j]] for j in range(p)]
        return np.any([lum[j] != lum[0] for j in range(1, p)], axis=0)

    def _rotation_match(self, first, p):
        """keys[first .. first+p-1] is a rotation of the established P-cycle."""
        k = self.keys
        match = np.zeros(k[0].shape, bool)
        for r in range(p):
            ok = self.cycle_p == p
            for j in range(p):
                ok &= k[first + j] == self.cycle[(j + r) % p]
            match |= ok
        return match

    def process(self, plane, attr, ink):
        for hist, v in ((self.keys, self._key(plane, attr, ink)), (self.planes, plane)):
            hist.insert(0, v)
            del hist[self.depth:]
        self.motion_hist.insert(0, self._motion())         # translation t -> t+1 (newest pair)
        del self.motion_hist[self.depth:]
        n = len(self.keys)
        if n < 2:
            return self.mixer.mix(plane[None], np.ones((1,) + plane.shape)), np.zeros((self.h, self.w // 8), np.uint8)

        # 1. establish cycles (window ending at the newest frame, t+1)
        claimed = np.zeros(plane.shape, bool)
        for p in self.periods:
            span = self._span_est(p)
            if n < span:
                continue
            est = self._periodic_from(0, p, span) & ~claimed
            est &= self._luma_flicker_from(0, p)
            if p > 2:
                est &= ~np.any(self.motion_hist[:span], axis=0)
            claimed |= est
            same = est & (self.cycle_p == p)
            for j in range(p):
                same &= np.any([self.keys[j] == self.cycle[(j + r) % p] for r in range(p)], axis=0)
            self.cycle_age[est & ~same] = 0
            for j in range(p):
                self.cycle[j][est] = self.keys[j][est]
            self.cycle_p[est] = p

        # 1b. adopt a neighbor's cycle: the region a bouncing ball keeps covering
        #     is never visible for `establish` frames in a row (red ball-shaped
        #     discs stayed raw in the orange stripe for the whole clip), but the
        #     same stripe runs next to it. A pixel without a cycle takes the
        #     cycle of a neighbor in its row (+-8..32 px, +-1 row) when its own
        #     last frames are exactly a window of that cycle; a ball's cell keys
        #     (other bitmap byte, other attribute) never are.
        if n >= 3:
            free = self.cycle_p == 0
            for dy, dx in self.NEIGHBORS:
                if not free.any():
                    break
                ncyc = np.roll(self.cycle, (dy, dx), axis=(1, 2))
                np_ = np.roll(self.cycle_p, (dy, dx), axis=(0, 1))
                # only a long-held neighbor cycle (a static stripe), never one
                # just established on a moving floor or on a ball
                np_ = np.where(np.roll(self.cycle_age, (dy, dx), axis=(0, 1)) >= self.adopt_age, np_, 0)
                for p in self.periods:
                    cand = free & (np_ == p)
                    if n < p + 1 or not cand.any():
                        continue
                    hit = np.zeros(plane.shape, bool)
                    for r in range(p):
                        ok = cand.copy()
                        for j in range(p):          # frames t .. t-P+1: one whole cycle
                            ok &= self.keys[1 + j] == ncyc[(j + r) % p]
                        hit |= ok
                    if hit.any():
                        for j in range(p):
                            self.cycle[j][hit] = ncyc[j][hit]
                        self.cycle_p[hit] = p
                        self.cycle_age[hit] = 0
                        free &= ~hit

        # 2. series for frame t (keys[1]): a P-window containing t that is a
        #    rotation of the established cycle; prefer the window ending at t
        period = np.zeros(plane.shape, np.uint8)
        start = np.zeros(plane.shape, np.uint8)            # 1: frames t..t-P+1, 0: t+1..t-P+2
        for p in self.periods:
            if n < p + 1:
                continue
            end_t = self._rotation_match(1, p)
            end_n = self._rotation_match(0, p) & ~end_t
            use = (end_t | end_n) & (period == 0)
            period[use] = p
            start[use & end_t] = 1

        # 3. the pattern stopped: t and t-1 both show cycle values, but not in
        #    cycle order (A,A for period 2). One cycle value next to a sprite
        #    value is occlusion (a ball drawn into one page only) - keep it.
        if n >= 3:
            kt, kp = self.keys[1], self.keys[2]
            in_t = np.zeros(plane.shape, bool)
            in_p = np.zeros(plane.shape, bool)
            in_order = np.zeros(plane.shape, bool)
            for j in range(max(self.periods)):
                valid = j < self.cycle_p
                nxt = self.cycle[(j + 1) % np.maximum(self.cycle_p, 1), np.arange(self.h)[:, None], np.arange(self.w)[None, :]]
                in_t |= valid & (kt == self.cycle[j])
                in_p |= valid & (kp == self.cycle[j])
                in_order |= valid & (kt == self.cycle[j]) & (kp == nxt)
            self.cycle_p[in_t & in_p & ~in_order] = 0
        self.cycle_age = np.where(self.cycle_p > 0, self.cycle_age + 1, 0)

        # render: the chosen window of P raw frames, or raw t
        pmax = max(self.periods)
        k = min(n, pmax + 1)
        planes = np.stack(self.planes[:k])                  # planes[0] = t+1, planes[1] = t
        weights = np.zeros(planes.shape)
        weights[1][period == 0] = 1.0
        for p in self.periods:
            for s in (0, 1):
                m = (period == p) & (start == s)
                if not m.any():
                    continue
                for j in range(p):
                    weights[s + j][m] = 1.0 / p
        out = self.mixer.mix(planes, weights)

        seg = period.reshape(self.h, self.w // 8, 8).max(axis=2)
        cls = np.where(seg > 0, P2 + seg.astype(np.int16) - 2, PASS).astype(np.uint8)
        self.last_motion = self.motion_hist[0].reshape(self.h, self.w // 8, 8).any(axis=2) & (seg == 0)
        return out, cls
