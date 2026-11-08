#!/usr/bin/env python3
"""Generate the Steam library artwork of the Steam Deck client ("Unreal NG", a non-Steam game).

The style of the app icon (iconlib.py, generate_unreal_icon.py): the dark slate, the four ZX Spectrum rainbow stripes
and the white-bordered "NG" badge. Steam shows five pictures of a non-Steam game, read from
userdata/<account>/config/grid/ under the shortcut's app id (install/steamdeck/steam-library.py installs them):

    poster.png   600 x 900    library capsule (portrait)          <appid>p.png
    capsule.png  920 x 430    wide capsule (home, last played)    <appid>.png
    hero.png     1920 x 620   banner at the top of the game page  <appid>_hero.png
    logo.png     1280 x 400   transparent logo over the hero      <appid>_logo.png
    icon.png     256 x 256    the app icon                        <appid>_icon.png

    python3 tools/icons/generate_steam_art.py                  # write install/steamdeck/art/*.png
    python3 tools/icons/generate_steam_art.py --out DIR        # elsewhere
    python3 tools/icons/generate_steam_art.py --preview p.png  # contact sheet only

Requires Pillow. The PNGs are committed: the Steam Deck bundle (install/steamdeck/package.sh) and apps/steamdeck-client/deploy.sh only copy them.
"""

import argparse
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iconlib import BADGE_COLOR, BG_COLOR, BORDER_COLOR, RAINBOW_COLORS, ROOT_DIR  # noqa: E402
import generate_unreal_icon  # noqa: E402

OUT_DIR = os.path.join(ROOT_DIR, 'install', 'steamdeck', 'art')

TITLE = 'UNREAL NG'
SUBTITLE = 'ZX SPECTRUM EMULATOR'
TEXT_COLOR = BORDER_COLOR
SUBTITLE_COLOR = (0xB8, 0xBC, 0xCC)
DEEP_COLOR = (0x1C, 0x1E, 0x27)   # the darkest point of the background gradient

BOLD_FONTS = [
    ('/System/Library/Fonts/Helvetica.ttc', 1),                   # macOS: Helvetica Bold
    ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 0),  # Debian/Ubuntu
    ('/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf', 0),           # Fedora
    ('C:/Windows/Fonts/arialbd.ttf', 0),                          # Windows
]
SS = 2  # supersampling of every picture


def bold_font(px):
    for path, index in BOLD_FONTS:
        try:
            return ImageFont.truetype(path, px, index=index)
        except OSError:
            continue
    return ImageFont.load_default()


