#!/usr/bin/env python3
"""Generate test pattern PNG files for each video mode."""

from PIL import Image, ImageDraw, ImageFont
import os

# Output directory
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'resources', 'patterns')

# ZX Spectrum standard palette (non-bright)
ZX_PALETTE = [
    (0x00, 0x00, 0x00),  # Black
    (0x00, 0x00, 0xD7),  # Blue
    (0xD7, 0x00, 0x00),  # Red
    (0xD7, 0x00, 0xD7),  # Magenta
    (0x00, 0xD7, 0x00),  # Green
    (0x00, 0xD7, 0xD7),  # Cyan
    (0xD7, 0xD7, 0x00),  # Yellow
    (0xD7, 0xD7, 0xD7),  # White
]

# ZX Spectrum bright palette
ZX_BRIGHT = [
    (0x00, 0x00, 0x00),  # Black (same)
    (0x00, 0x00, 0xFF),  # Bright Blue
    (0xFF, 0x00, 0x00),  # Bright Red
    (0xFF, 0x00, 0xFF),  # Bright Magenta
    (0x00, 0xFF, 0x00),  # Bright Green
    (0x00, 0xFF, 0xFF),  # Bright Cyan
    (0xFF, 0xFF, 0x00),  # Bright Yellow
    (0xFF, 0xFF, 0xFF),  # Bright White
]

# Extended 256-color palette for ATM/TS-Conf (RGB332)
def rgb332_palette():
    palette = []
    for i in range(256):
        r = ((i >> 5) & 0x07) * 255 // 7
        g = ((i >> 2) & 0x07) * 255 // 7
        b = (i & 0x03) * 255 // 3
        palette.append((r, g, b))
    return palette

# ZX Next 256-color palette (9-bit RGB)
def next_palette():
    palette = []
    for i in range(256):
        # Next uses RRRGGGBB format in standard mode
        r = ((i >> 5) & 0x07) * 255 // 7
        g = ((i >> 2) & 0x07) * 255 // 7
        b = (i & 0x03) * 255 // 3
        palette.append((r, g, b))
    return palette

VIDEO_MODES = [
    # (filename, width, height, label, palette_type)
    ("zx_256x192", 256, 192, "ZX 256x192", "zx"),
    ("zx_320x240", 320, 240, "ZX 320x240", "zx"),
    ("zx_352x288", 352, 288, "ZX 352x288 PAL", "zx"),
    ("zx_384x304", 384, 304, "ZX 384x304", "zx"),
    ("atm_320x200", 320, 200, "ATM 320x200", "extended"),
    ("atm_640x200", 640, 200, "ATM 640x200", "extended"),
    ("profi_512x240", 512, 240, "Profi 512x240", "extended"),
    ("evo_360x288", 360, 288, "Evo 360x288", "extended"),
    ("tsconf_640x400", 640, 400, "TS-Conf 640x400", "extended"),
    ("tsconf_640x480", 640, 480, "TS-Conf 640x480", "extended"),
    ("next_320x256", 320, 256, "Next 320x256", "next"),
    ("next_640x256", 640, 256, "Next 640x256", "next"),
    ("hires_512x384", 512, 384, "Hi-Res 512x384", "zx"),
    ("hires_640x512", 640, 512, "Hi-Res 640x512", "extended"),
]

def draw_zx_color_bars(draw, x, y, w, h, bright=False):
    """Draw 8 ZX Spectrum color bars."""
    palette = ZX_BRIGHT if bright else ZX_PALETTE
    bar_w = w // 8
    for i, color in enumerate(palette):
        draw.rectangle([x + i * bar_w, y, x + (i + 1) * bar_w - 1, y + h - 1], fill=color)

def draw_extended_color_bars(draw, x, y, w, h):
    """Draw extended palette color bars (16 colors)."""
    palette = rgb332_palette()
    bar_w = w // 16
    for i in range(16):
        color = palette[i * 16]
        draw.rectangle([x + i * bar_w, y, x + (i + 1) * bar_w - 1, y + h - 1], fill=color)

def draw_gradient(draw, x, y, w, h, horizontal=True):
    """Draw a grayscale gradient."""
    if horizontal:
        for i in range(w):
            gray = int(i * 255 / w)
            draw.line([(x + i, y), (x + i, y + h - 1)], fill=(gray, gray, gray))
    else:
        for i in range(h):
            gray = int(i * 255 / h)
            draw.line([(x, y + i), (x + w - 1, y + i)], fill=(gray, gray, gray))

