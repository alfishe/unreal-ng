"""Shared drawing primitives and platform emitters for the UnrealNG app icons.

Each application has its own generator script next to this module
(``generate_unreal_icon.py``, ``generate_videowall_icon.py``).  They supply a
``render(size)`` callable; this module turns it into the platform artefacts:

    <app>/install/macos/<icon>.icns                     macOS bundle icon
    <app>/install/windows/<icon>.ico                    Windows executable icon
    <app>/install/linux/icons/hicolor/NxN/apps/<id>.png freedesktop icon theme
    <app>/resources/icons/app/appicon_<N>.png           Qt resource (window icon)

Requires Pillow.  On macOS ``iconutil`` is used to build the .icns; elsewhere
Pillow's own ICNS writer is used.
"""

import math
import os
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# ---------------------------------------------------------------------------
# Palette (from the new-gui branch design)
# ---------------------------------------------------------------------------

RAINBOW_COLORS = [
    (0xE0, 0x40, 0x40),  # Red
    (0xE8, 0xC0, 0x30),  # Yellow
    (0x40, 0xC0, 0x40),  # Green
    (0x40, 0x90, 0xE0),  # Blue
]
BG_COLOR = (0x3A, 0x3D, 0x4A)
BADGE_COLOR = (0x2A, 0x2D, 0x3A)
BORDER_COLOR = (0xFF, 0xFF, 0xFF)
SHADOW_COLOR = (0, 0, 0, 80)

FONT_CANDIDATES = [
    '/System/Library/Fonts/Helvetica.ttc',                       # macOS
    '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',           # Debian/Ubuntu
    '/usr/share/fonts/dejavu/DejaVuSans.ttf',                    # Fedora
    'C:/Windows/Fonts/arial.ttf',                                # Windows
]

# ---------------------------------------------------------------------------
# Size tables
# ---------------------------------------------------------------------------

MACOS_ICONSET = [
    ('icon_16x16.png', 16),
    ('icon_16x16@2x.png', 32),
    ('icon_32x32.png', 32),
    ('icon_32x32@2x.png', 64),
    ('icon_128x128.png', 128),
    ('icon_128x128@2x.png', 256),
    ('icon_256x256.png', 256),
    ('icon_256x256@2x.png', 512),
    ('icon_512x512.png', 512),
    ('icon_512x512@2x.png', 1024),
]
WINDOWS_SIZES = [16, 24, 32, 48, 64, 128, 256]
LINUX_SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]
QT_SIZES = [16, 32, 48, 64, 128, 256, 512, 1024]

# ---------------------------------------------------------------------------
# Drawing primitives (coordinates in supersampled pixels)
# ---------------------------------------------------------------------------


def supersample_for(size):
    return 4 if size <= 256 else 2


def new_canvas(size, ss):
    canvas = size * ss
    img = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img), canvas


def finish(img, size, ss):
    return img.resize((size, size), Image.LANCZOS) if ss > 1 else img


def rounded_rect(draw, box, radius, fill):
    draw.rounded_rectangle(box, radius=radius, fill=fill)


def tile_background(draw, canvas):
    """The dark rounded tile shared by all app icons (96% wide, 18% radius)."""
    body = canvas * 0.96
    margin = (canvas - body) / 2
    rounded_rect(draw, (margin, margin, margin + body, margin + body), canvas * 0.18, BG_COLOR)


def stripes(draw, cx, cy, width, height, offset=(0, 0), colors=None, angle=-30):
    """Four skewed parallelograms side by side, centred on (cx, cy)."""
    colors = colors or RAINBOW_COLORS
    skew = height * math.tan(math.radians(angle)) * 0.5
    start_x = cx - width * 2 + offset[0]
    top = cy - height / 2 + offset[1]
    bottom = cy + height / 2 + offset[1]
    for i, color in enumerate(colors):
        x = start_x + i * width
        draw.polygon([
            (x - skew, top),
            (x + width - skew, top),
            (x + width + skew, bottom),
            (x + skew, bottom),
        ], fill=color)


def bordered_badge(draw, box, radius, border, fill=BADGE_COLOR):
    """Dark rounded square with a white border drawn outside it."""
    x0, y0, x1, y1 = box
    rounded_rect(draw, (x0 - border, y0 - border, x1 + border, y1 + border),
                 radius + border, BORDER_COLOR)
    rounded_rect(draw, box, radius, fill)


