"""Detectors ported from the monolithic POC versions, with the tuned parameters.

  period2 .. period5  (v9)  strict period P over 3 periods (P2: 6 frames .. P5:
                            15) in a run containing t inside [t-15, t+L];
                            brightness must differ; for P >= 3 no translation
                            inside the run. Recipe: P raw frames of the run
                            containing t. Rank = P (the consensus prefers the
                            smallest period, as v7-v9 did).
  field               (v10) two-page texture that moves (the irregular spiral's
                            lattices): 16x16 tiles whose color histogram and
                            color SET alternate every frame around t, whose
                            flicker the pixel stage did not explain (>= 25 %),
                            in a connected area of >= 12 tiles; recipe
                            1/4 t+1 + 1/2 t + 1/4 t-1 on unexplained pixels.
                            palette=True (v11): only the field's own colors
                            mix, and only with neighbor frames that show them.
"""
import numpy as np

from python.mod.context import TILE_FIELD
from python.mod.registry import Detector, Proposal, register
from python.twopage import other_page


class PeriodDetector(Detector):
    tags = ("static", "fine", "border")
    P = 2

    def __init__(self, confirm=6, confirm_periods=3, **params):
        super().__init__(**params)
        self.confirm, self.confirm_periods = confirm, confirm_periods
        self.span = max(2 * self.P, confirm, confirm_periods * self.P)
        self.history = self.span

    def _run_periodic(self, ctx, a, span):
        p = self.P
        ok = np.ones((ctx.h, ctx.w), bool)
        for j in range(a, a + span - p):
            ok &= ctx.key(j) == ctx.key(j + p)
        const = np.ones((ctx.h, ctx.w), bool)
        for j in range(a + 1, a + p):
            const &= ctx.key(j) == ctx.key(a)
        return ok & ~const

    def run(self, ctx, state):
        p, span, n, L = self.P, self.span, ctx.n, ctx.t
        mask = np.zeros((ctx.h, ctx.w), bool)
        start = np.zeros((ctx.h, ctx.w), np.int16)
        vetoed_luma = np.zeros((ctx.h, ctx.w), bool)
        vetoed_motion = np.zeros((ctx.h, ctx.w), bool)
        # runs [a, a+span-1] containing t (index L); the most future run first
        for a in range(max(0, L - span + 1), min(L, n - span) + 1):
            run = self._run_periodic(ctx, a, span) & ~mask
            if not run.any():
                continue
            b = max(min(L, a + span - p), a, L - p + 1)       # series: P frames of the run containing t
            lum = [ctx.luma(b + j) for j in range(p)]
            luma_ok = np.any([lum[j] != lum[0] for j in range(1, p)], axis=0)
            vetoed_luma |= run & ~luma_ok
            run &= luma_ok
            if p > 2:
                moved = np.any([ctx.motion(i) for i in range(a, a + span - 1)], axis=0)
                vetoed_motion |= run & moved
                run &= ~moved
            mask |= run
            start[run] = b
        weights = {}
        for b in np.unique(start[mask]):
            mb = mask & (start == b)
            for j in range(p):
                weights.setdefault(int(b) + j, np.zeros((ctx.h, ctx.w)))
                weights[int(b) + j][mb] = 1.0 / p
        return [Proposal(self.name, mask, mask.astype(np.float32), weights,
                         rank=np.full((ctx.h, ctx.w), p, np.int16),
                         info={"vetoed_luma": vetoed_luma & ~mask, "vetoed_motion": vetoed_motion & ~mask})]


@register
class Period2(PeriodDetector):
    name, P = "period2", 2


@register
class Period3(PeriodDetector):
    name, P = "period3", 3


@register
class Period4(PeriodDetector):
    name, P = "period4", 4


@register
class Period5(PeriodDetector):
    name, P = "period5", 5


