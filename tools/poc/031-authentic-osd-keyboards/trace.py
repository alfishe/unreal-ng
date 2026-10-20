"""Trace a printed mark from a reference photo into SVG path data in mm: rectify the plate it is printed on (its four
corners found as the largest light quad in a region, or given), split the print into its inks by hue, trace each ink's
outline (holes included) and fit smooth curves through it.

    trace.py PHOTO X Y W H WIDTH_MM HEIGHT_MM OUT.json [--inks NAME:H0-H1 ...] [--check OUT.png]

X Y W H is the region holding the plate in the photo's pixels, WIDTH_MM x HEIGHT_MM the plate's size. Each ink is a
hue range in degrees (0..360) of the saturated pixels; OUT.json maps each ink to {"d": path data, "colour": the
median colour}, the path in mm from the plate's top left. --check writes the rectified photo beside the traced paths
rendered back (rsvg) for comparing by eye, and prints the share of print pixels on which they differ.
"""

import argparse
import json
import subprocess
import sys

import cv2
import numpy as np

SCALE = 40  # px per mm of the rectified plate


def plate_corners(img, x, y, w, h):
    """The plate's corners: the largest light region's outline reduced to four points, top left first, clockwise"""
    roi = img[y:y + h, x:x + w]
    light = (cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)[..., 2] > 120).astype(np.uint8) * 255
    light = cv2.morphologyEx(light, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
    contours, _ = cv2.findContours(light, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    outline = max(contours, key=cv2.contourArea)
    quad = cv2.approxPolyDP(outline, 0.02 * cv2.arcLength(outline, True), True).reshape(-1, 2).astype(np.float32)
    if len(quad) != 4:
        sys.exit(f"the plate's outline has {len(quad)} corners, not 4")
    quad += (x, y)
    s, d = quad.sum(1), quad[:, 0] - quad[:, 1]
    return np.array([quad[s.argmin()], quad[d.argmax()], quad[s.argmax()], quad[d.argmin()]])


def rectify(img, corners, width_mm, height_mm):
    w, h = round(width_mm * SCALE), round(height_mm * SCALE)
    target = np.float32([(0, 0), (w, 0), (w, h), (0, h)])
    return cv2.warpPerspective(img, cv2.getPerspectiveTransform(corners, target), (w, h), flags=cv2.INTER_CUBIC)


def ink_mask(plate, h0, h1):
    """The pixels of one ink: saturated, their hue in [h0, h1) degrees, smoothed so the outline follows the print's
    edge, not the photo's noise"""
    hsv = cv2.cvtColor(plate, cv2.COLOR_BGR2HSV_FULL).astype(np.int32)
    hue = hsv[..., 0] * 360 // 256
    inside = (hue >= h0) & (hue < h1) if h0 < h1 else (hue >= h0) | (hue < h1)
    mask = (inside & (hsv[..., 1] > 60)).astype(np.uint8) * 255
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))
    mask = cv2.GaussianBlur(mask, (0, 0), SCALE * 0.2)
    return (mask > 127).astype(np.uint8) * 255


def smooth_path(points):
    """A closed curve through @p points (mm): Catmull-Rom turned into cubic Béziers"""
    n = len(points)
    out = [f"M{points[0][0]:.3f},{points[0][1]:.3f}"]
    for i in range(n):
        p0, p1, p2, p3 = points[i - 1], points[i], points[(i + 1) % n], points[(i + 2) % n]
        c1 = p1 + (p2 - p0) / 6
        c2 = p2 - (p3 - p1) / 6
        out.append(f"C{c1[0]:.3f},{c1[1]:.3f} {c2[0]:.3f},{c2[1]:.3f} {p2[0]:.3f},{p2[1]:.3f}")
    return "".join(out) + "Z"


def resample(points, step=SCALE * 0.25, window=SCALE * 0.15):
    """A closed outline evened out: its pixel steps averaged over @p window, then points every @p step along it (even
    spacing keeps the fitted curve from overshooting at the corners)"""
    n = len(points)
    k = max(1, int(window))
    padded = np.vstack([points[-k:], points, points[:k]])
    kernel = np.ones(2 * k + 1) / (2 * k + 1)
    points = np.column_stack([np.convolve(padded[:, i], kernel, "valid") for i in (0, 1)])
    closed = np.vstack([points, points[:1]])
    run = np.concatenate([[0], np.cumsum(np.hypot(*np.diff(closed, axis=0).T))])
    count = max(3, int(run[-1] / step))
    at = np.linspace(0, run[-1], count, endpoint=False)
    return np.column_stack([np.interp(at, run, closed[:, i]) for i in (0, 1)])


def trace(mask):
    """The mask's outlines (outer and holes) as one path in mm, for fill-rule evenodd"""
    contours, _ = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    paths = []
    for c in contours:
        if cv2.contourArea(c) < (0.3 * SCALE) ** 2:
            continue
        simple = resample(c.reshape(-1, 2).astype(float)) / SCALE
        if len(simple) >= 3:
            paths.append(smooth_path(simple))
    return "".join(paths)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photo")
    ap.add_argument("region", nargs=4, type=int)
    ap.add_argument("size", nargs=2, type=float)
    ap.add_argument("out")
    ap.add_argument("--inks", nargs="+", required=True, help="NAME:H0-H1 hue ranges in degrees")
    ap.add_argument("--check")
    a = ap.parse_args()
    img = cv2.imread(a.photo)
    corners = plate_corners(img, *a.region)
    plate = rectify(img, corners, *a.size)
    result, masks = {}, []
    for ink in a.inks:
        name, hues = ink.split(":")
        h0, h1 = (int(v) for v in hues.split("-"))
        mask = ink_mask(plate, h0, h1)
        colour = np.median(plate[mask > 0], axis=0)[::-1] if mask.any() else (0, 0, 0)
        result[name] = {"d": trace(mask), "colour": "#%02x%02x%02x" % tuple(int(v) for v in colour)}
        masks.append(mask)
    with open(a.out, "w") as f:
        json.dump({"corners": corners.round(1).tolist(), "size": a.size, "inks": result}, f, indent=1)
    if a.check:
        w, h = plate.shape[1], plate.shape[0]
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {a.size[0]} '
               f'{a.size[1]}"><rect width="100%" height="100%" fill="#fff"/>' +
               "".join(f'<path d="{v["d"]}" fill="#000" fill-rule="evenodd"/>' for v in result.values()) + "</svg>")
        svg_path = a.check + ".svg"
        open(svg_path, "w").write(svg)
        subprocess.run(["rsvg-convert", svg_path, "-o", a.check + ".trace.png"], check=True)
        traced = cv2.imread(a.check + ".trace.png", cv2.IMREAD_GRAYSCALE) < 128
        printed = np.any(masks, axis=0)
        diff = traced ^ printed
        print(f"corners {corners.round(1).tolist()}; traced vs print: {diff.sum() / printed.sum() * 100:.2f} % "
              f"of the print's pixels differ")
        overlay = plate.copy()
        overlay[traced & ~printed] = (0, 0, 255)
        overlay[printed & ~traced] = (255, 0, 0)
        cv2.imwrite(a.check, np.vstack([plate, cv2.cvtColor((~traced * 255).astype(np.uint8), cv2.COLOR_GRAY2BGR),
                                        overlay]))


if __name__ == "__main__":
    main()
