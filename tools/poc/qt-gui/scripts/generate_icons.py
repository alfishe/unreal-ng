#!/usr/bin/env python3
"""Generate app icons for macOS, Linux, and Windows from synthesized design."""

import os
import subprocess
import shutil
import math
from PIL import Image, ImageDraw, ImageFont

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'resources', 'icons', 'app')

# Icon sizes needed for each platform
MACOS_SIZES = [16, 32, 64, 128, 256, 512, 1024]
WINDOWS_SIZES = [16, 24, 32, 48, 64, 128, 256]
LINUX_SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]

# ZX Spectrum rainbow colors
RAINBOW_COLORS = [
    (0xE0, 0x40, 0x40),  # Red
    (0xE8, 0xC0, 0x30),  # Yellow
    (0x40, 0xC0, 0x40),  # Green
    (0x40, 0x90, 0xE0),  # Blue
]

BG_COLOR = (0x3A, 0x3D, 0x4A)
BADGE_COLOR = (0x2A, 0x2D, 0x3A)

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

def create_icon(size, flat=False):
    """Create the final icon design at given size.

    Args:
        size: Icon size in pixels
        flat: If True, create flat square icon (for macOS/iOS where system applies masking)
    """
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    if flat:
        # Flat square for macOS/iOS - system applies rounding
        draw.rectangle([0, 0, size, size], fill=BG_COLOR)
    else:
        # Rounded background for other platforms
        margin = int(size * 0.02)
        radius = int(size * 0.18)
        draw_rounded_rect(draw, (margin, margin, size - margin, size - margin), radius, BG_COLOR)

    # Draw stripes
    cx, cy = size / 2, size / 2
    stripe_width = size * 0.11
    stripe_height = size * 0.65
    draw_diagonal_stripes_shadow(draw, cx, cy, stripe_width, stripe_height, offset=max(2, size // 100))
    draw_diagonal_stripes(draw, cx, cy, stripe_width, stripe_height)

    # Badge parameters (variant 3: thick white border)
    badge_size_ratio = 0.48
    badge_radius_ratio = 0.12
    border_width = max(2, int(size * 0.008))
    font_size_ratio = 0.26

    badge_size = int(size * badge_size_ratio)
    badge_margin = (size - badge_size) // 2
    badge_radius = int(badge_size * badge_radius_ratio)

    # Draw white border
    draw_rounded_rect(draw,
                      (badge_margin - border_width, badge_margin - border_width,
                       badge_margin + badge_size + border_width, badge_margin + badge_size + border_width),
                      badge_radius + border_width // 2, (255, 255, 255))

    # Draw badge
    draw_rounded_rect(draw,
                      (badge_margin, badge_margin,
                       badge_margin + badge_size, badge_margin + badge_size),
                      badge_radius, BADGE_COLOR)

    # Draw "NG" text (skip for very small sizes)
    font_size = max(8, int(size * font_size_ratio))
    if font_size >= 8:
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
        tx = size/2 - tw/2
        ty = size/2 - th/2 - bbox[1]
        draw.text((tx, ty), text, fill=(255, 255, 255), font=font)

    return img

def generate_macos_iconset(output_dir):
    """Generate macOS .iconset directory and .icns file."""
    iconset_dir = os.path.join(output_dir, "unreal-ng.iconset")
    os.makedirs(iconset_dir, exist_ok=True)

    icon_specs = [
        (16, "icon_16x16.png"),
        (32, "icon_16x16@2x.png"),
        (32, "icon_32x32.png"),
        (64, "icon_32x32@2x.png"),
        (128, "icon_128x128.png"),
        (256, "icon_128x128@2x.png"),
        (256, "icon_256x256.png"),
        (512, "icon_256x256@2x.png"),
        (512, "icon_512x512.png"),
        (1024, "icon_512x512@2x.png"),
    ]

    for size, filename in icon_specs:
        img = create_icon(size, flat=True)  # macOS uses flat square icons
        output_path = os.path.join(iconset_dir, filename)
        img.save(output_path)
        print(f"  Created {filename}")

    icns_path = os.path.join(output_dir, "unreal-ng.icns")
    try:
        subprocess.run(["iconutil", "-c", "icns", iconset_dir, "-o", icns_path], check=True)
        print(f"  Created unreal-ng.icns")
        shutil.rmtree(iconset_dir)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("  Warning: iconutil not available, .icns not created")

def generate_windows_ico(output_dir):
    """Generate Windows .ico file with multiple sizes."""
    ico_path = os.path.join(output_dir, "unreal-ng.ico")

    images = []
    for size in WINDOWS_SIZES:
        img = create_icon(size)
        images.append(img)

    images[0].save(
        ico_path,
        format='ICO',
        sizes=[(img.width, img.height) for img in images],
        append_images=images[1:]
    )
    print(f"  Created unreal-ng.ico with sizes: {WINDOWS_SIZES}")

def generate_linux_pngs(output_dir):
    """Generate PNG icons for Linux in standard sizes."""
    linux_dir = os.path.join(output_dir, "linux")
    os.makedirs(linux_dir, exist_ok=True)

    for size in LINUX_SIZES:
        img = create_icon(size)
        filename = f"unreal-ng_{size}x{size}.png"
        output_path = os.path.join(linux_dir, filename)
        img.save(output_path)
        print(f"  Created {filename}")

    hicolor_dir = os.path.join(output_dir, "hicolor")
    for size in LINUX_SIZES:
        size_dir = os.path.join(hicolor_dir, f"{size}x{size}", "apps")
        os.makedirs(size_dir, exist_ok=True)
        img = create_icon(size)
        output_path = os.path.join(size_dir, "unreal-ng.png")
        img.save(output_path)
    print(f"  Created hicolor directory structure")

def generate_qt_icons(output_dir):
    """Generate icons for Qt resource system."""
    qt_sizes = [16, 32, 48, 64, 128, 256]

    for size in qt_sizes:
        img = create_icon(size)
        filename = f"appicon_{size}.png"
        output_path = os.path.join(output_dir, filename)
        img.save(output_path)
        print(f"  Created {filename}")

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("Generating macOS icons...")
    generate_macos_iconset(OUTPUT_DIR)

    print("\nGenerating Windows icons...")
    generate_windows_ico(OUTPUT_DIR)

    print("\nGenerating Linux icons...")
    generate_linux_pngs(OUTPUT_DIR)

    print("\nGenerating Qt resource icons...")
    generate_qt_icons(OUTPUT_DIR)

    print(f"\nAll icons generated in {OUTPUT_DIR}")

if __name__ == "__main__":
    main()
