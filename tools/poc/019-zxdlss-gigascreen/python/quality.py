"""Quality oracle: the processed frame may differ from the raw frame ONLY where
the raw picture flickers, and only by showing the flicker's mixed color.

The oracle is offline and two-sided (it sees W frames before AND after t), so
it knows whether a pixel really flickers at t - something a causal algorithm
can only estimate. Per pixel, on palette indices:
  motion         changed, and matches t-1 shifted by v AND t+1 shifted by -v
                 with one velocity v != 0 (32x32 tile search, |v| <= 8): moving
                 content, including grids whose pixels repeat with period 2
                 -> must be shown raw
  moving flicker not motion, but matches t-2 / t+2 (same phase) shifted by
                 -/+ 2v: flicker on moving content (C6/C8) -> needs motion-
                 compensated mixing; mixing it in place is an error
  periodic       strict period P in 2..5 over [t-W, t+W]  (C1/C2)
  irregular      <= 3 distinct values, >= 70 % of steps A,B,A, value changes
                 on >= 50 % of steps                       (C3, phase slips)
  steady         anything else -> must be shown raw
GigaScreen whose two phases are offset copies has no single velocity (the
shift flips every frame), so it is not "motion".

Per frame it reports:
  ghost      mixed although the oracle says steady or motion (smear, ghost,
             blur, geometric artefact)                  -> hard gate: 0
  ghost_mf   mixed in place on moving flicker (needs motion compensation)
  missed     oracle says flicker, output not mixed      -> lower is better
  color      OKLab distance output vs oracle mix on pixels both call flicker
  residue    output change t-1 -> t on pixels the oracle calls flicker
  exact      non-mixed pixels equal raw bit for bit (by construction; checked)
"""
import numpy as np

from python.mixers import linear_to_srgb, srgb_to_linear

W = 6


def _oklab(rgb):
    lin = srgb_to_linear(rgb.astype(np.float64))
    m1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
                   [0.2119034982, 0.6806995451, 0.1073969566],
                   [0.0883024619, 0.2817188376, 0.6299787005]])
    lms = np.cbrt(lin @ m1.T)
    m2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
                   [1.9779984951, -2.4285922050, 0.4505937099],
                   [0.0259040371, 0.7827717662, -0.8086757660]])
    return lms @ m2.T


class Oracle:
    def __init__(self, clip, palette_rgb):
        self.clip = clip
        self.lin = srgb_to_linear(palette_rgb.astype(np.float64))

    TILE, RADIUS = 32, 8

    def _tile_shift(self, cur, ref):
        """Best shift per 32x32 tile (ties to the smallest) -> per-pixel dy, dx maps."""
        t, r = self.TILE, self.RADIUS
        h, w = cur.shape
        th, tw = h // t, w // t
        best_cost = np.full((th, tw), np.inf)
        best = np.zeros((th, tw, 2), np.int32)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                c = (cur != np.roll(ref, (dy, dx), axis=(0, 1)))[:th * t, :tw * t].reshape(th, t, tw, t).sum(axis=(1, 3))
                c = c + 1e-3 * (abs(dy) + abs(dx))
                better = c < best_cost
                best_cost[better] = c[better]
                best[better] = (dy, dx)
        dy = np.zeros((h, w), np.int32)
        dx = np.zeros((h, w), np.int32)
        dy[:th * t, :tw * t] = np.repeat(np.repeat(best[..., 0], t, 0), t, 1)
        dx[:th * t, :tw * t] = np.repeat(np.repeat(best[..., 1], t, 0), t, 1)
        return dy, dx

    @staticmethod
    def _sample(plane, dy, dx):
        """plane[y - dy, x - dx] with per-pixel shifts (wrapping)."""
        h, w = plane.shape
        yy, xx = np.indices((h, w))
        return plane[(yy - dy) % h, (xx - dx) % w]

    def _motion(self, seq, step):
        """Pixels whose change t-step -> t -> t+step is one velocity (not zero)."""
        cur, prev, nxt = seq[W], seq[W - step], seq[W + step]
        dy, dx = self._tile_shift(cur, prev)
        moving = (dy != 0) | (dx != 0)
        back = cur == self._sample(prev, dy, dx)
        fwd = cur == self._sample(nxt, -dy, -dx)
        return moving & back & fwd & (cur != prev)

    def at(self, i):
        """-> (flicker, moving_flicker, oracle RGB) for clip frame i, or None at the clip edges."""
        if i - W < 0 or i + W >= len(self.clip):
            return None
        seq = np.stack([self.clip.plane(j) for j in range(i - W, i + W + 1)])   # (2W+1) x H x W, index W = t
        motion = self._motion(seq, 1)
        moving_flicker = self._motion(seq, 2) & ~motion & (seq[W] != seq[W - 1])
        n = seq.shape[0]
        const = np.all(seq == seq[0], axis=0)
        period = np.zeros(seq.shape[1:], np.uint8)
        for p in range(2, 6):
            ok = np.all(seq[:n - p] == seq[p:], axis=0) & ~const & (period == 0)
            period[ok] = p
        alt = np.mean(seq[:-2] == seq[2:], axis=0)
        chg = np.mean(seq[:-1] != seq[1:], axis=0)
        distinct = np.ones(seq.shape[1:], np.int32)
        for k in range(1, n):
            distinct += np.all(seq[:k] != seq[k], axis=0)
        irregular = (period == 0) & ~const & (distinct <= 3) & (alt >= 0.7) & (chg >= 0.5)
        flicker = ((period > 0) | irregular) & ~motion & ~moving_flicker

        # oracle mix: strict period -> the P frames ending at t; irregular -> whole window
        acc = np.zeros(seq.shape[1:] + (3,))
        for p in range(2, 6):
            m = period == p
            if m.any():
                for j in range(p):
                    acc[m] += self.lin[seq[W - j][m]] / p
        if irregular.any():
            for j in range(n):
                acc[irregular] += self.lin[seq[j][irregular]] / n
        return flicker, moving_flicker, np.round(linear_to_srgb(acc)).astype(np.uint8)


