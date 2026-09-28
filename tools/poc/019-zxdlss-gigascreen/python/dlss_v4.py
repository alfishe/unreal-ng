"""POC v4 - v3 with a tile-level motion rule.

v3 quality review (oracle, 2026-09-28): on pageflip-static the scrolling
checker produced "period 5" at cell edges (the grid moves 4 frames and pauses
1), and the per-segment motion match missed those edges, so they were mixed -
ghost 3.8 % of all pixels. In a tile that is moving, a per-segment match is
not reliable enough.

Rule: a tile that moved within the last `tile_memory` frames admits only
strict period-2 segments that did not match the motion (in-place GigaScreen
on top of moving content); period 3-5, irregular and hold segments there are
shown raw.
"""
import numpy as np

from python.dlss_v1 import CONST, P2, PASS
from python.dlss_v3 import DeflickerV3


class DeflickerV4(DeflickerV3):
    def __init__(self, shape, mixer, tile_memory=6, **kw):
        self.tile_memory = tile_memory
        super().__init__(shape, mixer, **kw)
        self.tile_left = np.zeros((self.th, self.tw), np.uint8)

    def _classify(self):
        cls, period = super()._classify()
        moving = getattr(self, "moving_tiles", np.zeros((self.th, self.tw), bool))
        self.tile_left = np.where(moving, self.tile_memory, np.maximum(self.tile_left.astype(np.int16) - 1, 0)).astype(np.uint8)
        t = self.tile
        recent = np.zeros((self.h, self.w // 8), bool)
        recent[:self.th * t, :self.tw * t // 8] = np.repeat(np.repeat(self.tile_left > 0, t, 0), t // 8, 1)
        demote = recent & (cls != CONST) & (cls != P2) & (cls != PASS)
        cls = cls.copy()
        cls[demote] = PASS
        self.state = np.where(demote, PASS, self.state).astype(np.uint8)
        self.hold_left = np.where(demote, 0, self.hold_left).astype(np.uint8)
        return cls, period