@register
class FieldDetector(Detector):
    name = "field"
    tags = ("dynamic", "field")
    history = 8

    def __init__(self, field_on=0.15, field_off=0.08, unexplained=0.25, min_tiles=12, present=4,
                 palette=False, palette_share=0.6, render="avg3", override=False, grow=False, whole=0.0,
                 grow_min=24, whole_off=0.1, whole_hold=12, refine=False, **params):
        super().__init__(**params)
        self.render, self.override, self.grow = render, override, grow
        self.whole = whole                  # grown field >= this share of the paper tiles -> the whole paper
        self.grow_min = grow_min            # only a seed of this many tiles grows (the balls' sky had
                                            # occasional 12-tile seeds; the spiral's are 40-60 tiles)
        # scene-level hysteresis of the whole-paper mode: on at `whole`, off only
        # after `whole_hold` frames below `whole_off` (the spiral's seeds shrink
        # for single frames; the mode then blinked between v10 and two-page)
        self.whole_off, self.whole_hold = whole_off, whole_hold
        self.refine = refine                # per-pixel motion vector refinement (twopage.py)
        self.whole_on, self.below = False, 0
        self.field_on, self.field_off, self.unexplained = field_on, field_off, unexplained
        self.min_tiles, self.present = min_tiles, present
        self.palette, self.palette_share = palette, palette_share
        self.field = None                   # tiles in field mode (hysteresis state)
        self.seeds = None                   # grow mode: seed tiles (hysteresis state)

    def skip(self):
        # not run: no tile alternates its color set, so no tile would stay a field
        if self.field is not None:
            self.field[:] = False
        if self.seeds is not None:
            self.seeds[:] = False
        if self.whole_on:
            self.below += 1
            if self.below >= self.whole_hold:
                self.whole_on, self.below = False, 0

    @staticmethod
    def tile_features(ctx, present=4):
        """Alternation of tile histograms and of tile color sets around t (v10);
        shared with the classifier."""
        L = ctx.t
        area = float(TILE_FIELD * TILE_FIELD)
        idx = range(max(0, L - 3), min(ctx.n - 2, L + 4))
        alts, votes, total = [], 0, 0
        for i in idx:
            h0, h1, h2 = ctx.tile_hist(i), ctx.tile_hist(i + 1), ctx.tile_hist(i + 2)
            alts.append((np.abs(h0 - h1).sum(axis=2) - np.abs(h0 - h2).sum(axis=2)) / (2 * area))
            s0, s1, s2 = h0 >= present, h1 >= present, h2 >= present
            votes = votes + (np.all(s0 == s2, axis=2) & np.any(s0 != s1, axis=2))
            total += 1
        alt = np.mean(alts, axis=0)
        set_alt = votes * 2 > total
        return alt, set_alt

    def run(self, ctx, state):
        L, n = ctx.t, ctx.n
        th, tw = ctx.h // TILE_FIELD, ctx.w // TILE_FIELD
        if self.field is None:
            self.field = np.zeros((th, tw), bool)
        if n < L + 3 or L < 1:
            return []
        alt, set_alt = self.tile_features(ctx, self.present)
        explained = state["explained"]
        t_plane, p_plane, n_plane = ctx.plane(L), ctx.plane(L + 1), ctx.plane(L - 1)
        un = ((t_plane != p_plane) & ~explained)[:th * TILE_FIELD, :tw * TILE_FIELD]
        un = un.reshape(th, TILE_FIELD, tw, TILE_FIELD).mean(axis=(1, 3))
        if self.grow:
            # seeds exactly as v10 (hysteresis on the seeds only - a grown or
            # whole-paper field kept as state sustained itself on every
            # alternating tile, e.g. the balls' sky); the field then takes every
            # connected tile that alternates, and one tile around it
            if self.seeds is None:
                self.seeds = np.zeros((th, tw), bool)
            cand = np.where(self.seeds, alt >= self.field_off, (alt >= self.field_on) & (un >= self.unexplained)) & set_alt
            self.seeds = _large_components(cand, self.min_tiles)
            # growth and the whole-paper switch stay inside the paper: the border's
            # scrolling raster bars (hip-hop) are one page moving, not two pages
            paper = np.zeros_like(self.seeds)
            paper[3:15, 3:19] = True            # 16x16 tiles of the 256x192 paper
            region = (alt >= self.field_on) & set_alt & paper
            grown = _grow(_large_components(self.seeds & paper, self.grow_min), region)
            self.field = self.seeds | (_dilate_tiles(grown) & paper)
            if self.whole > 0:
                share = self.field[paper].mean()
                if share >= self.whole:
                    self.whole_on, self.below = True, 0
                elif self.whole_on:
                    self.below = self.below + 1 if share < self.whole_off else 0
                    if self.below >= self.whole_hold:
                        self.whole_on, self.below = False, 0
                if self.whole_on:
                    self.field |= paper
        else:
            cand = np.where(self.field, alt >= self.field_off, (alt >= self.field_on) & (un >= self.unexplained)) & set_alt
            self.field = _large_components(cand, self.min_tiles)
        px = ctx.expand(self.field, TILE_FIELD).astype(bool)
        if not self.override:
            px &= ~explained
        if not px.any():
            return []
        shape = (ctx.h, ctx.w)
        if self.render == "twopage":
            # the other page at t from t-1 and t+1 (the same page, moved; twopage.py)
            fp, fn = other_page(p_plane, n_plane, refine=self.refine)
            half = np.where(px, 0.5, 0.0)
            quarter = np.where(px, 0.25, 0.0)
            return [Proposal(self.name, px, px.astype(np.float32), {L: half, "page_prev": quarter, "page_next": quarter},
                             sources={"page_prev": fp, "page_next": fn},
                             rank=np.full(shape, 10, np.int16), info={"tiles": self.field.copy()})]

        shape = (ctx.h, ctx.w)
        if not self.palette:
            w_n, w_t, w_p = np.full(shape, 0.25), np.full(shape, 0.5), np.full(shape, 0.25)
            mask = px
        else:
            pres = (ctx.tile_hist(L - 1) >= self.present) | (ctx.tile_hist(L) >= self.present) | (ctx.tile_hist(L + 1) >= self.present)
            pal = pres[self.field].mean(axis=0) >= self.palette_share
            bg_t, bg_p, bg_n = pal[t_plane], pal[p_plane], pal[n_plane]
            both = bg_p & bg_n
            w_p = np.where(both, 0.25, np.where(bg_p, 0.5, 0.0))
            w_n = np.where(both, 0.25, np.where(bg_n, 0.5, 0.0))
            w_t = 1.0 - w_p - w_n
            mask = px & bg_t & (bg_p | bg_n)
        weights = {L - 1: np.where(mask, w_n, 0.0), L: np.where(mask, w_t, 0.0), L + 1: np.where(mask, w_p, 0.0)}
        return [Proposal(self.name, mask, mask.astype(np.float32), weights,
                         rank=np.full(shape, 10, np.int16), info={"tiles": self.field.copy()})]


def _grow(seeds, region):
    """Tiles of `region` 4-connected to a seed."""
    out = seeds.copy()
    while True:
        nb = out.copy()
        nb[1:] |= out[:-1]
        nb[:-1] |= out[1:]
        nb[:, 1:] |= out[:, :-1]
        nb[:, :-1] |= out[:, 1:]
        new = nb & region & ~out
        if not new.any():
            return out | seeds
        out |= new


def _dilate_tiles(m):
    d = m.copy()
    d[1:] |= m[:-1]
    d[:-1] |= m[1:]
    d[:, 1:] |= m[:, :-1]
    d[:, :-1] |= m[:, 1:]
    return d


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
