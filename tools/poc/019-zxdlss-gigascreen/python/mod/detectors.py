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
  steps               two-page texture moving in steps (the DJ scene's
                            circles): see StepsDetector.
"""
import numpy as np

from python.mod.context import TILE_FIELD
from python.mod.registry import Detector, Proposal, register
from python.twopage import _block_equal, block_shift_exact, other_page, other_page_steps, shifted_equal


class PeriodDetector(Detector):
    tags = ("static", "fine", "border")
    P = 2

    def __init__(self, confirm=6, confirm_periods=3, flat_veto=False, **params):
        super().__init__(**params)
        self.confirm, self.confirm_periods = confirm, confirm_periods
        self.flat_veto = flat_veto
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
            if self.flat_veto and any(_flat(ctx, b + j) for j in range(p)):
                # a whole-screen flash (one color everywhere) is no GigaScreen page: an
                # accelerating strobe matched period 5 for three periods (Across the Edge
                # 3:22), and a flash's cells matched a page of the next scene (period 2)
                continue
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


FLAT_SHARE = 0.99


def _flat(ctx, i):
    """Frame i shows one color on >= 99 % of the screen (a whole-screen flash)."""
    f = ctx.frames[i]
    return f.feature("flat", lambda: np.bincount(f.plane.ravel(), minlength=16).max() >= FLAT_SHARE * f.plane.size)


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



@register
class StepsDetector(Detector):
    """Two-page texture that moves in STEPS, both pages at once (the DJ scene's
    circle lattice: still for 2-4 frames, then a jump). The strict period
    detectors need 3 periods without a step and leave it patchy; the field
    detector's seeds need unexplained flicker and a color set alternating on
    every frame and do not fire.

    Per 8x8 block of frame t, a page flip confirmed over two periods with the
    whole block identical:
      backward  t == t-2, and t-1 == t-3 or t-1 == t+1  -> t-1 is the other
                page as it is at t
      forward   t == t+2, and t+1 == t+3 or t+1 == t-1  -> t+1 likewise
    Recipe: backward only -> 1/2 t + 1/2 t-1; forward only -> 1/2 t + 1/2 t+1;
    both -> 1/2 t + 1/4 t-1 + 1/4 t+1 (the period-2 recipe). A block with a
    single-page object moving through (a ball) or a one-frame flash is equal
    to no neighbor and stays raw. Only pixels whose neighbor differs in
    brightness mix (the period detectors' luma rule); pixels earlier stages
    explained stay theirs.

    tiles=True: additionally only inside connected groups of >= min_tiles
    16x16 paper tiles that alternate at >= min_alt of the centers t-2..t+2
    and step somewhere (experiment; the DJ's color-phase changes break the
    tile vote)."""
    name = "steps"
    tags = ("dynamic", "field")
    history = 4

    def __init__(self, tiles=False, lockstep=True, alt=0.9, min_alt=4, step=0.05, min_tiles=12, paper_only=True,
                 grow=False, static_other=False, consistent=False, **params):
        super().__init__(**params)
        self.tiles, self.lockstep, self.paper_only, self.grow = tiles, lockstep, paper_only, grow
        self.static_other, self.consistent = static_other, consistent
        self.alt, self.min_alt, self.step, self.min_tiles = alt, min_alt, step, min_tiles

    def _center(self, ctx, i):
        """(alternates, stepped) per tile for the frame at ring index i."""
        def compute():
            t = TILE_FIELD
            th, tw = ctx.h // t, ctx.w // t

            def tsum(m):
                return m[:th * t, :tw * t].reshape(th, t, tw, t).sum(axis=(1, 3))
            p0 = ctx.plane(i)
            pm1, pm2, pp1, pp2 = ctx.plane(i + 1), ctx.plane(i + 2), ctx.plane(i - 1), ctx.plane(i - 2)
            chg = (p0 != pm1) | (p0 != pp1)
            n = tsum(chg)
            back = tsum(chg & (p0 == pm2) & (p0 != pm1)) >= self.alt * n
            fwd = tsum(chg & (p0 == pp2) & (p0 != pp1)) >= self.alt * n
            return (n > 0.05 * t * t) & (back | fwd), tsum(p0 != pm2) >= self.step * t * t
        return ctx.frames[i].feature(("steps", self.alt, self.step), compute)

    def _eq(self, ctx, i, j):
        """8x8-block equality of ring frames i and j, per pixel (cached per pair)."""
        key = (ctx.frames[i].serial, ctx.frames[j].serial, "block_eq")
        if key not in ctx._pair_cache:
            ctx._pair_cache[key] = _block_equal(ctx.plane(i), ctx.plane(j))
        return ctx._pair_cache[key]

    def _lockstep(self, ctx, a_new, a_old, b_new, b_old):
        """Per pixel: page B moved from b_old to b_new exactly as page A moved
        from a_old to a_new (8x8 blocks, exact match; cached per frame quad)."""
        f = ctx.frames
        key = (f[a_new].serial, f[a_old].serial, "lockstep", f[b_new].serial, f[b_old].serial)
        if key not in ctx._pair_cache:
            vkey = (f[a_new].serial, f[a_old].serial, "shift")
            if vkey not in ctx._pair_cache:
                ctx._pair_cache[vkey] = block_shift_exact(ctx.plane(a_new), ctx.plane(a_old))
            vy, vx, found = ctx._pair_cache[vkey]
            ctx._pair_cache[key] = found & shifted_equal(ctx.plane(b_new), ctx.plane(b_old), vy, vx)
        return ctx._pair_cache[key]

    def run(self, ctx, state):
        L = ctx.t
        if L < 3 or ctx.n < L + 4:
            return []
        # ring index: L is t, L + k is t - k. Own page A at t, t-2, t+2; the other
        # page B at t-1, t+1 (t-3, t+3 as a second instance)
        back = self._eq(ctx, L, L + 2) & (self._eq(ctx, L + 1, L + 3) | self._eq(ctx, L + 1, L - 1))
        fwd = self._eq(ctx, L, L - 2) & (self._eq(ctx, L - 1, L - 3) | self._eq(ctx, L - 1, L + 1))
        if self.lockstep and L >= 2:
            # B seen once at its position (a color phase starts): it moved t-1 -> t+1
            # exactly as A moved over the step (A t-2 -> t, or t -> t+2)
            fwd |= self._eq(ctx, L, L - 2) & self._lockstep(ctx, L, L + 2, L - 1, L + 1)
            back |= self._eq(ctx, L, L + 2) & self._lockstep(ctx, L - 2, L, L - 1, L + 1)
        if self.static_other:
            # the other page did not move across t (t-1 == t+1, confirmed by t-3 or t+3):
            # it is the other page at t whatever frame t's own page did (a red lattice
            # jumping over a still black page)
            still = self._eq(ctx, L + 1, L - 1) & (self._eq(ctx, L + 1, L + 3) | self._eq(ctx, L - 1, L - 3))
            back |= still
            fwd |= still
        area = ~state["explained"]
        info = {}
        if self.tiles:
            if L < 4 or ctx.n < L + 5:
                return []
            cs = [self._center(ctx, i) for i in range(L - 2, L + 3)]
            alternating = np.sum([a for a, _ in cs], axis=0) >= self.min_alt
            tiles = alternating & np.any([s for _, s in cs], axis=0)
            if self.paper_only:
                paper = np.zeros_like(tiles)
                paper[3:15, 3:19] = True
                tiles &= paper
                alternating &= paper
            tiles = _large_components(tiles, self.min_tiles)
            if self.grow:
                # the whole alternating area connected to a stepping group, one tile around
                tiles = _dilate_tiles(_grow(tiles, alternating)) & (alternating | _dilate_tiles(tiles))
            area &= ctx.expand(tiles, TILE_FIELD).astype(bool)
            info["tiles"] = tiles
        lum = ctx.luma(L)
        shape = (ctx.h, ctx.w)
        if self.consistent:
            # one recipe for the whole frame: per-block choices of t-1 / t+1 / both
            # tiled the DJ's circles into pink, dark red and raw pieces when the
            # other page itself alternates (red, gray, red, black). The majority of
            # the flickering blocks decides; every block with a confirmed side takes it.
            flick = area & ((ctx.plane(L) != ctx.plane(L + 1)) | (ctx.plane(L) != ctx.plane(L - 1)))
            n_back = int((flick & back & ~fwd).sum())
            n_fwd = int((flick & fwd & ~back).sum())
            n_both = int((flick & back & fwd).sum())
            if max(n_back, n_fwd, n_both) == 0:
                return []
            valid = back | fwd
            if n_both >= max(n_back, n_fwd):
                use_prev = use_next = valid
            elif n_back >= n_fwd:
                use_prev, use_next = valid, np.zeros(shape, bool)
            else:
                use_prev, use_next = np.zeros(shape, bool), valid
            back, fwd = use_prev & (ctx.luma(L + 1) != lum), use_next & (ctx.luma(L - 1) != lum)
            both = use_prev & use_next
            mask = area & (back | fwd)
            w_prev = np.where(both, 0.25, np.where(use_prev, 0.5, 0.0))
            w_next = np.where(both, 0.25, np.where(use_next, 0.5, 0.0))
            w_prev, w_next = np.where(mask, w_prev, 0.0), np.where(mask, w_next, 0.0)
            info["recipe"] = "both" if n_both >= max(n_back, n_fwd) else ("back" if n_back >= n_fwd else "fwd")
        else:
            back &= ctx.luma(L + 1) != lum
            fwd &= ctx.luma(L - 1) != lum
            mask = area & (back | fwd)
            w_prev = np.where(back & fwd, 0.25, np.where(back, 0.5, 0.0))
            w_next = np.where(back & fwd, 0.25, np.where(fwd, 0.5, 0.0))
        if not mask.any():
            return []
        weights = {L + 1: np.where(mask, w_prev, 0.0), L - 1: np.where(mask, w_next, 0.0),
                   L: np.where(mask, 1.0 - w_prev - w_next, 0.0)}
        return [Proposal(self.name, mask, mask.astype(np.float32), weights,
                         rank=np.full(shape, 11, np.int16), info=info)]


@register
class SceneAverageDetector(Detector):
    """A large static picture in front of a flickering background that moves
    (Across the Edge's DJ: the hooded figure over a two-page circle lattice
    jumping in steps): the whole frame is averaged, 1/4 t-1 + 1/2 t + 1/4 t+1.
    Per-block recipes tiled the lattice into pieces of different colors; the
    plain average is what the eye sees (the figure is constant or strictly
    period 2 - the average is its period-2 mix; the lattice lags one frame).
    The spiral and the balls have no large static picture (their front object
    moves); the hip-hop scene's moving part is on the border.

    Over the frames t-3..t+3, 16x16 tiles:
      static  pixels constant or strictly period 2 over the 7 frames
      dyn     pixels differing from t-1 and t+1 that are not static
      object  largest 4-connected group of tiles >= 95 % static with >= 3
              colors: >= min_object of the frame's tiles (DJ >= 0.184, balls <= 0.157)
      moving  paper tiles with > 5 % dyn pixels: >= min_dynamic of the paper
    On while both hold; off after `hold` frames in a row without them."""
    name = "scene_avg"
    tags = ("scene",)
    history = 4

    def __init__(self, min_object=0.17, min_dynamic=0.4, hold=12, detail=0.0, render="avg3", **params):
        super().__init__(**params)
        self.min_object, self.min_dynamic, self.hold = min_object, min_dynamic, hold
        # detail > 0 (mod-tpgwaf): an object tile also shows horizontal color changes on
        # >= detail of its pixels in frame t - a drawn picture, not the balls' raster-bar sky
        self.detail = detail
        self.render = render                # "avg3" or "steps" (twopage.other_page_steps)
        self.on, self.off_frames = False, 0

    def skip(self):
        if self.on:
            self.off_frames += 1
            if self.off_frames >= self.hold:
                self.on, self.off_frames = False, 0

    def _features(self, ctx):
        L = ctx.t
        seq = np.stack([ctx.plane(j) for j in range(L - 3, L + 4)])
        const = np.all(seq == seq[0], axis=0)
        static = const | np.all(seq[:-2] == seq[2:], axis=0)
        t = seq[3]
        dyn = (t != ctx.plane(L + 1)) & (t != ctx.plane(L - 1)) & ~static
        T = TILE_FIELD
        th, tw = ctx.h // T, ctx.w // T

        def tiles(m):
            return m[:th * T, :tw * T].reshape(th, T, tw, T).mean(axis=(1, 3))
        cols = seq[:, :th * T, :tw * T].reshape(7, th, T, tw, T).transpose(1, 3, 0, 2, 4).reshape(th, tw, -1)
        present = np.zeros((th, tw, 16), bool)
        for c in range(16):
            present[..., c] = np.any(cols == c, axis=2)
        detailed = present.sum(axis=2) >= 3
        if self.detail > 0:
            hchg = np.zeros(t.shape, bool)
            hchg[:, :-1] = t[:, :-1] != t[:, 1:]
            detailed &= tiles(hchg) >= self.detail
        obj = _largest_component(detailed & (tiles(static) >= 0.95)) / float(th * tw)
        paper = np.zeros((th, tw), bool)
        paper[3:15, 3:19] = True
        moving = (tiles(dyn) > 0.05)[paper].mean()
        return obj, moving

    def run(self, ctx, state):
        L = ctx.t
        if L < 3 or ctx.n < L + 4:
            return []
        obj, moving = self._features(ctx)
        if obj >= self.min_object and moving >= self.min_dynamic:
            self.on, self.off_frames = True, 0
        else:
            self.skip()
        if not self.on:
            return []
        shape = (ctx.h, ctx.w)
        mask = np.ones(shape, bool)
        info = {"object": obj, "moving": moving}
        if self.render == "steps":
            # the other page at t from the neighbor across which t's own page did not
            # jump (fewer trails on lattices moving in steps than the plain average)
            fp, fn = other_page_steps(ctx.plane(L + 2), ctx.plane(L + 1), ctx.plane(L), ctx.plane(L - 1),
                                      ctx.plane(L - 2))
            weights = {L: np.full(shape, 0.5), "scene_prev": np.full(shape, 0.25), "scene_next": np.full(shape, 0.25)}
            return [Proposal(self.name, mask, mask.astype(np.float32), weights,
                             sources={"scene_prev": fp, "scene_next": fn},
                             rank=np.full(shape, 12, np.int16), info=info)]
        # source order t, t-1, t+1: the summation order of the mix (as the two-page mix)
        weights = {L: np.full(shape, 0.5), L + 1: np.full(shape, 0.25), L - 1: np.full(shape, 0.25)}
        return [Proposal(self.name, mask, mask.astype(np.float32), weights,
                         rank=np.full(shape, 12, np.int16), info=info)]


def _largest_component(mask):
    return max((len(c) for c in _components(mask)), default=0)


def _components(mask):
    h, w = mask.shape
    seen = np.zeros((h, w), bool)
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not seen[y, x]:
                seen[y, x] = True
                stack, comp = [(y, x)], []
                while stack:
                    cy, cx = stack.pop()
                    comp.append((cy, cx))
                    for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            stack.append((ny, nx))
                yield comp
