#!/usr/bin/env python3
"""Generate app icons at all required sizes using variant 3 (thick white border)."""

import os
import math
from PIL import Image, ImageDraw, ImageFont

# ZX Spectrum rainbow colors
RAINBOW_COLORS = [
    (0xE0, 0x40, 0x40),  # Red
    (0xE8, 0xC0, 0x30),  # Yellow
    (0x40, 0xC0, 0x40),  # Green
    (0x40, 0x90, 0xE0),  # Blue
]

BG_COLOR = (0x3A, 0x3D, 0x4A)
BADGE_COLOR = (0x2A, 0x2D, 0x3A)

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), 'app')


def draw_rounded_rect(draw, xy, radius, fill):
    """Draw a rounded rectangle."""
    x0, y0, x1, y1 = xy
    draw.rectangle([x0 + radius, y0, x1 - radius, y1], fill=fill)
    draw.rectangle([x0, y0 + radius, x1, y1 - radius], fill=fill)
    draw.ellipse([x0, y0, x0 + radius * 2, y0 + radius * 2], fill=fill)
    draw.ellipse([x1 - radius * 2, y0, x1, y0 + radius * 2], fill=fill)
    draw.ellipse([x0, y1 - radius * 2, x0 + radius * 2, y1], fill=fill)
    draw.ellipse([x1 - radius * 2, y1 - radius * 2, x1, y1], fill=fill)


def draw_diagonal_stripes(draw, cx, cy, stripe_width, stripe_height, angle=-30):
    """Draw 4 diagonal rainbow stripes."""
    total_width = stripe_width * 4
    start_x = cx - total_width / 2
    for i, color in enumerate(RAINBOW_COLORS):
        x = start_x + i * stripe_width
        rad = math.radians(angle)
        skew = stripe_height * math.tan(rad) * 0.5
        points = [
            (x - skew, cy - stripe_height / 2),
            (x + stripe_width - skew, cy - stripe_height / 2),
            (x + stripe_width + skew, cy + stripe_height / 2),
            (x + skew, cy + stripe_height / 2),
        ]
        draw.polygon(points, fill=color)


def draw_diagonal_stripes_shadow(draw, cx, cy, stripe_width, stripe_height, offset=4):
    """Draw shadow for diagonal stripes."""
    total_width = stripe_width * 4
    start_x = cx - total_width / 2 + offset
    for i in range(4):
        x = start_x + i * stripe_width
        rad = math.radians(-30)
        skew = stripe_height * math.tan(rad) * 0.5
        points = [
            (x - skew, cy - stripe_height / 2 + offset),
            (x + stripe_width - skew, cy - stripe_height / 2 + offset),
            (x + stripe_width + skew, cy + stripe_height / 2 + offset),
            (x + skew, cy + stripe_height / 2 + offset),
        ]
        draw.polygon(points, fill=(0, 0, 0, 80))


def create_icon(size):
    """Create icon variant 3: Small badge with thick white border."""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Rounded background
    margin = int(size * 0.02)
    radius = int(size * 0.18)
    draw_rounded_rect(draw, (margin, margin, size - margin, size - margin), radius, BG_COLOR)

    # Draw stripes
    cx, cy = size / 2, size / 2
    stripe_width = size * 0.11
    stripe_height = size * 0.65
    draw_diagonal_stripes_shadow(draw, cx, cy, stripe_width, stripe_height, offset=5)
    draw_diagonal_stripes(draw, cx, cy, stripe_width, stripe_height)

    # Badge params (variant 3: thick white border)
    badge_size_ratio = 0.48
    border_width = max(1, int(size * 0.008))  # Scale border with size
    border_color = (255, 255, 255)

    badge_size = int(size * badge_size_ratio)
    badge_margin = (size - badge_size) // 2
    badge_radius = int(badge_size * 0.12)

    # Draw thick white border
    draw_rounded_rect(draw,
                      (badge_margin - border_width, badge_margin - border_width,
                       badge_margin + badge_size + border_width, badge_margin + badge_size + border_width),
                      badge_radius + border_width // 2, border_color)

    # Draw badge
    draw_rounded_rect(draw,
                      (badge_margin, badge_margin,
                       badge_margin + badge_size, badge_margin + badge_size),
                      badge_radius, BADGE_COLOR)

    # Draw "NG" text
    font_size = max(8, int(size * 0.26))
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_size)
    except:
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", font_size)
        except:
            font = ImageFont.load_default()

    text = "NG"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = size / 2 - tw / 2
    ty = size / 2 - th / 2 - bbox[1]
    draw.text((tx, ty), text, fill=(255, 255, 255), font=font)

    return img


def generate_all_icons():
    """Generate all icon sizes and create .icns and .ico files."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Generate large sizes (256+) natively for best quality
    large_sizes = [1024, 512, 256]
    for size in large_sizes:
        img = create_icon(size)
        path = os.path.join(OUTPUT_DIR, f'appicon_{size}.png')
        img.save(path, 'PNG')
        print(f'Generated appicon_{size}.png')

    # Scale smaller sizes from 256px source (avoids distortions)
    src_256 = Image.open(os.path.join(OUTPUT_DIR, 'appicon_256.png'))
    small_sizes = [128, 64, 48, 32, 16]
    for size in small_sizes:
        img = src_256.resize((size, size), Image.LANCZOS)
        path = os.path.join(OUTPUT_DIR, f'appicon_{size}.png')
        img.save(path, 'PNG')
        print(f'Scaled appicon_{size}.png from 256px')

    # Generate .icns for macOS
    import subprocess
    iconset_dir = os.path.join(OUTPUT_DIR, 'unreal-ng.iconset')
    os.makedirs(iconset_dir, exist_ok=True)

    # Copy files to iconset with proper naming
    import shutil
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_16.png'), os.path.join(iconset_dir, 'icon_16x16.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_32.png'), os.path.join(iconset_dir, 'icon_16x16@2x.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_32.png'), os.path.join(iconset_dir, 'icon_32x32.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_64.png'), os.path.join(iconset_dir, 'icon_32x32@2x.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_128.png'), os.path.join(iconset_dir, 'icon_128x128.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_256.png'), os.path.join(iconset_dir, 'icon_128x128@2x.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_256.png'), os.path.join(iconset_dir, 'icon_256x256.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_512.png'), os.path.join(iconset_dir, 'icon_256x256@2x.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_512.png'), os.path.join(iconset_dir, 'icon_512x512.png'))
    shutil.copy(os.path.join(OUTPUT_DIR, 'appicon_1024.png'), os.path.join(iconset_dir, 'icon_512x512@2x.png'))

    # Create .icns
    icns_path = os.path.join(OUTPUT_DIR, 'unreal-ng.icns')
    subprocess.run(['iconutil', '-c', 'icns', iconset_dir, '-o', icns_path], check=True)
    shutil.rmtree(iconset_dir)
    print(f'Generated unreal-ng.icns')

    # Generate .ico for Windows
    ico_path = os.path.join(OUTPUT_DIR, 'unreal-ng.ico')
    img = Image.open(os.path.join(OUTPUT_DIR, 'appicon_256.png'))
    img.save(ico_path, format='ICO', sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
    print(f'Generated unreal-ng.ico')

    print('\nDone!')


if __name__ == "__main__":
    generate_all_icons()
