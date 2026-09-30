"""POC v6 - per-pixel mixing masks over the last 2..5 frames, nothing older.

v5 finding (Across the Edge, balls over the red/yellow GigaScreen stripe):
history rules over a 10-frame window plus an 8-frame sustain leave the
background a ball has just crossed raw for ~18 frames: a ball-shaped
silhouette of raw red/yellow flicker trails every ball. A long-lived per-pixel
background model (tried next) fixed the silhouette but went stale on dynamic
scenes and painted delayed ghosts. Decisions must come from the last few
frames only.

v6, per pixel, no state beyond the last 2 * max_period frames:
  key(p)   = key="pixel": color index | ink bit << 4
             key="cell" (default): paper area - the 8x1 segment's bitmap byte and
             the attribute the beam used (plane B), the same for its 8 pixels;
             border - the pixel's color.
             A red ball drawn in ink over a red/yellow stripe that is also ink
             has the same per-pixel key as the stripe's red phase, so the
             per-pixel key kept the mask on and mixed ball pieces with the
             stripe. The sprite changes the bitmap byte (or the attribute) of
             every segment it touches, so the cell key drops those segments.
             Cost: a segment at a sprite edge leaves the mask as a whole.
  period P = smallest P in 2..5 with key(t - j) == key(t - j - P) for j < P
             (the last 2P frames repeat with period P) and not constant
  output   = mix of key(t) .. key(t - P + 1); raw otherwise
So the background behind a sprite mixes again 2P frames (80 ms for GigaScreen)
after the sprite leaves, and a pixel stops mixing the first frame its pattern
breaks. The class map codes P2..P5 as v1.
`confirm` (analysis only) makes a period hold over more frames before the
mask turns on; `bias` < 1 weights the series toward the current frame.
SIMD-CANDIDATE: compares and selects over the key ring.
"""
import numpy as np

from python.dlss_v1 import P2, PASS


