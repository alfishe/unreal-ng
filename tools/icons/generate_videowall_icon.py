#!/usr/bin/env python3
"""Generate the UnrealNG Video Wall (unreal-videowall) application icon.

Design: the same dark rounded tile as the UnrealNG icon, filled with a wall of
white-bezelled screens.  The ZX Spectrum rainbow is displayed as one band that
runs across all screens, so the wall reads as a single large display.  Very
small sizes fall back to a 2x2 wall so the screens stay distinguishable.

    python3 tools/icons/generate_videowall_icon.py                 # emit all artefacts
    python3 tools/icons/generate_videowall_icon.py --preview p.png # contact sheet only
"""

import sys

from PIL import Image, ImageDraw

from iconlib import (BORDER_COLOR, cli, finish, generate, new_canvas, rounded_rect,
                     stripes, supersample_for, tile_background)

APP_DIR = 'unreal-videowall'
ICON_BASENAME = 'videowall'              # videowall.icns / videowall.ico
DESKTOP_ICON = 'unrealng-videowall'      # Icon= key in unrealng-videowall.desktop

SCREEN_COLOR = (0x14, 0x16, 0x1E)        # unlit ZX screen
GLOW_COLOR = (0x22, 0x25, 0x32)          # faint screen backlight


def render(size):
    ss = supersample_for(size)
    img, draw, canvas = new_canvas(size, ss)
    tile_background(draw, canvas)

    n = 2 if size < 32 else 3                     # screens per row/column
    wall = canvas * 0.70
    gap = canvas * (0.055 if n == 2 else 0.035)
    cell = (wall - gap * (n - 1)) / n
    bezel = max(int(1.25 * ss), int(canvas * 0.012))
    radius = int(cell * 0.12)
    origin = (canvas - wall) / 2

    # Bezels and unlit screens
    screen_mask = Image.new('L', (canvas, canvas), 0)
    mask_draw = ImageDraw.Draw(screen_mask)
    for row in range(n):
        for col in range(n):
            x0 = origin + col * (cell + gap)
            y0 = origin + row * (cell + gap)
            box = (x0, y0, x0 + cell, y0 + cell)
            rounded_rect(draw, (x0 - bezel, y0 - bezel, x0 + cell + bezel, y0 + cell + bezel),
                         radius + bezel, BORDER_COLOR)
            rounded_rect(draw, box, radius, SCREEN_COLOR)
            rounded_rect(mask_draw, box, radius, 255)

    # One rainbow band displayed across the whole wall, clipped to the screens
    content = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    cdraw = ImageDraw.Draw(content)
    cdraw.rectangle((0, 0, canvas, canvas), fill=GLOW_COLOR)
    band_w = canvas * 0.10
    stripes(cdraw, canvas / 2, canvas / 2, band_w, canvas * 1.2)
    img.paste(content, (0, 0), screen_mask)

    return finish(img, size, ss)


def main():
    return cli(__doc__.split('\n')[0], render,
               lambda: generate(APP_DIR, ICON_BASENAME, DESKTOP_ICON, render))


if __name__ == '__main__':
    sys.exit(main())
