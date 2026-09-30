"""POC v9 - decisions from a window around t with L frames of look-ahead; no memory.

v8 finding: decisions from the past only need memory (established cycles,
adoption from neighbors) and memory goes stale - a ball-shaped patch of raw
background stays where the ball was half a second ago until a cycle is
re-established; the irregular spiral's background (phase slips) waits for a
new confirmation after every slip. With look-ahead the frames after t say
what the pixel does next, so nothing needs to be remembered.

Present delay: L frames (process() gets frame t+L and returns frame t).

Per pixel, for frame t:
  D-P (P = 2..5)  some run of max(6, 3P) consecutive frames inside
                  [t - 15, t + L] contains t and repeats strictly with period P
                  (cell key: bitmap byte + attribute from plane B; border: color)
  D-luma          the P frames of the series differ in brightness
  D-motion        no translation (v7 detector) inside that run, for P >= 3
  consensus       smallest P that passes
Render: P raw frames of that run containing t (past or future), else raw t.
A ball's alternation at a pixel is shorter than 3 periods; the background it
has just uncovered continues in the next frames, so it mixes at once.
"""
import numpy as np

from python.dlss_v1 import P2, PASS
from python.dlss_v7 import DeflickerV7


class DeflickerV9(DeflickerV7):
    def __init__(self, shape, mixer, lookahead=4, periods=(2, 3, 4, 5), confirm=6, confirm_periods=3):
        super().__init__(shape, mixer, periods=periods, confirm=confirm, confirm_periods=confirm_periods)
        self.L = lookahead
        self.delay = lookahead
        self.depth = self.L + max(self._span(p) for p in self.periods)

    def _run_periodic(self, a, p, span):
        """keys[a .. a+span-1] (newest first) repeat with period p, not constant."""
        k = self.keys
        ok = np.ones(k[0].shape, bool)
        for j in range(a, a + span - p):
            ok &= k[j] == k[j + p]
        const = np.ones(k[0].shape, bool)
        for j in range(a + 1, a + p):
            const &= k[j] == k[a]
        return ok & ~const

    def process(self, plane, attr, ink):
        for hist, v in ((self.keys, self._key(plane, attr, ink)), (self.planes, plane)):
            hist.insert(0, v)
            del hist[self.depth:]
        self.motion_hist.insert(0, self._motion())         # motion_hist[i]: frame i vs frame i+1
        del self.motion_hist[self.depth:]
        n = len(self.keys)
        L = min(self.L, n - 1)                              # index of frame t (start-up: fewer frames)

        period = np.zeros(plane.shape, np.uint8)
        start = np.zeros(plane.shape, np.int16)             # index of the newest frame of the series
        for p in self.periods:
            span = self._span(p)
            free = period == 0
            if not free.any():
                break
            # runs [a, a+span-1] that contain t: a <= L <= a+span-1, a >= 0, a+span <= n
            for a in range(max(0, L - span + 1), min(L, n - span) + 1):
                run = self._run_periodic(a, p, span) & free
                if not run.any():
                    continue
                # series: P frames of the run containing t, as old as possible
                b = min(L, a + span - p)
                b = max(b, a, L - p + 1)
                lum = [self.luma[self.planes[b + j]] for j in range(p)]
                run &= np.any([lum[j] != lum[0] for j in range(1, p)], axis=0)
                if p > 2:
                    run &= ~np.any(self.motion_hist[a:a + span - 1], axis=0)
                period[run] = p
                start[run] = b
                free &= ~run

        # render: the chosen P raw frames around t, or raw t
        planes = np.stack(self.planes[:n])
        weights = np.zeros(planes.shape)
        weights[L][period == 0] = 1.0
        for p in self.periods:
            m = period == p
            if not m.any():
                continue
            for b in np.unique(start[m]):
                mb = m & (start == b)
                for j in range(p):
                    weights[b + j][mb] = 1.0 / p
        out = self.mixer.mix(planes, weights)

        seg = period.reshape(self.h, self.w // 8, 8).max(axis=2)
        cls = np.where(seg > 0, P2 + seg.astype(np.int16) - 2, PASS).astype(np.uint8)
        self.last_motion = np.zeros(seg.shape, bool)
        return out, cls
