"""POC v3 - v2 plus a motion gate: change explained by a shift is motion, not flicker.

v2 review (2026-09-28): ghosts from the bouncing balls (balls-floor), distorted
edges of the winding spiral (irregular), distorted checker grid scrolling in
the background (pageflip-static). All three are motion that looks periodic
per pixel: a grid scrolling one cell per two frames IS period 2 in place.
Per-segment history cannot tell it from GigaScreen; neighbourhood motion can.

Motion gate, per frame, comparing frame t with t-2 - the same phase of a
period-2 flicker, so the flicker cancels and only motion remains (comparing
with t-1 found nothing: the flicker on the same tile hid the motion):
  1. 32x32 tiles; for each tile find the shift v (|dx|,|dy| <= R) that best
     matches t against t-2. Ties go to the smallest shift - ZX content repeats
     every 8 pixels (attribute cells, checkers), so large shifts alias.
  2. The tile moves when v != 0 explains the change much better than no shift
     (cost(v) < ratio * cost(0)) and enough pixels changed. GigaScreen whose
     phases are offset images has t == t-2 in place, so it never qualifies.
  3. In a moving tile, a segment that changed against t-2 and matches t-2
     shifted by v (>= 7 of 8 pixels) is motion: PASS now and for `cooldown`
     more frames.
Motion that is also flicker (C6) passes through unmixed here - no ghosts,
flicker stays; motion-compensated mixing is the next version.
"""
import numpy as np

from python.dlss_v1 import PASS
from python.dlss_v2 import DeflickerV2

MOTION = 8  # extra class code for the class map (rendered like PASS)


class DeflickerV3(DeflickerV2):
    def __init__(self, shape, mixer, tile=32, radius=8, ratio=0.5, min_changed=0.02, match_pixels=7,
                 cooldown=2, **kw):
        self.tile, self.radius, self.ratio = tile, radius, ratio
        self.min_changed, self.match_pixels, self.cooldown = min_changed, match_pixels, cooldown
        super().__init__(shape, mixer, **kw)
        self.th, self.tw = self.h // tile, self.w // tile
        self.motion_left = np.zeros((self.h, self.w // 8), np.uint8)
        self.shifts = [(dy, dx) for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)]

    def _tile_sum(self, m):
        t = self.tile
        return m[:self.th * t, :self.tw * t].reshape(self.th, t, self.tw, t).sum(axis=(1, 3))

    def _motion_segments(self):
        if len(self.plane_hist) < 3:
            return np.zeros((self.h, self.w // 8), bool)
        cur, ref = self.plane_hist[0], self.plane_hist[2]
        # cost of every shift, per tile; shifted[y, x] = ref[y - dy, x - dx]
        costs = np.empty((len(self.shifts), self.th, self.tw), np.float64)
        match_maps = {}
        for s, (dy, dx) in enumerate(self.shifts):
            eq = cur == np.roll(ref, (dy, dx), axis=(0, 1))
            costs[s] = self._tile_sum(~eq) + 1e-3 * (abs(dy) + abs(dx))   # ties -> smallest shift
            match_maps[s] = eq
        zero = self.shifts.index((0, 0))
        best = np.argmin(costs, axis=0)
        best_cost = np.take_along_axis(costs, best[None], axis=0)[0]
        zero_cost = costs[zero]
        area = self.tile * self.tile
        moving = (best != zero) & (best_cost < self.ratio * zero_cost) & (zero_cost > self.min_changed * area)

        self.moving_tiles = moving

        # per segment: changed against t-2 and explained by its tile's shift
        seg_motion = np.zeros((self.h, self.w // 8), bool)
        changed = np.any((cur != ref).reshape(self.h, self.w // 8, 8), axis=2)
        t = self.tile
        for ty, tx in zip(*np.nonzero(moving)):
            eq = match_maps[best[ty, tx]][ty * t:(ty + 1) * t, tx * t:(tx + 1) * t]
            ok = eq.reshape(t, t // 8, 8).sum(axis=2) >= self.match_pixels
            seg = seg_motion[ty * t:(ty + 1) * t, tx * t // 8:(tx + 1) * t // 8]
            seg |= ok & changed[ty * t:(ty + 1) * t, tx * t // 8:(tx + 1) * t // 8]
        return seg_motion

    def _classify(self):
        cls, period = super()._classify()
        motion = self._motion_segments()
        self.motion_left = np.where(motion, self.cooldown + 1, np.maximum(self.motion_left.astype(np.int16) - 1, 0)).astype(np.uint8)
        gated = self.motion_left > 0
        cls = cls.copy()
        cls[gated & (cls != 1)] = PASS          # keep CONST, everything else passes through
        self.state = np.where(gated, PASS, self.state).astype(np.uint8)
        self.hold_left = np.where(gated, 0, self.hold_left).astype(np.uint8)
        self.last_motion = motion
        return cls, period