class DeflickerV6:
    PAPER_Y0, PAPER_X0 = 48, 48

    def __init__(self, shape, mixer, max_period=5, confirm=0, bias=1.0, key="cell", remember=False, remember_after=16,
                 lookahead=False, spatial=0):
        self.h, self.w = shape
        # spatial consensus for memory re-entry: allowed only next to pixels that
        # were mixed in the previous output (within +-spatial rows, +-8 pixels)
        self.spatial = spatial
        self.prev_mixed = np.zeros(shape, bool)
        # lookahead: process() gets frame t+1 and returns frame t (one frame of
        # present delay); a pixel pairs t with t-1 or with t+1, whichever is its
        # background's other phase. Period 2 only.
        self.lookahead = lookahead
        self.delay = 1 if lookahead else 0
        assert not lookahead or max_period == 2
        self.remember, self.remember_after = remember, remember_after
        self.alt_run = np.zeros(shape, np.int32)
        self.pair_a = np.zeros(shape, np.int32)
        self.pair_b = np.zeros(shape, np.int32)
        self.pair_valid = np.zeros(shape, bool)
        self.key_mode = key
        self.paper = np.zeros(shape, bool)
        self.paper[self.PAPER_Y0:self.PAPER_Y0 + 192, self.PAPER_X0:self.PAPER_X0 + 256] = True
        self.mixer = mixer
        self.max_period = max_period
        self.confirm = confirm      # analysed frames a period must hold (at least 2P); analysis only
        self.bias = bias            # weight of frame t-j ~ bias**j: < 1 favors the current frame
        self.depth = max(2 * max_period, confirm) + self.delay
        self.keys, self.planes = [], []

    def _key(self, plane, attr, ink):
        if self.key_mode == "pixel":
            return plane.astype(np.int32) | (ink.astype(np.int32) << 4)
        bits = ink.reshape(self.h, self.w // 8, 8).astype(np.int32)
        byte = np.zeros(bits.shape[:2], np.int32)
        for k in range(8):
            byte |= bits[:, :, k] << (7 - k)
        cell = (1 << 20) | (np.repeat(byte, 8, axis=1) << 8) | attr.astype(np.int32)
        return np.where(self.paper, cell, plane.astype(np.int32))

    def _alternates(self, first, span):
        """keys[first .. first+span-1] strictly alternate (period 2, not constant)."""
        k = self.keys
        ok = k[first] != k[first + 1]
        for j in range(first, first + span - 2):
            ok &= k[j] == k[j + 2]
        return ok

    def _process_lookahead(self):
        """Output for frame t = keys[1]; keys[0] is t+1, keys[2] is t-1."""
        k = self.keys
        n = len(k)
        t_plane = self.planes[1] if n >= 2 else self.planes[0]
        if n < 3:
            return self.mixer.mix(t_plane[None], np.ones((1,) + t_plane.shape)), np.zeros((self.h, self.w // 8), np.uint8)
        kn, kt, kp = k[0], k[1], k[2]
        span = max(4, self.confirm)
        use_prev = np.zeros(kt.shape, bool)
        use_next = np.zeros(kt.shape, bool)
        # a confirmed alternation through t (window ending at t, or at t+1)
        if n >= span + 1:
            use_prev |= self._alternates(1, span)
        if n >= span:
            use_prev |= self._alternates(0, span)
        if self.remember:
            alt = (kn == kp) & (kn != kt)
            self.alt_run = np.where(alt, self.alt_run + 1, 0)
            confirmed = self.alt_run >= self.remember_after
            self.pair_a[confirmed], self.pair_b[confirmed] = kt[confirmed], kn[confirmed]
            self.pair_valid |= confirmed
            a, b, v = self.pair_a, self.pair_b, self.pair_valid
            t_in = v & ((kt == a) | (kt == b))
            other = np.where(kt == a, b, a)
            back_prev = t_in & (kp == other)
            back_next = t_in & (kn == other)
            if self.spatial:
                near = self._dilate(self.prev_mixed, self.spatial, 8)
                back_prev &= near
                back_next &= near
            use_prev |= back_prev
            use_next |= back_next & ~use_prev
            # the pattern stopped (a pair value held on both sides of t): forget it
            self.pair_valid &= ~(t_in & (kp == kt) & (kn == kt))
        planes = np.stack([t_plane, np.where(use_next, self.planes[0], self.planes[2])])
        mixed = use_prev | use_next
        weights = np.stack([np.where(mixed, 0.5, 1.0), np.where(mixed, 0.5, 0.0)])
        out = self.mixer.mix(planes, weights)
        self.prev_mixed = mixed
        seg = mixed.reshape(self.h, self.w // 8, 8).any(axis=2)
        return out, np.where(seg, P2, PASS).astype(np.uint8)

    @staticmethod
    def _dilate(m, ry, rx):
        out = m.copy()
        for dy in range(-ry, ry + 1):
            out |= np.roll(m, dy, axis=0)
        row = out.copy()
        for dx in range(-rx, rx + 1):
            out |= np.roll(row, dx, axis=1)
        return out

    def process(self, plane, attr, ink):
        key = self._key(plane, attr, ink)
        for hist, v in ((self.keys, key), (self.planes, plane)):
            hist.insert(0, v)
            del hist[self.depth:]
        if self.lookahead:
            return self._process_lookahead()
        n = len(self.keys)
        period = np.zeros(plane.shape, np.uint8)
        const = np.ones(plane.shape, bool)
        for j in range(1, min(n, 4)):
            const &= self.keys[j] == key
        for p in range(2, self.max_period + 1):
            span = max(2 * p, self.confirm)
            if n < span:
                break
            ok = ~const & (period == 0)
            for j in range(span - p):
                ok &= self.keys[j] == self.keys[j + p]
            period[ok] = p

        if self.remember and n >= 2:
            # analysis memory: the last confirmed period-2 pair per pixel, kept while
            # a sprite covers the pixel. The mask returns as soon as the last two raw
            # frames show that pair again (A,B or B,A) - the second frame after the
            # sprite leaves instead of the confirm-th. Render still uses t and t-1 only.
            k0, k1 = self.keys[0], self.keys[1]
            alt = (k0 == self.keys[2]) & (k0 != k1) if n >= 3 else np.zeros(k0.shape, bool)
            self.alt_run = np.where(alt, self.alt_run + 1, 0)
            # only a long-lived alternation is background: a ball drawn into one
            # screen page at a time alternates with the background for a few
            # frames and must not overwrite the remembered background pair
            confirmed = self.alt_run >= self.remember_after
            self.pair_a[confirmed], self.pair_b[confirmed] = k0[confirmed], k1[confirmed]
            self.pair_valid |= confirmed
            in_pair0 = (k0 == self.pair_a) | (k0 == self.pair_b)
            back = self.pair_valid & in_pair0 & (k0 != k1) & ((k1 == self.pair_a) | (k1 == self.pair_b))
            # the pattern stopped (a pair value held twice): forget the pair
            self.pair_valid &= ~(in_pair0 & (k0 == k1))
            period[back & (period == 0) & ~const] = 2

        # render: the current frame and at most max_period - 1 before it, nothing older
        planes = np.stack(self.planes[:self.max_period])
        weights = np.zeros(planes.shape)
        weights[0][period == 0] = 1.0
        for p in range(2, min(self.max_period, len(planes)) + 1):
            m = period == p
            w = self.bias ** np.arange(p)
            for j in range(p):
                weights[j][m] = w[j] / w.sum()
        out = self.mixer.mix(planes, weights)

        # class map per 8-pixel segment: the mixing mask only (the most mixing
        # period in the segment); everything shown raw is black
        seg = period.reshape(self.h, self.w // 8, 8).max(axis=2)
        cls = np.where(seg > 0, P2 + seg.astype(np.int16) - 2, PASS).astype(np.uint8)
        return out, cls
