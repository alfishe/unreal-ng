#!/usr/bin/env python3
"""Generate the UnrealNG (unreal-qt) application icon for all platforms.

Design from the new-gui branch: dark rounded tile, four ZX Spectrum rainbow
stripes with a drop shadow, and a white-bordered "NG" badge.

    python3 tools/icons/generate_unreal_icon.py                 # emit all artefacts
    python3 tools/icons/generate_unreal_icon.py --preview p.png # contact sheet only
"""

import sys

from iconlib import (BADGE_COLOR, BORDER_COLOR, SHADOW_COLOR, bordered_badge, cli,
                     finish, generate, load_font, new_canvas, stripes,
                     supersample_for, tile_background)

APP_DIR = 'unreal-qt'
ICON_BASENAME = 'unreal'         # unreal.icns / unreal.ico
DESKTOP_ICON = 'unrealng'        # Icon= key in unrealng.desktop


def render(size):
    ss = supersample_for(size)
    img, draw, canvas = new_canvas(size, ss)
    tile_background(draw, canvas)

    cx = cy = canvas / 2
    stripe_w = canvas * 0.11
    stripe_h = canvas * 0.65
    shadow = max(2 * ss, int(canvas * 0.02))
    stripes(draw, cx, cy, stripe_w, stripe_h, offset=(shadow, shadow), colors=[SHADOW_COLOR] * 4)
    stripes(draw, cx, cy, stripe_w, stripe_h)

    badge = int(canvas * 0.48)
    radius = int(badge * 0.12)
    border = max(int(1.25 * ss), int(canvas * 0.008))
    x0, y0 = cx - badge / 2, cy - badge / 2
    bordered_badge(draw, (x0, y0, x0 + badge, y0 + badge), radius, border, BADGE_COLOR)

    # "NG" is unreadable below ~24 px and just muddies the badge, so skip it there.
    if size >= 24:
        font = load_font(max(8 * ss, int(canvas * 0.26)))
        text = 'NG'
        bbox = draw.textbbox((0, 0), text, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        draw.text((cx - tw / 2 - bbox[0], cy - th / 2 - bbox[1]), text, fill=BORDER_COLOR, font=font)

    return finish(img, size, ss)


def main():
    return cli(__doc__.split('\n')[0], render,
               lambda: generate(APP_DIR, ICON_BASENAME, DESKTOP_ICON, render))


if __name__ == '__main__':
    sys.exit(main())
