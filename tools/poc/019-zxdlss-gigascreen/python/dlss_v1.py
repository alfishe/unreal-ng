"""ZX DLSS GigaScreen - POC v1: causal per-segment period detection + per-pixel mixing.

Implements design-analysis.md §5-§6, §9-§10 on palette-index planes (no plane B
yet), treating the whole frame - picture area and border - as 8x1 segments.

Classes per segment (codes used in the class map):
  0 PASS      unexplained change or never-seen value -> raw pixels
  1 CONST     nothing changed in the window           -> raw pixels
  2 P2..5     strict period P (seen repeating)        -> average of the last P frames
  6 RECUR     2..5 recurring values, irregular order  -> average over the window
  7 HOLD      was periodic, now out of order but only known values -> keep last mix rule
Motion, regions, split mode: later versions.
"""
import numpy as np

PASS, CONST, P2, P3, P4, P5, RECUR, HOLD = range(8)
CLASS_NAMES = ["pass", "const", "p2", "p3", "p4", "p5", "recur", "hold"]
CLASS_COLORS = np.array([[0, 0, 0], [40, 40, 40], [0, 200, 255], [0, 255, 120], [255, 220, 0],
                         [255, 120, 0], [255, 0, 200], [120, 120, 255]], np.uint8)


def segment_signatures(plane):
    """H x W palette indices (< 16) -> H x (W/8) uint32 signatures (8 x 4 bits)."""
    h, w = plane.shape
    p = plane.reshape(h, w // 8, 8).astype(np.uint32) & 0xF
    sig = np.zeros((h, w // 8), np.uint32)
    for k in range(8):
        sig |= p[:, :, k] << (4 * k)
    return sig


class DeflickerV1:
    def __init__(self, shape, mixer, window=10, max_period=5, recur_min_changes=3):
        self.h, self.w = shape
        self.window, self.max_period = window, max_period
        self.recur_min_changes = recur_min_changes
        self.mixer = mixer
        self.sig_hist = []            # newest first, each H x W/8
        self.plane_hist = []          # newest first, each H x W
        self.state = np.full((self.h, self.w // 8), PASS, np.uint8)
        self.state_period = np.zeros((self.h, self.w // 8), np.uint8)
        self.hold_left = np.zeros((self.h, self.w // 8), np.uint8)

    def reset(self):
        self.__init__((self.h, self.w), self.mixer, self.window, self.max_period, self.recur_min_changes)

    def _classify(self):
        h = np.stack(self.sig_hist)                       # K x Hs x Ws, K <= window
        k = h.shape[0]
        cls = np.full(h.shape[1:], PASS, np.uint8)
        period = np.zeros(h.shape[1:], np.uint8)

        # strict period: smallest P with h[i] == h[i+P] for all i, needing >= 2P frames
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

        # recurring set: current value seen before, 2..5 distinct values, changes often
        if k >= 4:
            seen_before = np.any(h[1:] == h[0], axis=0)
            changes = np.sum(h[:-1] != h[1:], axis=0)
            distinct = np.ones(h.shape[1:], np.int32)
            for i in range(1, k):
                distinct += np.all(h[:i] != h[i], axis=0)
            recur = undecided & seen_before & (distinct >= 2) & (distinct <= 5) & (changes >= self.recur_min_changes)
            cls[recur] = RECUR
            undecided &= ~recur

        # hysteresis: a periodic segment whose current value is a known one keeps its rule
        was_periodic = (self.state >= P2) & (self.state <= P5) | (self.state == RECUR) | (self.state == HOLD)
        known = np.any(h[1:] == h[0], axis=0) if k > 1 else np.zeros(h.shape[1:], bool)
        hold = undecided & was_periodic & known & (self.hold_left > 0)
        cls[hold] = HOLD
        period[hold] = self.state_period[hold]

        # bookkeeping
        new_hold_budget = np.where((cls >= P2) & (cls <= P5), period, 0).astype(np.uint8)
        self.hold_left = np.where(cls == HOLD, self.hold_left - 1, np.maximum(new_hold_budget, np.where(cls == RECUR, 3, 0))).astype(np.uint8)
        self.state = cls
        self.state_period = np.where(cls == HOLD, self.state_period, period).astype(np.uint8)
        return cls, self.state_period

    def process(self, plane):
        """Feed one raw frame (palette indices); returns (output RGB, class map per segment)."""
        self.plane_hist.insert(0, plane)
        self.sig_hist.insert(0, segment_signatures(plane))
        del self.plane_hist[self.window:]
        del self.sig_hist[self.window:]

        cls, period = self._classify()

        # per-pixel weights over the history
        k = len(self.plane_hist)
        planes = np.stack(self.plane_hist)
        weights = np.zeros((k, self.h, self.w))
        cls_px = np.repeat(cls, 8, axis=1)
        per_px = np.repeat(period, 8, axis=1)
        raw = (cls_px == PASS) | (cls_px == CONST)
        weights[0][raw] = 1.0
        for p in range(2, self.max_period + 1):
            m = (~raw) & (cls_px != RECUR) & (per_px == p)
            for j in range(min(p, k)):
                weights[j][m] = 1.0 / min(p, k)
        rec = (cls_px == RECUR) | ((cls_px == HOLD) & (per_px == 0))
        weights[:, rec] = 1.0 / k
        return self.mixer.mix(planes, weights), cls
