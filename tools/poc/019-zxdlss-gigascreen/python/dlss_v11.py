"""POC v11 - v10 whose field mode mixes only the field's own background.

v10 finding (irregular spiral): the field mode blended t-1, t, t+1 for every
unmasked pixel of a field tile. The snake moves between those frames, so
pieces of the lattice from the neighbor frames lay over the snake like
semi-transparent tiles, and pieces of the snake over the lattice next to it.

Field palette: the colors present in most field tiles (>= 60 %, over t-1, t,
t+1) - the lattice's blue, black, cyan; the snake's colors are only in the
tiles it crosses.
  pixel at t not in the palette -> object (the snake): raw t (or the v9 mask)
  pixel at t in the palette     -> mixed with the neighbor frames whose pixel
                                   is in the palette too: both 1/4 + 1/2 + 1/4,
                                   one 1/2 + 1/2, none -> raw t
"""
import numpy as np

from python.dlss_v10 import FIELD, DeflickerV10


class DeflickerV11(DeflickerV10):
    def __init__(self, shape, mixer, palette_share=0.6, **kw):
        super().__init__(shape, mixer, **kw)
        self.palette_share = palette_share

    def _render_field(self, out, cls, field_px, L):
        if not field_px.any() or L < 1:
            return out, cls
        t_plane, p_plane, n_plane = self.planes[L], self.planes[L + 1], self.planes[L - 1]
        # palette: colors present in >= palette_share of the field tiles in t-1, t or t+1
        present = (self.hist[L - 1] >= self.present) | (self.hist[L] >= self.present) | (self.hist[L + 1] >= self.present)
        palette = present[self.field].mean(axis=0) >= self.palette_share
        bg_t, bg_p, bg_n = palette[t_plane], palette[p_plane], palette[n_plane]

        both = bg_p & bg_n
        w_p = np.where(both, 0.25, np.where(bg_p, 0.5, 0.0))
        w_n = np.where(both, 0.25, np.where(bg_n, 0.5, 0.0))
        w_t = 1.0 - w_p - w_n
        use = field_px & bg_t & (bg_p | bg_n)
        if use.any():
            smooth = self.mixer.mix(np.stack([n_plane, t_plane, p_plane]), np.stack([w_n, w_t, w_p]))
            out[use] = smooth[use]
            seg = use.reshape(self.h, self.w // 8, 8).any(axis=2)
            cls = cls.copy()
            cls[seg & (cls < 2)] = FIELD
        self.last_field = use
        return out, cls