def load_font(px):
    for path in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(path, px)
        except OSError:
            continue
    return ImageFont.load_default()


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------


def _rel(path):
    return os.path.relpath(path, ROOT_DIR)


def write_icns(path, render):
    tmp = tempfile.mkdtemp(prefix='unrealng-icon-')
    iconset = os.path.join(tmp, 'app.iconset')
    os.makedirs(iconset)
    try:
        for name, size in MACOS_ICONSET:
            render(size).save(os.path.join(iconset, name))
        if shutil.which('iconutil'):
            subprocess.run(['iconutil', '-c', 'icns', iconset, '-o', path], check=True)
        else:
            sizes = sorted({size for _, size in MACOS_ICONSET})
            images = [render(size) for size in sizes]
            images[-1].save(path, format='ICNS', append_images=images[:-1])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'  {_rel(path)}')


def write_ico(path, render):
    # Pillow drops any frame larger than the base image, so lead with 256 px.
    images = [render(size) for size in sorted(WINDOWS_SIZES, reverse=True)]
    images[0].save(path, format='ICO',
                   sizes=[(im.width, im.height) for im in images],
                   append_images=images[1:])
    print(f'  {_rel(path)}  sizes={WINDOWS_SIZES}')


def write_hicolor(root, icon_name, render):
    if os.path.isdir(root):
        shutil.rmtree(root)
    for size in LINUX_SIZES:
        directory = os.path.join(root, f'{size}x{size}', 'apps')
        os.makedirs(directory, exist_ok=True)
        render(size).save(os.path.join(directory, f'{icon_name}.png'))
    print(f'  {_rel(root)}/<N>x<N>/apps/{icon_name}.png  sizes={LINUX_SIZES}')


def write_qt_pngs(directory, render):
    os.makedirs(directory, exist_ok=True)
    for size in QT_SIZES:
        render(size).save(os.path.join(directory, f'appicon_{size}.png'))
    print(f'  {_rel(directory)}/appicon_<N>.png  sizes={QT_SIZES}')


def write_preview(path, render, sizes=(16, 24, 32, 48, 64, 128, 256), zoom=4):
    """Contact sheet: native renders on top, small sizes zoomed below."""
    pad = 24
    row1 = [render(s) for s in sizes]
    small = [s for s in sizes if s <= 64]
    row2 = [render(s).resize((s * zoom, s * zoom), Image.NEAREST) for s in small]
    w = max(sum(im.width + pad for im in row1), sum(im.width + pad for im in row2)) + pad
    h1 = max(im.height for im in row1) + pad
    h2 = max(im.height for im in row2) + pad
    sheet = Image.new('RGBA', (w, h1 + h2 + pad), (0xE8, 0xE8, 0xE8, 255))
    for y, row, hh in ((pad, row1, h1), (h1 + pad, row2, h2)):
        x = pad
        for im in row:
            sheet.paste(im, (x, y + (hh - pad - im.height) // 2), im)
            x += im.width + pad
    sheet.save(path)
    print(f'Preview written to {path}')


def generate(app_dir, icon_basename, desktop_icon, render):
    """Emit the full artefact set for one application."""
    base = os.path.join(ROOT_DIR, app_dir)
    print(f'{app_dir}:')
    os.makedirs(os.path.join(base, 'install', 'macos'), exist_ok=True)
    os.makedirs(os.path.join(base, 'install', 'windows'), exist_ok=True)
    write_icns(os.path.join(base, 'install', 'macos', f'{icon_basename}.icns'), render)
    write_ico(os.path.join(base, 'install', 'windows', f'{icon_basename}.ico'), render)
    write_hicolor(os.path.join(base, 'install', 'linux', 'icons', 'hicolor'), desktop_icon, render)
    write_qt_pngs(os.path.join(base, 'resources', 'icons', 'app'), render)


def cli(description, render, generate_fn):
    import argparse
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('--preview', metavar='PNG', help='write a contact sheet and exit')
    args = parser.parse_args()
    if args.preview:
        write_preview(args.preview, render)
    else:
        generate_fn()
    return 0
