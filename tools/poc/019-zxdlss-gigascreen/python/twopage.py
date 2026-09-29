"""Two-page motion compensation, shared by the multi-reference oracle and the
field-mc detector.

In a two-page scene frames t-1 and t+1 show the SAME page, moved. Block
matching between them is reliable (the same picture, no page change), unlike
matching t against t-1. The other page at the instant t is then frame t-1
moved half the way forward and frame t+1 moved half the way back:

  v(block)   displacement t-1 -> t+1 of each 8x8 block (search +-R, the
             fewest mismatching pixels, ties to the smallest |v|)
  B^(t)(p)   from t-1 at p - v/2 and from t+1 at p + v/2 (v odd: floor on one
             side, ceil on the other); where the two samples agree, that
             color; where they disagree, both at half weight
  refine     (optional) per pixel, the vector of its block or of one of the 8
             neighbor blocks (or zero) whose samples t-1 / t+1 agree best in the
             3x3 window around the pixel: object boundaries follow pixels, not
             the 8x8 grid (a block half snake, half lattice gave the snake's
             edges an 8-pixel staircase)
"""
import numpy as np
from scipy import ndimage

BLOCK, RADIUS = 8, 8


def block_motion(prev, nxt, block=BLOCK, radius=RADIUS, per_block=False):
    """-> (dy, dx) per pixel (or per block with per_block=True): displacement of
    prev's content to nxt, per block of the midway grid (prev at p - v/2, nxt at p + v/2)."""
    h, w = prev.shape
    bh, bw = h // block, w // block
    best = np.full((bh, bw), np.inf)
    vy = np.zeros((bh, bw), np.int32)
    vx = np.zeros((bh, bw), np.int32)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            # symmetric search around the midway grid: prev at p - v/2, nxt at p + v/2
            ay, ax = dy // 2, dx // 2
            by, bx = dy - ay, dx - ax
            a = np.roll(prev, (ay, ax), axis=(0, 1))          # a(p) = prev(p - v_a)
            b = np.roll(nxt, (-by, -bx), axis=(0, 1))         # b(p) = nxt(p + v_b)
            c = (a != b)[:bh * block, :bw * block].reshape(bh, block, bw, block).sum(axis=(1, 3))
            c = c + 1e-3 * (abs(dy) + abs(dx))
            better = c < best
            best[better] = c[better]
            vy[better] = dy
            vx[better] = dx
    if per_block:
        return vy, vx
    full_y = np.zeros((h, w), np.int32)
    full_x = np.zeros((h, w), np.int32)
    full_y[:bh * block, :bw * block] = np.repeat(np.repeat(vy, block, 0), block, 1)
    full_x[:bh * block, :bw * block] = np.repeat(np.repeat(vx, block, 0), block, 1)
    return full_y, full_x


def _samples(prev, nxt, vy, vx):
    h, w = prev.shape
    ay, ax = vy // 2, vx // 2
    by, bx = vy - ay, vx - ax
    yy, xx = np.indices((h, w))
    return prev[(yy - ay) % h, (xx - ax) % w], nxt[(yy + by) % h, (xx + bx) % w]


def refine_vectors(prev, nxt, bvy, bvx, block=BLOCK, window=3):
    """Per-pixel choice among the vectors of the pixel's block, its 8 neighbor
    blocks and zero: the one with the fewest t-1 / t+1 mismatches in a
    window x window neighborhood (ties: own block first, zero last)."""
    h, w = prev.shape
    bh, bw = bvy.shape
    by_idx = np.minimum(np.arange(h) // block, bh - 1)
    bx_idx = np.minimum(np.arange(w) // block, bw - 1)
    best_cost = np.full((h, w), np.inf)
    vy = np.zeros((h, w), np.int32)
    vx = np.zeros((h, w), np.int32)
    cands = [(0, 0)] + [(dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy, dx) != (0, 0)]
    for k, (oy, ox) in enumerate(cands):
        ny = np.clip(by_idx + oy, 0, bh - 1)[:, None]
        nx = np.clip(bx_idx + ox, 0, bw - 1)[None, :]
        cy, cx = bvy[ny, nx], bvx[ny, nx]
        a, b = _samples(prev, nxt, cy, cx)
        cost = ndimage.uniform_filter((a != b).astype(np.float32), size=window, mode="nearest") + 1e-3 * k
        better = cost < best_cost
        best_cost[better] = cost[better]
        vy[better], vx[better] = cy[better], cx[better]
    zero = np.zeros((h, w), np.int32)
    a, b = _samples(prev, nxt, zero, zero)
    cost = ndimage.uniform_filter((a != b).astype(np.float32), size=window, mode="nearest") + 1e-2
    better = cost < best_cost
    vy[better], vx[better] = 0, 0
    return vy, vx


def other_page(prev, nxt, block=BLOCK, radius=RADIUS, refine=False):
    """-> (samples from prev, samples from nxt) at instant t (palette index planes)."""
    if refine:
        bvy, bvx = block_motion(prev, nxt, block, radius, per_block=True)
        vy, vx = refine_vectors(prev, nxt, bvy, bvx, block)
    else:
        vy, vx = block_motion(prev, nxt, block, radius)
    return _samples(prev, nxt, vy, vx)


def two_page_mix(mixer, cur, prev, nxt, block=BLOCK, radius=RADIUS):
    """1/2 t + 1/2 other page at t (1/4 + 1/4 from each side)."""
    fp, fn = other_page(prev, nxt, block, radius)
    planes = np.stack([cur, fp, fn])
    weights = np.stack([np.full(cur.shape, 0.5), np.full(cur.shape, 0.25), np.full(cur.shape, 0.25)])
    return mixer.mix(planes, weights)
