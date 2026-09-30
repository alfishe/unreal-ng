"""POC v2 - v1 with a strict "irregular flicker" rule.

v1 finding (out/v1, 2026-09-28): the recurring-set rule (2..5 recurring values,
frequent changes) fired on pure motion - a rotating few-color shape recurs in a
10-frame window - and blended its frames into ghosts; output changed more than
raw on both raster negatives. v2:
  - RECUR only for near-period-2 alternation with slips: <= 3 distinct values,
    >= 70 % of steps satisfy h[i] == h[i+2], value changes on >= 50 % of steps.
  - HOLD only for segments that were strictly periodic (not RECUR).
"""
import numpy as np

from python.dlss_v1 import CONST, HOLD, P2, P5, PASS, RECUR, DeflickerV1


class DeflickerV2(DeflickerV1):
    def __init__(self, shape, mixer, window=10, max_period=5, recur_max_distinct=3,
                 recur_min_alternation=0.7, recur_min_change=0.5):
        self.recur_max_distinct = recur_max_distinct
        self.recur_min_alternation = recur_min_alternation
        self.recur_min_change = recur_min_change
        super().__init__(shape, mixer, window, max_period)

    def reset(self):
        self.__init__((self.h, self.w), self.mixer, self.window, self.max_period, self.recur_max_distinct,
                      self.recur_min_alternation, self.recur_min_change)

    def _classify(self):
        h = np.stack(self.sig_hist)
        k = h.shape[0]
        cls = np.full(h.shape[1:], PASS, np.uint8)
        period = np.zeros(h.shape[1:], np.uint8)

        undecided = np.ones(h.shape[1:], bool)
        const = np.all(h == h[0], axis=0)
        cls[const] = CONST
        undecided &= ~const
        for p in range(2, self.max_period + 1):
            if k < 2 * p:
                break
            ok = np.all(h[:k - p] == h[p:], axis=0) & undecided
            cls[ok] = P2 + (p - 2)
            period[ok] = p
            undecided &= ~ok

        if k >= 5:
            seen_before = np.any(h[1:] == h[0], axis=0)
            change = np.mean(h[:-1] != h[1:], axis=0)
            alternation = np.mean(h[:-2] == h[2:], axis=0)
            distinct = np.ones(h.shape[1:], np.int32)
            for i in range(1, k):
                distinct += np.all(h[:i] != h[i], axis=0)
            recur = (undecided & seen_before & (distinct >= 2) & (distinct <= self.recur_max_distinct)
                     & (alternation >= self.recur_min_alternation) & (change >= self.recur_min_change))
            cls[recur] = RECUR
            undecided &= ~recur

        was_strict = ((self.state >= P2) & (self.state <= P5)) | (self.state == HOLD)
        known = np.any(h[1:] == h[0], axis=0) if k > 1 else np.zeros(h.shape[1:], bool)
        hold = undecided & was_strict & known & (self.hold_left > 0)
        cls[hold] = HOLD
        period[hold] = self.state_period[hold]

        strict_budget = np.where((cls >= P2) & (cls <= P5), period, 0).astype(np.uint8)
        self.hold_left = np.where(cls == HOLD, self.hold_left - 1, strict_budget).astype(np.uint8)
        self.state = cls
        self.state_period = np.where(cls == HOLD, self.state_period, period).astype(np.uint8)
        return cls, self.state_period
