#!/usr/bin/env python3
"""Generate icon design variants for review."""

import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'resources', 'icons', 'variants')
ICON_SIZE = 512
SHOWCASE_COLS = 4

# ZX Spectrum rainbow colors
RAINBOW_COLORS = [
    (0xE0, 0x40, 0x40),  # Red
    (0xE8, 0xC0, 0x30),  # Yellow
    (0x40, 0xC0, 0x40),  # Green
    (0x40, 0x90, 0xE0),  # Blue
]

# Dark background color
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

def draw_base_with_stripes(size):
    """Create base image with background and stripes."""
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

    return img

def add_badge_and_text(img, size, badge_size_ratio=0.50, badge_radius_ratio=0.12,
                       badge_opacity=255, border_width=0, border_color=(80, 80, 90),
                       font_size_ratio=0.28, shadow=False):
    """Add badge and NG text to image."""
    draw = ImageDraw.Draw(img)

    badge_size = int(size * badge_size_ratio)
    badge_margin = (size - badge_size) // 2
    badge_radius = int(badge_size * badge_radius_ratio)

    # Draw border if specified
    if border_width > 0:
        draw_rounded_rect(draw,
                          (badge_margin - border_width, badge_margin - border_width,
                           badge_margin + badge_size + border_width, badge_margin + badge_size + border_width),
                          badge_radius + border_width // 2, border_color)

    # Draw badge shadow if specified
    if shadow:
        shadow_offset = 4
        shadow_color = (0, 0, 0, 100)
        draw_rounded_rect(draw,
                          (badge_margin + shadow_offset, badge_margin + shadow_offset,
                           badge_margin + badge_size + shadow_offset, badge_margin + badge_size + shadow_offset),
                          badge_radius, shadow_color)

    # Draw badge
    badge_color_with_alpha = (*BADGE_COLOR, badge_opacity)
    draw_rounded_rect(draw,
                      (badge_margin, badge_margin,
                       badge_margin + badge_size, badge_margin + badge_size),
                      badge_radius, badge_color_with_alpha)

    # Draw "NG" text
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", int(size * font_size_ratio))
    except:
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", int(size * font_size_ratio))
        except:
            font = ImageFont.load_default()

    text = "NG"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = size/2 - tw/2
    ty = size/2 - th/2 - bbox[1]
    draw.text((tx, ty), text, fill=(255, 255, 255), font=font)

    return img

def create_variant_1(size):
    """Variant 1: Small badge, solid, no border."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26)

def create_variant_2(size):
    """Variant 2: Small badge, thin white border."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              border_width=2, border_color=(200, 200, 200))

def create_variant_3(size):
    """Variant 3: Small badge, thick white border."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              border_width=4, border_color=(255, 255, 255))

def create_variant_4(size):
    """Variant 4: Small badge, semi-transparent (85%)."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              badge_opacity=217)

def create_variant_5(size):
    """Variant 5: Small badge, semi-transparent (70%)."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              badge_opacity=178)

def create_variant_6(size):
    """Variant 6: Small badge, drop shadow."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              shadow=True)

def create_variant_7(size):
    """Variant 7: Small badge, gray border + shadow."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              border_width=2, border_color=(100, 100, 110), shadow=True)

def create_variant_8(size):
    """Variant 8: Small badge, subtle glow border."""
    img = draw_base_with_stripes(size)
    return add_badge_and_text(img, size, badge_size_ratio=0.48, font_size_ratio=0.26,
                              border_width=3, border_color=(70, 75, 90))

def create_showcase(variants, labels):
    """Create a showcase image with all variants."""
    n = len(variants)
    cols = SHOWCASE_COLS
    rows = (n + cols - 1) // cols

    padding = 40
    label_height = 40
    cell_size = ICON_SIZE + padding * 2 + label_height

    showcase = Image.new('RGB', (cols * cell_size, rows * cell_size), (240, 240, 240))
    draw = ImageDraw.Draw(showcase)

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 24)
    except:
        font = ImageFont.load_default()

    for i, (variant, label) in enumerate(zip(variants, labels)):
        row = i // cols
        col = i % cols

        x = col * cell_size + padding
        y = row * cell_size + padding

        showcase.paste(variant, (x, y), variant)

        bbox = draw.textbbox((0, 0), label, font=font)
        tw = bbox[2] - bbox[0]
        label_x = x + (ICON_SIZE - tw) // 2
        label_y = y + ICON_SIZE + 10
        draw.text((label_x, label_y), label, fill=(60, 60, 60), font=font)

    return showcase

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    variants = []
    labels = []

    generators = [
        (create_variant_1, "1. Solid, no border"),
        (create_variant_2, "2. Thin white border"),
        (create_variant_3, "3. Thick white border"),
        (create_variant_4, "4. Alpha 85%"),
        (create_variant_5, "5. Alpha 70%"),
        (create_variant_6, "6. Drop shadow"),
        (create_variant_7, "7. Gray border+shadow"),
        (create_variant_8, "8. Subtle glow"),
    ]

    for gen_func, label in generators:
        print(f"Generating {label}...")
        img = gen_func(ICON_SIZE)
        variants.append(img)
        labels.append(label)

        filename = f"variant_{label.split('.')[0].strip()}.png"
        img.save(os.path.join(OUTPUT_DIR, filename))

    print("\nCreating showcase...")
    showcase = create_showcase(variants, labels)
    showcase_path = os.path.join(OUTPUT_DIR, "showcase.png")
    showcase.save(showcase_path)

    print(f"\nSaved {len(variants)} variants and showcase to {OUTPUT_DIR}")
    return showcase_path

if __name__ == "__main__":
    path = main()
    import subprocess
    subprocess.run(["open", path])