def gradient(w, h, top=BG_COLOR, bottom=DEEP_COLOR):
    """A vertical slate gradient, lighter at the top."""
    img = Image.new('RGBA', (w, h))
    draw = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        draw.line([(0, y), (w, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    return img


def band(img, x, y, width, length, angle=60, shadow=True):
    """The Spectrum's four stripes as one diagonal band: (x, y) is the middle of its lower end, `width` is one
    stripe across, the band rises at `angle` degrees to the right for `length` pixels."""
    a = math.radians(angle)
    dx, dy = math.cos(a), -math.sin(a)   # along the band
    nx, ny = -dy, dx                     # across it (to the lower right)
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    def quad(i, off):
        s0 = (i - 2) * width
        s1 = s0 + width
        x0, y0 = x + off, y + off
        return [(x0 + nx * s0, y0 + ny * s0), (x0 + nx * s1, y0 + ny * s1),
                (x0 + nx * s1 + dx * length, y0 + ny * s1 + dy * length),
                (x0 + nx * s0 + dx * length, y0 + ny * s0 + dy * length)]

    if shadow:
        blur = Image.new('RGBA', img.size, (0, 0, 0, 0))
        bdraw = ImageDraw.Draw(blur)
        for i in range(4):
            bdraw.polygon(quad(i, width * 0.18), fill=(0, 0, 0, 110))
        img.alpha_composite(blur.filter(ImageFilter.GaussianBlur(width * 0.25)))
    for i, color in enumerate(RAINBOW_COLORS):
        draw.polygon(quad(i, 0), fill=color + (255,))
    img.alpha_composite(layer)


def icon(size):
    return generate_unreal_icon.render(size)


def paste_icon(img, size, cx, cy, glow=True):
    tile = icon(size)
    if glow:
        halo = Image.new('RGBA', img.size, (0, 0, 0, 0))
        hd = ImageDraw.Draw(halo)
        r = size * 0.55
        hd.ellipse((cx - r, cy - r + size * 0.06, cx + r, cy + r + size * 0.06), fill=(0, 0, 0, 120))
        img.alpha_composite(halo.filter(ImageFilter.GaussianBlur(size * 0.12)))
    img.alpha_composite(tile, (round(cx - size / 2), round(cy - size / 2)))


def text_center(draw, cx, top, text, font, fill, spacing=0):
    """Draw `text` centred on cx with its ink top at `top` and `spacing` extra pixels between letters; the height."""
    widths = [draw.textlength(ch, font=font) for ch in text]
    total = sum(widths) + spacing * (len(text) - 1)
    bbox = draw.textbbox((0, 0), text, font=font)
    x = cx - total / 2
    for ch, w in zip(text, widths):
        draw.text((x, top - bbox[1]), ch, font=font, fill=fill)
        x += w + spacing
    return bbox[3] - bbox[1]


def text_width(draw, text, font, spacing=0):
    return sum(draw.textlength(ch, font=font) for ch in text) + spacing * (len(text) - 1)


def wordmark(draw, cx, top, title_px, sub_px=None):
    """UNREAL NG with the subtitle under it, centred on cx; the bottom."""
    h = text_center(draw, cx, top, TITLE, bold_font(title_px), TEXT_COLOR, spacing=title_px * 0.06)
    bottom = top + h
    if sub_px:
        gap = sub_px * 0.9
        h2 = text_center(draw, cx, bottom + gap, SUBTITLE, bold_font(sub_px), SUBTITLE_COLOR, spacing=sub_px * 0.32)
        bottom += gap + h2
    return bottom


def finish(img, size):
    return img.resize(size, Image.LANCZOS)


def poster():
    W, H = 600 * SS, 900 * SS
    img = gradient(W, H)
    band(img, W * 0.66, H * 1.12, W * 0.085, H * 0.95)
    draw = ImageDraw.Draw(img)
    paste_icon(img, int(W * 0.46), W / 2, H * 0.29)
    wordmark(draw, W / 2, H * 0.50, int(W * 0.115), int(W * 0.036))
    return finish(img, (600, 900))


def capsule():
    """The wide capsule: the home screen's big tile of the last game played (Steam picks that size, not the poster)"""
    W, H = 920 * SS, 430 * SS
    img = gradient(W, H)
    band(img, W * 0.865, H * 1.2, H * 0.085, H * 1.8)
    draw = ImageDraw.Draw(img)
    paste_icon(img, int(H * 0.56), W * 0.20, H * 0.50)
    title_px = int(H * 0.17)
    cx = W * 0.20 + H * 0.30 + text_width(draw, TITLE, bold_font(title_px), title_px * 0.06) / 2 + H * 0.06
    wordmark(draw, cx, H * 0.36, title_px, int(H * 0.052))
    return finish(img, (920, 430))


def hero():
    W, H = 1920 * SS, 620 * SS
    img = gradient(W, H)
    band(img, W * 0.78, H * 1.25, H * 0.10, H * 1.9)
    # a fainter echo of the band further left: depth without a second focal point
    echo = Image.new('RGBA', img.size, (0, 0, 0, 0))
    band(echo, W * 0.58, H * 1.25, H * 0.05, H * 1.9, shadow=False)
    faded = Image.eval(echo.getchannel('A'), lambda a: a * 18 // 100)
    echo.putalpha(faded)
    img.alpha_composite(echo)
    return finish(img, (1920, 620))


def logo():
    W, H = 1280 * SS, 400 * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    size = int(H * 0.62)
    title_px = int(H * 0.25)
    tw = text_width(draw, TITLE, bold_font(title_px), title_px * 0.06)
    gap = H * 0.10
    left = (W - (size + gap + tw)) / 2
    paste_icon(img, size, left + size / 2, H / 2)
    wordmark(draw, left + size + gap + tw / 2, H * 0.30, title_px, int(H * 0.075))
    return finish(img, (1280, 400))


ART = {
    'poster.png': poster,
    'capsule.png': capsule,
    'hero.png': hero,
    'logo.png': logo,
    'icon.png': lambda: icon(256),
}


def preview(path, pictures):
    """The pictures on one sheet, the logo over the hero as Steam shows it."""
    hero_img = pictures['hero.png'].copy()
    lg = pictures['logo.png']
    hero_img.alpha_composite(lg.resize((lg.width * 3 // 5, lg.height * 3 // 5)), (120, 130))
    sheet = Image.new('RGBA', (1920 + 40, 620 + 900 + 60), (0x10, 0x11, 0x16, 255))
    sheet.alpha_composite(hero_img, (20, 20))
    sheet.alpha_composite(pictures['poster.png'], (20, 660))
    sheet.alpha_composite(pictures['capsule.png'], (640, 660))
    sheet.alpha_composite(pictures['icon.png'], (640, 1110))
    sheet.convert('RGB').save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', default=OUT_DIR, help='output directory (default: install/steamdeck/art)')
    parser.add_argument('--preview', help='write only a contact sheet to this file')
    args = parser.parse_args()
    pictures = {name: render() for name, render in ART.items()}
    if args.preview:
        preview(args.preview, pictures)
        print(f'preview: {args.preview}')
        return 0
    os.makedirs(args.out, exist_ok=True)
    for name, picture in pictures.items():
        path = os.path.join(args.out, name)
        picture.save(path, optimize=True)
        print(f'{os.path.relpath(path, ROOT_DIR)}  {picture.width}x{picture.height}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
