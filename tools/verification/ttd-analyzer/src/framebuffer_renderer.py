"""Render the ZX Spectrum screen RAM of a checkpoint to a PNG or PPM image.

The ZX Spectrum screen memory layout is non-linear and quirks of the ULA's
addressing have to be reproduced byte-for-byte to match what the emulator
would have shown on a real TV. This decoder follows the same convention as
``core/src/emulator/video/screen.cpp``.

Screen layout
-------------
For a 48K/128K/Pentagon model the visible screen is 256×192 pixels with a
single border (sized to taste; we use 32 px). The display fetches from a
16 KB bank:

* pixels — 6144 bytes at bank offset 0x0000..0x17FF
* attributes — 768 bytes at bank offset 0x1800..0x1AFF (32×24 attr grid)

Pixel byte addressing (classic ULA weirdness)::

    offset = (third << 11) | (line_in_char << 8) | (char_row << 5) | column

where ``third = y // 64``, ``line_in_char = y & 7``, ``char_row = (y >> 3) & 7``,
and ``column = x // 8``. Equivalently::

    offset = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3)

Each pixel byte stores 8 horizontal pixels, MSB = leftmost.

Attribute byte layout: ``FBBIIPPP`` where ``PPP`` = ink (FG), ``III`` = paper
(BG), ``B`` = bright, ``F`` = flash (animated; we render one phase only).

Bank selection
--------------
``p7FFD`` bit 3 selects which 16 KB bank the ULA displays:

* bit 3 = 0 → bank 5 (default at boot)
* bit 3 = 1 → bank 7 (128K/Pentagon "shadow" screen)

The selected bank is located at ``bank_index * PAGE_SIZE`` in the checkpoint's
materialized RAM image.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Optional, Tuple

from .ttd_format import (
    Checkpoint,
    EMU_PAGE_SIZE,
    NEVER_TOUCHED_SLOT,
    SUB_PAGES_PER_EMU_PAGE,
    TtdDump,
)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SCREEN_WIDTH = 256
SCREEN_HEIGHT = 192
PIXEL_BYTES = SCREEN_WIDTH * SCREEN_HEIGHT // 8  # 6144
ATTR_BYTES = 32 * 24                              # 768
SCREEN_BYTES = PIXEL_BYTES + ATTR_BYTES           # 6912

BORDER_DEFAULT_PX = 32  # border thickness on each side

# The canonical "Spectrum RGB" palette — used by every major emulator
# (SpecEmu, Zero, Fuse, Unreal). The non-bright values are (0, 0, 0)-ish
# to (215, 215, 215); bright variants push the non-zero channels to 255.
# This matches the YouTube-/Screenshot-grade look users expect.
PALETTE_NORMAL = [
    (0x00, 0x00, 0x00),  # black
    (0x00, 0x00, 0xD7),  # blue
    (0xD7, 0x00, 0x00),  # red
    (0xD7, 0x00, 0xD7),  # magenta
    (0x00, 0xD7, 0x00),  # green
    (0x00, 0xD7, 0xD7),  # cyan
    (0xD7, 0xD7, 0x00),  # yellow
    (0xD7, 0xD7, 0xD7),  # white
]
PALETTE_BRIGHT = [
    (0x00, 0x00, 0x00),  # black (bright bit has no effect on black)
    (0x00, 0x00, 0xFF),  # blue
    (0xFF, 0x00, 0x00),  # red
    (0xFF, 0x00, 0xFF),  # magenta
    (0x00, 0xFF, 0x00),  # green
    (0x00, 0xFF, 0xFF),  # cyan
    (0xFF, 0xFF, 0x00),  # yellow
    (0xFF, 0xFF, 0xFF),  # white
]


class RenderError(Exception):
    """Raised when the screen RAM can't be decoded for the checkpoint."""


# ---------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------


@dataclass
class ScreenImage:
    """Decoded RGB image with optional border."""

    width: int
    height: int
    rgb: bytes  # width * height * 3 bytes, row-major top-to-bottom

    def write_png(self, path: str) -> None:
        try:
            from PIL import Image  # type: ignore
        except ImportError as e:
            raise RenderError(
                "Writing PNG requires Pillow. Install with `pip install Pillow` "
                "or use write_ppm() instead."
            ) from e
        img = Image.frombytes("RGB", (self.width, self.height), self.rgb)
        img.save(path)

    def write_ppm(self, path: str) -> None:
        """Write a binary PPM (netpbm P6) — universally supported, no deps."""
        header = f"P6\n{self.width} {self.height}\n255\n".encode("ascii")
        with open(path, "wb") as f:
            f.write(header)
            f.write(self.rgb)


def _selected_screen_bank(p7ffd: int) -> int:
    """Return the physical bank index (5 or 7) the ULA is displaying."""
    return 7 if (p7ffd & 0b0000_1000) else 5