def draw_checkerboard(draw, x, y, w, h, cell_size=8):
    """Draw a checkerboard pattern."""
    for cy in range(0, h, cell_size):
        for cx in range(0, w, cell_size):
            color = (255, 255, 255) if ((cx // cell_size) + (cy // cell_size)) % 2 == 0 else (0, 0, 0)
            draw.rectangle([x + cx, y + cy, x + cx + cell_size - 1, y + cy + cell_size - 1], fill=color)

def draw_grid(draw, x, y, w, h, spacing=16):
    """Draw a pixel grid."""
    for gx in range(x, x + w, spacing):
        draw.line([(gx, y), (gx, y + h - 1)], fill=(64, 64, 64))
    for gy in range(y, y + h, spacing):
        draw.line([(x, gy), (x + w - 1, gy)], fill=(64, 64, 64))

def draw_border_markers(draw, w, h):
    """Draw corner markers and edge indicators."""
    # Corner markers (8x8 squares)
    marker_size = 8
    corners = [(0, 0), (w - marker_size, 0), (0, h - marker_size), (w - marker_size, h - marker_size)]
    for cx, cy in corners:
        draw.rectangle([cx, cy, cx + marker_size - 1, cy + marker_size - 1], fill=(255, 0, 0))

    # Center cross
    cx, cy = w // 2, h // 2
    draw.line([(cx - 10, cy), (cx + 10, cy)], fill=(255, 255, 255), width=1)
    draw.line([(cx, cy - 10), (cx, cy + 10)], fill=(255, 255, 255), width=1)

def draw_resolution_info(draw, w, h, label, font):
    """Draw resolution and mode info text."""
    # Mode label at top
    text = label
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    draw.text(((w - text_w) // 2, 4), text, fill=(255, 255, 255), font=font)

    # Resolution at bottom
    res_text = f"{w}x{h}"
    bbox = draw.textbbox((0, 0), res_text, font=font)
    text_w = bbox[2] - bbox[0]
    draw.text(((w - text_w) // 2, h - 20), res_text, fill=(200, 200, 200), font=font)

def generate_test_pattern(filename, width, height, label, palette_type):
    """Generate a test pattern image."""
    img = Image.new('RGBA', (width, height), (0, 0, 0, 255))
    draw = ImageDraw.Draw(img)

    # Try to load a font
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Monaco.ttf", 12)
    except:
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12)
        except:
            font = ImageFont.load_default()

    # Calculate regions
    header_h = 24
    footer_h = 24
    content_h = height - header_h - footer_h

    # Background
    draw.rectangle([0, 0, width - 1, height - 1], fill=(32, 32, 48))

    # Header bar
    draw.rectangle([0, 0, width - 1, header_h - 1], fill=(0, 0, 96))

    # Footer bar
    draw.rectangle([0, height - footer_h, width - 1, height - 1], fill=(0, 0, 96))

    # Content area - split into sections
    content_y = header_h
    section_h = content_h // 4

    # Section 1: Color bars
    if palette_type == "zx":
        draw_zx_color_bars(draw, 0, content_y, width, section_h, bright=False)
        # Draw bright colors on right half if wide enough
        if width >= 320:
            draw_zx_color_bars(draw, width // 2, content_y, width // 2, section_h, bright=True)
    elif palette_type == "extended":
        draw_extended_color_bars(draw, 0, content_y, width, section_h)
    elif palette_type == "next":
        # ZX Next: show both standard and bright ZX colors plus extended
        draw_zx_color_bars(draw, 0, content_y, width // 2, section_h, bright=False)
        draw_extended_color_bars(draw, width // 2, content_y, width // 2, section_h)

    content_y += section_h

    # Section 2: Grayscale gradient
    draw_gradient(draw, 0, content_y, width, section_h, horizontal=True)

    content_y += section_h

    # Section 3: Checkerboard
    cell_size = 8 if width < 400 else 16
    draw_checkerboard(draw, 0, content_y, width, section_h, cell_size)

    content_y += section_h

    # Section 4: Grid with markers
    draw.rectangle([0, content_y, width - 1, content_y + section_h - 1], fill=(0, 0, 0))
    spacing = 16 if width < 400 else 32
    draw_grid(draw, 0, content_y, width, section_h, spacing)

    # Border markers
    draw_border_markers(draw, width, height)

    # Text labels
    draw_resolution_info(draw, width, height, label, font)

    return img

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for filename, width, height, label, palette_type in VIDEO_MODES:
        print(f"Generating {filename}.png ({width}x{height})...")
        img = generate_test_pattern(filename, width, height, label, palette_type)
        img.save(os.path.join(OUTPUT_DIR, f"{filename}.png"))

    print(f"\nGenerated {len(VIDEO_MODES)} test patterns in {OUTPUT_DIR}")

if __name__ == "__main__":
    main()
