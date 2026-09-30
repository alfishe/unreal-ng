"""Frame context shared by all detectors: a ring of raw frames with look-ahead
and a per-frame feature cache, so a feature is computed once however many
detectors use it.

Indexing is newest first: index 0 is frame t+L (the newest received), index L
is frame t (the frame being rendered), index L+k is t-k.
"""
import numpy as np


PAPER_Y0, PAPER_X0 = 48, 48
TILE_MOTION, RADIUS = 32, 8        # translation detector (v7)
TILE_FIELD = 16                    # field detector tiles (v10)


class Frame:
    """One raw frame and its lazily computed features."""

    def __init__(self, serial, plane, attr, ink, paper):
        self.serial = serial
        self.plane, self.attr, self.ink = plane, attr, ink
        self._paper = paper
        self._cache = {}

    def feature(self, name, fn):
        if name not in self._cache:
            self._cache[name] = fn()
        return self._cache[name]

    @property
    def key(self):
        """Cell key: paper - bitmap byte of the 8x1 segment + attribute the beam
        used (plane B); border - the pixel color (v6)."""
        def compute():
            h, w = self.plane.shape
            bits = self.ink.reshape(h, w // 8, 8).astype(np.int32)
            byte = np.zeros(bits.shape[:2], np.int32)
            for k in range(8):
                byte |= bits[:, :, k] << (7 - k)
            cell = (1 << 20) | (np.repeat(byte, 8, axis=1) << 8) | self.attr.astype(np.int32)
            return np.where(self._paper, cell, self.plane.astype(np.int32))
        return self.feature("key", compute)


class FrameContext:
    def __init__(self, shape, lookahead, depth, palette_rgb):
        self.h, self.w = shape
        self.L = lookahead
        self.depth = depth
        self.frames = []                    # newest first
        self.serial = 0
        self.paper = np.zeros(shape, bool)
        self.paper[PAPER_Y0:PAPER_Y0 + 192, PAPER_X0:PAPER_X0 + 256] = True
        # luma of the palette the frames are drawn in (the clip's, via the mixer)
        self.luma_lut = (palette_rgb.astype(np.float64) @ np.array([0.299, 0.587, 0.114])).astype(np.float32)
        self._pair_cache = {}               # (serial newer, serial older) -> feature

    def push(self, plane, attr, ink):
        self.frames.insert(0, Frame(self.serial, plane, attr, ink, self.paper))
        self.serial += 1
        del self.frames[self.depth:]
        live = {f.serial for f in self.frames}
        self._pair_cache = {k: v for k, v in self._pair_cache.items() if k[0] in live and k[1] in live}

    # ---- ring access ------------------------------------------------------
    @property
    def n(self):
        return len(self.frames)

    @property
    def t(self):
        """Index of the frame being rendered (fewer frames at start-up)."""
        return min(self.L, self.n - 1)

    def key(self, i):
        return self.frames[i].key

    def plane(self, i):
        return self.frames[i].plane

    def luma(self, i):
        f = self.frames[i]
        return f.feature("luma", lambda: self.luma_lut[f.plane])

    # ---- shared features ----------------------------------------------------
    def motion(self, i):
        """Pixels changed by a translation from frame i+1 to frame i (v7 D-motion):
        frame i is frame i+1 shifted by some v != 0 in the pixel's 32x32 tile
        (v at least 4x better than v = 0) and the pixel equals the shifted one."""
        if i + 1 >= self.n:
            return np.zeros((self.h, self.w), bool)
        key = (self.frames[i].serial, self.frames[i + 1].serial)
        if key not in self._pair_cache:
            self._pair_cache[key] = self._translation(self.plane(i), self.plane(i + 1))
        return self._pair_cache[key]

    def tile_hist(self, i):
        """16-color histogram per 16x16 tile (v10)."""
        f = self.frames[i]

        def compute():
            t = TILE_FIELD
            th, tw = self.h // t, self.w // t
            tiles = f.plane[:th * t, :tw * t].reshape(th, t, tw, t).transpose(0, 2, 1, 3).reshape(th, tw, t * t)
            hist = np.zeros((th, tw, 16), np.float32)
            for c in range(16):
                hist[..., c] = (tiles == c).sum(axis=2)
            return hist
        return f.feature("tile_hist", compute)

    def _translation(self, cur, prev):
        t, r = TILE_MOTION, RADIUS
        th, tw = self.h // t, self.w // t
        costs = np.empty(((2 * r + 1) ** 2, th, tw), np.float32)
        k = 0
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                costs[k] = (cur != np.roll(prev, (dy, dx), axis=(0, 1)))[:th * t, :tw * t].reshape(th, t, tw, t).sum(axis=(1, 3))
                k += 1
        zero = (2 * r + 1) * r + r
        nz = costs.copy()
        nz[zero] = np.inf
        best = np.argmin(nz, axis=0)
        best_cost = np.take_along_axis(nz, best[None], axis=0)[0]
        moving = (best_cost < 0.25 * costs[zero]) & (costs[zero] > 0.02 * t * t)
        dy = self.expand(best // (2 * r + 1) - r, t)
        dx = self.expand(best % (2 * r + 1) - r, t)
        yy, xx = np.indices((self.h, self.w))
        shifted = prev[(yy - dy) % self.h, (xx - dx) % self.w]
        return self.expand(moving, t).astype(bool) & (cur == shifted) & (cur != prev)

    def expand(self, tiles, t):
        th, tw = tiles.shape
        full = np.zeros((self.h, self.w), tiles.dtype)
        full[:th * t, :tw * t] = np.repeat(np.repeat(tiles, t, 0), t, 1)
        return full