def decode_screen_rgb(
    cp: Checkpoint,
    ram: bytes,
    border_px: int = BORDER_DEFAULT_PX,
    flash_phase: int = 0,
) -> ScreenImage:
    """Decode the screen for one checkpoint.

    Parameters
    ----------
    cp
        The checkpoint (its ``chipset.p7ffd`` selects the screen bank; its
        ``chipset.border_attr`` paints the border).
    ram
        Materialized RAM image (use ``dump.materialize_ram(cp)``). Must be
        at least ``(bank + 1) * EMU_PAGE_SIZE`` bytes; the screen occupies
        the top 6912 bytes of the selected 16 KB bank.
    border_px
        Border thickness in pixels on each side. 0 disables the border.
    flash_phase
        0 = render flash attribute as-is (ink-on-paper), 1 = invert flash
        cells. The real ULA alternates at ~1.6 Hz.

    Returns
    -------
    ScreenImage with width = 256 + 2*border_px, height = 192 + 2*border_px.
    """
    bank = _selected_screen_bank(cp.chipset.p7ffd)
    bank_offset = bank * EMU_PAGE_SIZE
    if bank_offset + SCREEN_BYTES > len(ram):
        raise RenderError(
            f"materialized RAM is only {len(ram)} bytes; cannot read bank "
            f"{bank} (needs offset {bank_offset}..{bank_offset + SCREEN_BYTES})"
        )

    pixel_data = ram[bank_offset : bank_offset + PIXEL_BYTES]
    attr_data = ram[bank_offset + PIXEL_BYTES : bank_offset + SCREEN_BYTES]

    out_w = SCREEN_WIDTH + 2 * border_px
    out_h = SCREEN_HEIGHT + 2 * border_px
    buf = bytearray(out_w * out_h * 3)

    # Border fill — one color for the whole frame.
    border_color_index = cp.chipset.border_attr & 0b0000_0111
    border_rgb = PALETTE_NORMAL[border_color_index]
    for i in range(0, len(buf), 3):
        buf[i : i + 3] = border_rgb

    # Pixel decode.
    for y in range(SCREEN_HEIGHT):
        # Classic ULA pixel address calculation.
        addr = (
            ((y & 0xC0) << 5)
            | ((y & 0x07) << 8)
            | ((y & 0x38) << 2)
        )
        attr_row = (y // 8) * 32
        for x_byte in range(32):
            byte = pixel_data[addr + x_byte]
            attr = attr_data[attr_row + x_byte]
            ink = attr & 0x07
            paper = (attr >> 3) & 0x07
            bright = (attr >> 6) & 0x01
            flash = (attr >> 7) & 0x01
            palette = PALETTE_BRIGHT if bright else PALETTE_NORMAL
            fg_idx, bg_idx = (paper, ink) if (flash and flash_phase) else (ink, paper)
            fg = palette[fg_idx]
            bg = palette[bg_idx]
            # 8 horizontal pixels per byte, MSB first.
            base = ((y + border_px) * out_w + (x_byte * 8 + border_px)) * 3
            for bit in range(8):
                color = fg if (byte & (0x80 >> bit)) else bg
                buf[base : base + 3] = color
                base += 3

    return ScreenImage(width=out_w, height=out_h, rgb=bytes(buf))


def render_checkpoint(
    dump: TtdDump,
    cp: Checkpoint,
    out_path: str,
    border_px: int = BORDER_DEFAULT_PX,
    fmt: str = "auto",
) -> None:
    """Materialize RAM, decode the screen, and write to ``out_path``.

    ``fmt`` is one of ``"auto"`` (default — picks from extension), ``"png"``,
    or ``"ppm"``. PNG requires Pillow; PPM works with no third-party deps.
    """
    ram = dump.materialize_ram(cp)
    img = decode_screen_rgb(cp, ram, border_px=border_px)

    if fmt == "auto":
        fmt = "png" if out_path.lower().endswith(".png") else "ppm"

    if fmt == "png":
        img.write_png(out_path)
    elif fmt == "ppm":
        img.write_ppm(out_path)
    else:
        raise RenderError(f"unknown image format {fmt!r}")


def render_dirty_heatmap(
    dump: TtdDump,
    out_path: str,
    fmt: str = "auto",
) -> ScreenImage:
    """Render a one-pixel-per-checkpoint wide heatmap of dirty sub-pages.

    Useful for spotting capture gaps visually: a row of solid colour means
    the dirty tracker was inactive (bug); normal sessions show a noisy band.

    v2 layout: rows are individual 4 KB sub-pages
    (``model_ram_pages * SUB_PAGES_PER_EMU_PAGE`` rows tall). Each row of
    four corresponds to one 16 KB emulator page; faint horizontal dividers
    every 4 rows mark emu-page boundaries when viewed zoomed-out.

    Returns the produced image (also writes it to ``out_path``).
    """
    n_cps = len(dump.checkpoints)
    if n_cps == 0:
        raise RenderError("no checkpoints to render")

    # Heatmap dimensions: 1 px wide per checkpoint × (N sub-pages) high.
    # v2 stores 4 sub-slots per emu page, so height is 4× the v1 value.
    height = dump.header.model_ram_pages * SUB_PAGES_PER_EMU_PAGE
    buf = bytearray(n_cps * height * 3)

    for x, cp in enumerate(dump.checkpoints):
        # Walk the flat sub-slot list (length = model_ram_pages * 4).
        # The list is ordered (page, sub) — row 0..3 are page 0's sub-pages,
        # row 4..7 are page 1's sub-pages, and so on.
        for row, ref in enumerate(cp.ram_sub_slots):
            base = (row * n_cps + x) * 3
            if ref == NEVER_TOUCHED_SLOT:
                # Black = "never touched" — the sub-page is at session-start
                # content and the capture has nothing to replay.
                continue
            # Colour the pixel by low bits of the slot index. This produces
            # a colourful but stable colour per slot, making it easy to spot
            # "the same sub-page flickered between captures" patterns
            # visually. Clamp each channel to [0,255]: the formula
            # (n*32 + 32) gives 0..256 inclusive, which overflows at ref%8==7.
            r = min(255, (ref & 0x07) * 32 + 32)
            g = min(255, ((ref >> 3) & 0x07) * 32 + 32)
            b = min(255, ((ref >> 6) & 0x07) * 32 + 32)
            buf[base : base + 3] = (r, g, b)

    img = ScreenImage(width=n_cps, height=height, rgb=bytes(buf))

    if fmt == "auto":
        fmt = "png" if out_path.lower().endswith(".png") else "ppm"
    if fmt == "png":
        img.write_png(out_path)
    elif fmt == "ppm":
        img.write_ppm(out_path)

    return img
