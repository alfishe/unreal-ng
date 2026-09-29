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
"""
import numpy as np

BLOCK, RADIUS = 8, 8


def block_motion(prev, nxt, block=BLOCK, radius=RADIUS):
    """-> (dy, dx) per pixel: displacement of prev's content to nxt (nxt(p + v) = prev(p)),
    estimated per block of nxt... here per block of the frame midway (p grid)."""
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
    full_y = np.zeros((h, w), np.int32)
    full_x = np.zeros((h, w), np.int32)
    full_y[:bh * block, :bw * block] = np.repeat(np.repeat(vy, block, 0), block, 1)
    full_x[:bh * block, :bw * block] = np.repeat(np.repeat(vx, block, 0), block, 1)
    return full_y, full_x


def other_page(prev, nxt, block=BLOCK, radius=RADIUS):
    """-> (samples from prev, samples from nxt) at instant t (palette index planes)."""
    h, w = prev.shape
    vy, vx = block_motion(prev, nxt, block, radius)
    ay, ax = vy // 2, vx // 2
    by, bx = vy - ay, vx - ax
    yy, xx = np.indices((h, w))
    from_prev = prev[(yy - ay) % h, (xx - ax) % w]
    from_next = nxt[(yy + by) % h, (xx + bx) % w]
    return from_prev, from_next


def two_page_mix(mixer, cur, prev, nxt, block=BLOCK, radius=RADIUS):
    """1/2 t + 1/2 other page at t (1/4 + 1/4 from each side)."""
    fp, fn = other_page(prev, nxt, block, radius)
    planes = np.stack([cur, fp, fn])
    weights = np.stack([np.full(cur.shape, 0.5), np.full(cur.shape, 0.25), np.full(cur.shape, 0.25)])
    return mixer.mix(planes, weights)
