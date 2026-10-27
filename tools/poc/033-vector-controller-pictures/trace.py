"""Measure a pad's outline on a reference image and print it as cubic Bezier segments (millimetres).

The reference (an orthographic top-view photo or drawing) is never committed and never embedded: this script only
extracts its silhouette, keeps one half (the left one, mirrored, so the outline is symmetric), scales it to the
device's measured width and fits cubic Beziers within a tolerance. The printed list goes into the device module as
its SHELL (the right half, from the top centre clockwise to the bottom centre, the format of svgkit.mirrored_d).

    python3 trace.py <image> --width 162 [--dark | --light] [--crop x0,y0,x1,y1] [--top-clamp 4.2]
                     [--y0 -50] [--tol 0.3] [--overlay out.png] [--shell shell.py]

--dark: the pad is darker than the background (a black pad on white); --light: lighter (a grey pad on a dark
background); default: anything not white (a drawing rendered on white). --top-clamp cuts what rises above the case
top line (the shoulder buttons seen from above) at that many mm below the topmost point. Needs numpy and OpenCV.
"""

import argparse
import math

import cv2
import numpy as np


def silhouette(path, mode, crop, threshold, upscale=1):
    im = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if im.ndim == 2:
        im = cv2.cvtColor(im, cv2.COLOR_GRAY2BGR)
    if crop:
        x0, y0, x1, y1 = crop
        im = im[y0:y1, x0:x1]
    if upscale > 1:  # a small product shot: interpolate before the threshold for a sub-pixel edge
        im = cv2.resize(im, None, fx=upscale, fy=upscale, interpolation=cv2.INTER_CUBIC)
    if mode == "alpha":
        g = im[:, :, 3]
        mode = "light"
    else:
        g = cv2.cvtColor(im[:, :, :3], cv2.COLOR_BGR2GRAY)
    if im.shape[2] == 4:
        alpha = im[:, :, 3:4].astype(float) / 255
        im = (im[:, :, :3] * alpha + 255 * (1 - alpha)).astype(np.uint8)
    if mode == "dark":
        m = g < threshold
    elif mode == "light":
        m = g > threshold
    else:
        m = g < 245
    m = m.astype(np.uint8) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
    h, w = m.shape
    flood = cv2.copyMakeBorder(m, 1, 1, 1, 1, cv2.BORDER_CONSTANT, value=0)  # the outside joined all round
    cv2.floodFill(flood, np.zeros((h + 4, w + 4), np.uint8), (0, 0), 255)
    m |= cv2.bitwise_not(flood[1:-1, 1:-1])  # fill the holes (the buttons' outlines)
    contours, _ = cv2.findContours(m, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    pad = max(contours, key=cv2.contourArea)
    m = np.zeros_like(m)
    cv2.drawContours(m, [pad], -1, 255, cv2.FILLED)  # the pad alone (no labels, cables or shadows next to it)
    return im, m, pad[:, 0, :].astype(float)


def symmetry_axis(mask):
    """The vertical line the silhouette is most symmetric about (a product shot is often a little off centre or
    turned): the axis with the best overlap of the mask and its mirror image, to half a pixel"""
    cols = np.where(mask.any(0))[0]
    x0, x1 = cols.min(), cols.max()
    best, axis = -1.0, (x0 + x1) / 2
    m = mask > 0
    for twice in range(int(x0 + x1 - (x1 - x0) * 0.1), int(x0 + x1 + (x1 - x0) * 0.1) + 1):
        # the mirror about x = twice / 2 maps column c to twice - c
        c = np.arange(m.shape[1])
        src = twice - c
        ok = (src >= 0) & (src < m.shape[1])
        mirrored = np.zeros_like(m)
        mirrored[:, ok] = m[:, src[ok]]
        iou = (m & mirrored).sum() / max(1, (m | mirrored).sum())
        if iou > best:
            best, axis = iou, twice / 2
    return axis, best


def left_half(points, width_mm, y0_mm, top_clamp, cx, shape):
    """The contour in mm (x from the symmetry axis @p cx, y from y0 at the top), its left half from the top centre
    anticlockwise to the bottom centre, mirrored to the right; scaled so the mirrored outline is @p width_mm wide.
    Points on the image's edge below the top are left out: where a product shot cuts the pad off, the outline there
    is the frame's, not the pad's (trace() bridges the gap with a round cap)."""
    xs, ys = points[:, 0], points[:, 1]
    scale = width_mm / (2 * (cx - xs.min()))
    h, w = shape
    framed = (xs <= 0) | (ys <= 0) | (xs >= w - 1) | (ys >= h - 1)
    mm = np.stack(((xs - cx) * scale, (ys - ys.min()) * scale + y0_mm), axis=1)
    if top_clamp is not None:
        mm[:, 1] = np.maximum(mm[:, 1], y0_mm + top_clamp)
    framed &= mm[:, 1] > y0_mm + (top_clamp or 0) + 1
    # walk the contour from the point nearest the top centre, the way that goes into x < 0
    near = np.abs(mm[:, 0]) < 1.0
    start = int(np.flatnonzero(near)[np.argmin(mm[near, 1])])
    order, flags = np.roll(mm, -start, axis=0), np.roll(framed, -start)
    if order[min(40, len(order) - 1), 0] > order[0, 0]:
        order, flags = np.roll(order[::-1], 1, axis=0), np.roll(flags[::-1], 1)
    half = [order[0]]
    away = False
    for p, framed_point in zip(order[1:], flags[1:]):
        away |= p[0] < -5
        if away and p[0] > 0:  # crossed the centre line at the bottom
            break
        if not framed_point:
            half.append(p)
    half = np.array(half)
    half[0, 0] = 0
    half[-1, 0] = 0
    half[:, 0] = -half[:, 0]
    # one point per 0.25 mm
    keep = [half[0]]
    for p in half[1:]:
        if np.hypot(*(p - keep[-1])) >= 0.25:
            keep.append(p)
    keep[-1] = half[-1]
    return np.array(keep), scale


# Schneider's curve fitting (Graphics Gems I, "An algorithm for automatically fitting digitized curves")

def bezier(ctrl, t):
    mt = 1 - t
    return (mt ** 3)[:, None] * ctrl[0] + (3 * mt * mt * t)[:, None] * ctrl[1] + \
        (3 * mt * t * t)[:, None] * ctrl[2] + (t ** 3)[:, None] * ctrl[3]


def fit_one(pts, u, t1, t2):
    a1 = np.outer(3 * (1 - u) ** 2 * u, t1)
    a2 = np.outer(3 * (1 - u) * u ** 2, t2)
    c00, c01, c11 = (a1 * a1).sum(), (a1 * a2).sum(), (a2 * a2).sum()
    base = bezier(np.array([pts[0], pts[0], pts[-1], pts[-1]]), u)
    tmp = pts - base
    x0, x1 = (a1 * tmp).sum(), (a2 * tmp).sum()
    det = c00 * c11 - c01 * c01
    seg = np.hypot(*(pts[-1] - pts[0]))
    if abs(det) < 1e-12:
        a, b = seg / 3, seg / 3
    else:
        a, b = (x0 * c11 - x1 * c01) / det, (c00 * x1 - c01 * x0) / det
        if a < seg * 1e-3 or b < seg * 1e-3:
            a, b = seg / 3, seg / 3
    return np.array([pts[0], pts[0] + t1 * a, pts[-1] + t2 * b, pts[-1]])


def reparam(ctrl, pts, u):
    d1 = 3 * np.array([ctrl[1] - ctrl[0], ctrl[2] - ctrl[1], ctrl[3] - ctrl[2]])
    d2 = 2 * np.array([d1[1] - d1[0], d1[2] - d1[1]])
    q = bezier(ctrl, u) - pts
    mt = 1 - u
    q1 = (mt * mt)[:, None] * d1[0] + (2 * mt * u)[:, None] * d1[1] + (u * u)[:, None] * d1[2]
    q2 = mt[:, None] * d2[0] + u[:, None] * d2[1]
    num_ = (q * q1).sum(1)
    den = (q1 * q1).sum(1) + (q * q2).sum(1)
    out = u - np.where(np.abs(den) > 1e-12, num_ / np.where(np.abs(den) > 1e-12, den, 1), 0)
    return np.clip(out, 0, 1)


def chord(pts):
    d = np.concatenate(([0], np.cumsum(np.hypot(*np.diff(pts, axis=0).T))))
    return d / d[-1]


def unit(v):
    n = np.hypot(*v)
    return v / n if n else v


def fit(pts, t1, t2, tol, depth=0):
    if len(pts) == 2:
        seg = np.hypot(*(pts[1] - pts[0])) / 3
        return [np.array([pts[0], pts[0] + t1 * seg, pts[1] + t2 * seg, pts[1]])]
    u = chord(pts)
    ctrl = fit_one(pts, u, t1, t2)
    for _ in range(8):
        err = np.hypot(*(bezier(ctrl, u) - pts).T)
        if err.max() <= tol:
            return [ctrl]
        u = reparam(ctrl, pts, u)
        ctrl = fit_one(pts, u, t1, t2)
    err = np.hypot(*(bezier(ctrl, u) - pts).T)
    if err.max() <= tol or depth > 12:
        return [ctrl]
    split = int(np.clip(np.argmax(err), 2, len(pts) - 3))
    tc = unit(pts[split - 2] - pts[split + 2])
    return fit(pts[:split + 1], t1, tc, tol, depth + 1) + fit(pts[split:], -tc, t2, tol, depth + 1)


def smooth(pts, n=3):
    """A light moving average (the contour is a pixel staircase); the end points stay"""
    out = pts.copy()
    for i in range(1, len(pts) - 1):
        a, b = max(0, i - n), min(len(pts), i + n + 1)
        out[i] = pts[a:b].mean(0)
    return out


def corners(pts, angle=40):
    """Indices where the outline turns by more than @p angle degrees over about 1 mm"""
    out = []
    for i in range(4, len(pts) - 4):
        a = math.atan2(*(pts[i] - pts[i - 4])[::-1])
        b = math.atan2(*(pts[i + 4] - pts[i])[::-1])
        turn = abs((math.degrees(b - a) + 180) % 360 - 180)
        if turn > angle and (not out or i - out[-1] > 6):
            out.append(i)
    return out


def trace_run(pts, tol, first, last):
    pts = smooth(pts)
    cuts = [0] + corners(pts) + [len(pts) - 1]
    segs = []
    for a, b in zip(cuts, cuts[1:]):
        run = pts[a:b + 1]
        if len(run) < 2:
            continue
        t1 = np.array([1.0, 0]) if a == 0 and first else unit(run[min(3, len(run) - 1)] - run[0])
        t2 = np.array([-1.0, 0]) if b == len(pts) - 1 and last else unit(run[max(-4, -len(run))] - run[-1])
        segs += fit(run, t1, t2, tol)
    return segs


def trace(pts, tol, gap=1.5, cap=0.15):
    """Bezier segments along @p pts; where points are missing (the frame cut the pad off) the two ends are joined
    by a cap leaving each end along its own direction, its handles @p cap of the gap long (0.15: nearly flat, for a
    frame that cuts off only the last few tenths of a millimetre; check the height against the maker's)"""
    breaks = [i + 1 for i in range(len(pts) - 1) if np.hypot(*(pts[i + 1] - pts[i])) > gap]
    runs = np.split(pts, breaks)
    segs = []
    for k, run in enumerate(runs):
        if k:
            a, b = segs[-1][3], run[0]
            ta = unit(segs[-1][3] - segs[-1][2])
            tb = unit(run[min(3, len(run) - 1)] - run[0])
            chord = np.hypot(*(b - a))
            segs.append(np.array([a, a + ta * chord * cap, b - tb * chord * cap, b]))
        segs += trace_run(run, tol, k == 0, k == len(runs) - 1)
    return segs


def tidy(segs, shortest=1.0):
    """Fold segments shorter than @p shortest mm (the fitter's leftovers at corners and at the centre line) into
    the one before: its end and its second handle move by the same offset"""
    start = segs[0][0].copy()
    while len(segs) > 1 and np.hypot(*(segs[0][3] - segs[0][0])) < shortest:
        segs = segs[1:]
    out = [segs[0].copy()]
    out[0][1] += start - out[0][0]
    out[0][0] = start
    for s in segs[1:]:
        if np.hypot(*(s[3] - s[0])) < shortest:
            delta = s[3] - out[-1][3]
            out[-1][2] += delta
            out[-1][3] = s[3].copy()
        else:
            out.append(s.copy())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--width", type=float, required=True)
    ap.add_argument("--dark", action="store_true")
    ap.add_argument("--light", action="store_true")
    ap.add_argument("--alpha", action="store_true", help="the silhouette is the image's alpha channel")
    ap.add_argument("--upscale", type=int, default=1)
    ap.add_argument("--grid", help="the reference with a 5 mm grid in the device's coordinates")
    ap.add_argument("--threshold", type=int, default=90)
    ap.add_argument("--crop")
    ap.add_argument("--top-clamp", type=float)
    ap.add_argument("--y0", type=float, default=-50)
    ap.add_argument("--tol", type=float, default=0.3)
    ap.add_argument("--cap", type=float, default=0.15, help="handle length of a cap over a cut-off part, of the gap")
    ap.add_argument("--overlay")
    ap.add_argument("--shell")
    a = ap.parse_args()
    crop = tuple(int(v) for v in a.crop.split(",")) if a.crop else None
    mode = "alpha" if a.alpha else "dark" if a.dark else "light" if a.light else "drawing"
    im, mask, contour = silhouette(a.image, mode, crop, a.threshold, a.upscale)
    cx, iou = symmetry_axis(mask)
    half, scale = left_half(contour, a.width, a.y0, a.top_clamp, cx, mask.shape)
    segs = tidy(trace(half, a.tol, cap=a.cap))
    f = lambda p: f"({p[0]:.1f}, {p[1]:.1f})"  # noqa: E731
    lines = [f'[("M", {f(segs[0][0])}),']
    lines += [f'     ("C", {f(s[1])}, {f(s[2])}, {f(s[3])}),' for s in segs]
    text = "\n".join(lines).rstrip(",") + "]"
    fitted = np.concatenate([bezier(s, np.linspace(0, 1, 30)) for s in segs])
    print(f"# {len(segs)} segments, {1 / scale:.2f} px per mm, height {fitted[:, 1].max() - a.y0:.1f} mm, "
          f"mirror overlap {iou:.3f}")
    print(text)
    if a.shell:
        open(a.shell, "w").write(text + "\n")
    top = contour[:, 1].min()
    if a.grid:
        g = im.copy()
        to_px = lambda x, y: (int(round(cx + x / scale)), int(round(top + (y - a.y0) / scale)))  # noqa: E731
        lines = g.copy()
        for v in range(-100, 101, 5):
            colour = (0, 0, 255) if v == 0 else (255, 140, 0) if v % 10 == 0 else (255, 200, 120)
            cv2.line(lines, to_px(v, -150), to_px(v, 150), colour, 1)
            cv2.line(lines, to_px(-150, v), to_px(150, v), colour, 1)
        g = cv2.addWeighted(g, 0.35, lines, 0.65, 0)
        for v in range(-100, 101, 10):
            cv2.putText(g, str(v), to_px(v + 0.4, a.y0 + 2.5), 0, 0.45, (0, 0, 200), 1)
            cv2.putText(g, str(v), to_px(-a.width / 2 - 4, v - 0.4), 0, 0.45, (0, 0, 200), 1)
        cv2.imwrite(a.grid, g)
    if a.overlay:  # the fitted outline (both halves) over the reference at 50 %
        ink = im.copy()
        for sx in (1, -1):
            for s in segs:
                p = bezier(s, np.linspace(0, 1, 40))
                px = np.stack((cx + sx * p[:, 0] / scale, top + (p[:, 1] - a.y0) / scale), 1).astype(np.int32)
                cv2.polylines(ink, [px], False, (0, 0, 255), max(2, int(scale / 5)))
        cv2.imwrite(a.overlay, cv2.addWeighted(im, 0.5, ink, 0.5, 0))


if __name__ == "__main__":
    main()
