"""Third oracle: pixel XOR + averaging, with rhythm and brightness channels.

v1 of this oracle (t == t-2 and t != t-1 -> mix) scored 0 % on the raster
negatives v10 handles right: a one-frame inverted flash makes the frame
AFTER it look like A,B,A, and XOR alone cannot tell a flash from rhythm.

Channels, two-sided, window W = 6 frames around t:
  XOR/rhythm  per pixel and period P = 2..5: the run of frames k with
              color(k) == color(k-P) through t; it must cover at least two whole
              periods (2P + 1 frames) and not be constant
  brightness  mean luma of every frame, whole frame and per 32x32 tile; a frame
              is a FLASH where its luma jumps against both neighbors and does
              not repeat at any period P = 2..5 (a rhythmic strobe repeats, a
              flash does not); a rhythm run that includes a flash frame of the
              pixel's tile does not count
Reference: the mean of the P frames of the run that contain t (as centered as
the run allows); otherwise the raw pixel t.
Dimension:
  agree   share of pixels where the output or the reference differs from raw t
          whose output is within dE 0.02 (OKLab) of the reference (higher is better)
"""
import numpy as np

from common.zxscreen import ZX_RGB
from python.oracle2 import DE_OK, lab

W = 6
PERIODS = (2, 3, 4, 5)
TILE = 32
FLASH_JUMP = 12.0          # luma units (0..255) against both neighbors


class Oracle3Accumulator:
    def __init__(self, clip, mixer):
        self.clip, self.mixer = clip, mixer
        self.luma = (ZX_RGB.astype(np.float64) @ np.array([0.299, 0.587, 0.114])).astype(np.float32)
        self.agree = []
        self.last = None

    def _tile_luma(self, plane):
        h, w = plane.shape
        th, tw = h // TILE, w // TILE
        l = self.luma[plane][:th * TILE, :tw * TILE]
        return l.reshape(th, TILE, tw, TILE).mean(axis=(1, 3))

    def _flashes(self, seq):
        """seq: (2W+1) x th x tw tile luma -> (2W+1) x th x tw flash flags.

        A flash is a lone spike on a steady background: the frame's luma jumps
        against both neighbors, the neighbors agree with each other, and the
        frame's luma does not come back at any period P (within half the jump).
        A two-page scene repeats every 2 frames even while it moves (the first
        version, repeat within 4 luma units, called 29 % of the tunnel a flash)."""
        n = seq.shape[0]
        flash = np.zeros(seq.shape, bool)
        for k in range(1, n - 1):
            d_prev, d_next = np.abs(seq[k] - seq[k - 1]), np.abs(seq[k] - seq[k + 1])
            jump = np.minimum(d_prev, d_next)
            spike = (jump > FLASH_JUMP) & (np.abs(seq[k - 1] - seq[k + 1]) < 0.5 * jump)
            repeats = np.zeros(seq.shape[1:], bool)
            for p in PERIODS:
                for kk in (k - p, k + p):
                    if 0 <= kk < n:
                        repeats |= np.abs(seq[k] - seq[kk]) <= 0.5 * jump
            flash[k] = spike & ~repeats
        return flash

    def reference(self, i):
        """-> (reference RGB, rhythm period per pixel 0 = none, flash mask per pixel)."""
        clip = self.clip
        idx = list(range(i - W, i + W + 1))
        planes = np.stack([clip.plane(j) for j in idx])            # index W = t
        h, w = planes.shape[1:]
        tl = np.stack([self._tile_luma(p) for p in planes])
        th, tw = tl.shape[1:]
        flash_t = self._flashes(tl)
        flash = np.zeros(planes.shape, bool)
        flash[:, :th * TILE, :tw * TILE] = np.repeat(np.repeat(flash_t, TILE, 1), TILE, 2)

        period = np.zeros((h, w), np.uint8)
        start = np.zeros((h, w), np.int16)                          # first frame of the P-series
        n = planes.shape[0]
        for p in PERIODS:
            eq = np.zeros(planes.shape, bool)                       # eq[k]: color(k) == color(k-p)
            eq[p:] = planes[p:] == planes[:-p]
            # run of eq around t: extend left from t and right from t
            left = np.full((h, w), W, np.int16)                     # first k of the run (eq[k] true)
            ok = eq[W].copy()
            for k in range(W - 1, p - 1, -1):
                ok &= eq[k]
                left = np.where(ok, k, left)
            right = np.full((h, w), W, np.int16)
            ok = eq[W].copy()
            for k in range(W + 1, n):
                ok &= eq[k]
                right = np.where(ok, k, right)
            # frames covered: left-p .. right; two periods on both sides of t
            covered_lo, covered_hi = left - p, right
            # two whole periods in a row containing t (a lone flash gives 3 frames);
            # not required on both sides - that cut the edges of real bursts
            rhythm = eq[W] & (covered_hi - covered_lo + 1 >= 2 * p + 1)
            nonconst = np.zeros((h, w), bool)
            for j in range(1, p):
                nonconst |= planes[W] != planes[W - j]
            rhythm &= nonconst
            # no flash frame inside the run
            for k in range(n):
                inside = (k >= covered_lo) & (k <= covered_hi)
                rhythm &= ~(inside & flash[k])
            take = rhythm & (period == 0)
            period[take] = p
            start[take] = np.clip(W - p // 2, covered_lo, covered_hi - p + 1)[take]

        cur = planes[W]
        weights = np.zeros(planes.shape)
        weights[W][period == 0] = 1.0
        for p in PERIODS:
            m = period == p
            if not m.any():
                continue
            for s in np.unique(start[m]):
                ms = m & (start == s)
                for j in range(p):
                    weights[s + j][ms] = 1.0 / p
        ref = self.mixer.mix(planes, weights)
        return ref, period, flash[W]

    def add(self, i, out):
        if i - W < 0 or i + W >= len(self.clip):
            return
        ref, period, flash = self.reference(i)
        raw = ZX_RGB[self.clip.plane(i)]
        self.last = (ref, period, flash)
        roi = np.any(out != raw, axis=2) | np.any(ref != raw, axis=2)
        if roi.any():
            de = np.linalg.norm(lab(out) - lab(ref), axis=2)
            self.agree.append(float(np.mean(de[roi] <= DE_OK)))
        else:
            self.agree.append(1.0)

    def report(self):
        # nothing to compare on a frame (neither mixes) is full agreement
        return {"agree": round(float(np.mean(self.agree)), 4) if self.agree else 1.0}