class QualityAccumulator:
    ERROR_COLORS = {"ghost": (255, 0, 0), "ghost_mf": (255, 128, 0), "missed": (255, 220, 0), "color": (255, 0, 255)}

    def __init__(self, color_threshold=0.05):
        self.color_threshold = color_threshold          # OKLab distance counted as a color error
        self.n = 0
        self.ghost = self.ghost_mf = self.missed = self.flicker = self.moving_flicker = self.mixed = self.residue = 0.0
        self.color_errors = []
        self.exact_violations = 0

    def add(self, raw, out, prev_out, oracle):
        """raw/out: H x W x 3; oracle: (flicker, oracle_rgb) or None. Returns an error map (H x W x 3)."""
        mixed = np.any(out != raw, axis=2)
        err = np.zeros_like(raw)
        if oracle is None:
            return err
        flicker, moving_flicker, want = oracle
        ghost = mixed & ~flicker & ~moving_flicker
        ghost_mf = mixed & moving_flicker
        missed = flicker & ~mixed
        both = mixed & flicker
        de = np.linalg.norm(_oklab(out[both]) - _oklab(want[both]), axis=1) if both.any() else np.zeros(0)
        self.n += 1
        self.ghost += ghost.mean()
        self.ghost_mf += ghost_mf.mean()
        self.moving_flicker += moving_flicker.mean()
        self.missed += missed.sum() / max(flicker.sum(), 1)
        self.flicker += flicker.mean()
        self.mixed += mixed.mean()
        if prev_out is not None and flicker.any():
            self.residue += np.any(out[flicker] != prev_out[flicker], axis=1).mean()
        self.color_errors.append(de)
        err[ghost] = self.ERROR_COLORS["ghost"]
        err[ghost_mf] = self.ERROR_COLORS["ghost_mf"]
        err[missed] = self.ERROR_COLORS["missed"]
        bad_color = np.zeros(mixed.shape, bool)
        bad_color[both] = de > self.color_threshold
        err[bad_color] = self.ERROR_COLORS["color"]
        return err

    def report(self):
        de = np.concatenate(self.color_errors) if self.color_errors else np.zeros(1)
        n = max(self.n, 1)
        return {
            "ghost_pixels": round(self.ghost / n, 5),                 # share of all pixels, hard gate 0
            "ghost_moving_flicker": round(self.ghost_mf / n, 5),      # mixed in place on moving flicker
            "missed_flicker": round(self.missed / n, 4),              # share of oracle-flicker pixels not mixed
            "oracle_moving_flicker_pixels": round(self.moving_flicker / n, 4),
            "oracle_flicker_pixels": round(self.flicker / n, 4),
            "mixed_pixels": round(self.mixed / n, 4),
            "flicker_residue": round(self.residue / n, 4),
            "color_dE_p50": round(float(np.percentile(de, 50)), 4) if de.size else 0.0,
            "color_dE_p99": round(float(np.percentile(de, 99)), 4) if de.size else 0.0,
            "color_error_share": round(float(np.mean(de > self.color_threshold)), 4) if de.size else 0.0,
        }
