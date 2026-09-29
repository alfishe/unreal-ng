"""POC v10 - v9 (look-ahead) plus a field mode for two-page textures that move.

v9 finding (Across the Edge "irregular" spiral): the background is two
different lattices on the two screen pages (blue net with black holes /
cyan and blue dots), alternating every frame while the lattice scrolls and
slips phase. Per pixel it is no period at all; mixing pieces of it is worse
than mixing all of it. Scene-level pixel statistics do not single it out
(half the screen changes every frame in most scenes of the demo), but a
16x16 tile's COLOR HISTOGRAM alternates every frame whatever the lattice
does, and ordinary motion leaves the histogram nearly unchanged.

Field tile (per 16x16 tile, around t with the look-ahead):
  alternation  mean over frames t-3..t+3 of
               (|H(f) - H(f-1)|_1 - |H(f) - H(f-2)|_1) / (2 * area) >= on (0.15)
  unexplained  share of the tile's pixels that changed t-1 -> t and are not in
               the v9 mask >= 0.25 (a scene v9 already handles - the balls'
               sky - does not switch; a ball alone is too small a share)
  color set    the set of colors present (>= 4 px) alternates: same as t-2,
               different from t-1, in most frames of t-3..t+3 (scrolling raster
               bars change only the shares)
  size         a connected area of at least 12 tiles (a shaded ball is a
               two-page moving texture too, but a few tiles)
  hysteresis   stays on until the alternation drops below off (0.08)
Render in a field tile: v9 mask pixels as v9; the rest 1/4 t-1 + 1/2 t + 1/4 t+1
(three raw frames: no flicker, a little motion blur).
"""
import numpy as np

from python.dlss_v9 import DeflickerV9

FIELD = 7            # class code for the class map (HOLD color of v1)


class DeflickerV10(DeflickerV9):
    FT = 16

    def __init__(self, shape, mixer, lookahead=6, field_on=0.15, field_off=0.08, unexplained=0.25,
                 min_tiles=12, present=4, **kw):
        super().__init__(shape, mixer, lookahead=lookahead, **kw)
        self.field_on, self.field_off, self.unexplained = field_on, field_off, unexplained
        self.min_tiles, self.present = min_tiles, present
        self.th, self.tw = self.h // self.FT, self.w // self.FT
        self.hist = []                                     # tile histograms, newest first (16 colors)
        self.field = np.zeros((self.th, self.tw), bool)
        self.last_field = np.zeros((self.h, self.w), bool)

    def _tile_hist(self, plane):
        t = self.FT
        tiles = plane[:self.th * t, :self.tw * t].reshape(self.th, t, self.tw, t).transpose(0, 2, 1, 3).reshape(self.th, self.tw, t * t)
        h = np.zeros((self.th, self.tw, 16), np.float32)
        for c in range(16):
            h[..., c] = (tiles == c).sum(axis=2)
        return h

    @staticmethod
    def _large_components(mask, min_size):
        """Keep 4-connected components of at least min_size tiles."""
        h, w = mask.shape
        label = np.zeros((h, w), np.int32)
        keep = np.zeros((h, w), bool)
        cur = 0
        for y in range(h):
            for x in range(w):
                if mask[y, x] and not label[y, x]:
                    cur += 1
                    stack, comp = [(y, x)], []
                    label[y, x] = cur
                    while stack:
                        cy, cx = stack.pop()
                        comp.append((cy, cx))
                        for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not label[ny, nx]:
                                label[ny, nx] = cur
                                stack.append((ny, nx))
                    if len(comp) >= min_size:
                        for cy, cx in comp:
                            keep[cy, cx] = True
        return keep

    def _expand_ft(self, tiles):
        t = self.FT
        full = np.zeros((self.h, self.w), bool)
        full[:self.th * t, :self.tw * t] = np.repeat(np.repeat(tiles, t, 0), t, 1)
        return full

    def process(self, plane, attr, ink):
        self.hist.insert(0, self._tile_hist(plane))
        del self.hist[self.L + 6:]
        out, cls = super().process(plane, attr, ink)        # v9 on frame t = planes[L]
        field_px, L = self._detect_field(out)
        if field_px is None:
            return out, cls
        return self._render_field(out, cls, field_px, L)

    def _detect_field(self, out):
        """-> (field pixels outside the v9 mask, index L of frame t), or (None, L)."""
        n = len(self.planes)
        L = min(self.L, n - 1)
        if n < L + 3 or len(self.hist) < L + 3:
            return None, L

        # alternation of tile histograms over frames around t (indices L-3 .. L+3)
        area = float(self.FT * self.FT)
        alts = []
        for i in range(max(0, L - 3), min(len(self.hist) - 2, L + 4)):
            h0, h1, h2 = self.hist[i], self.hist[i + 1], self.hist[i + 2]
            alts.append((np.abs(h0 - h1).sum(axis=2) - np.abs(h0 - h2).sum(axis=2)) / (2 * area))
        alt = np.mean(alts, axis=0)

        # the SET of colors present alternates (page A {blue, black}, page B
        # {cyan, blue, black}); scrolling raster bars show every color in every
        # frame, so only the shares change (hip-hop scene: 15 % ghosts without it)
        sets = [self.hist[i] >= self.present for i in range(len(self.hist))]
        votes, total = 0, 0
        for i in range(max(0, L - 3), min(len(sets) - 2, L + 4)):
            same2 = np.all(sets[i] == sets[i + 2], axis=2)
            diff1 = np.any(sets[i] != sets[i + 1], axis=2)
            votes = votes + (same2 & diff1)
            total += 1
        set_alt = votes * 2 > total if total else np.zeros(alt.shape, bool)

        # share of the tile flickering but not explained by the v9 mask
        t_plane, p_plane = self.planes[L], self.planes[L + 1]
        mixed = np.any(out != self.mixer.mix(t_plane[None], np.ones((1,) + t_plane.shape)), axis=2)
        changed = t_plane != p_plane
        un = (changed & ~mixed)[:self.th * self.FT, :self.tw * self.FT]
        un = un.reshape(self.th, self.FT, self.tw, self.FT).mean(axis=(1, 3))

        cand = np.where(self.field, alt >= self.field_off, (alt >= self.field_on) & (un >= self.unexplained)) & set_alt
        # a field is a large connected area of tiles; a GigaScreen-shaded ball
        # is a two-page moving texture too, but only a few tiles
        self.field = self._large_components(cand, self.min_tiles)
        field_px = self._expand_ft(self.field) & ~mixed
        self.last_field = field_px
        return field_px, L

    def _render_field(self, out, cls, field_px, L):
        t_plane, p_plane = self.planes[L], self.planes[L + 1]
        if field_px.any() and L >= 1:
            planes = np.stack([self.planes[L - 1], t_plane, p_plane])      # t+1, t, t-1
            weights = np.stack([np.full(t_plane.shape, 0.25), np.full(t_plane.shape, 0.5), np.full(t_plane.shape, 0.25)])
            smooth = self.mixer.mix(planes, weights)
            out[field_px] = smooth[field_px]
            seg = field_px.reshape(self.h, self.w // 8, 8).any(axis=2)
            cls = cls.copy()
            cls[seg & (cls < 2)] = FIELD
        return out, cls
