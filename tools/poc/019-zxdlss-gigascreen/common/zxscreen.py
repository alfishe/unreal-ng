"""ZX Spectrum screen memory: layout, decode, emulator palette."""
import os

import numpy as np
import zstandard

# unreal-ng palette (core/src/emulator/video/zx/screenzx.cpp, ABGR little-endian) -> RGB,
# index = bright * 8 + color
_ABGR = [0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
         0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF]
ZX_RGB = np.array([[v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF] for v in _ABGR], np.uint8)

# bitmap byte offset of (line y, column byte x)
_Y = np.arange(192)
LINE_OFFSET = ((_Y & 0xC0) << 5) | ((_Y & 0x07) << 8) | ((_Y & 0x38) << 2)
BITMAP_INDEX = LINE_OFFSET[:, None] + np.arange(32)[None, :]              # 192 x 32
ATTR_INDEX = 6144 + (_Y[:, None] >> 3) * 32 + np.arange(32)[None, :]      # 192 x 32 (per 8x1 segment)


def segments(screen):
    """6912-byte screen -> (bitmap 192x32, attribute 192x32) per 8x1 segment."""
    return screen[BITMAP_INDEX], screen[ATTR_INDEX]


def ink_paper(attr):
    """attribute bytes -> (ink index, paper index) in 0..15 (FLASH ignored)."""
    bright = (attr >> 6) & 1
    return (attr & 7) + 8 * bright, ((attr >> 3) & 7) + 8 * bright


def decode(screen):
    """6912-byte screen -> 192 x 256 ZX color indices (0..15)."""
    bitmap, attr = segments(screen)
    bits = np.unpackbits(bitmap[..., None], axis=2).reshape(192, 256).astype(bool)
    ink, paper = ink_paper(attr)
    return np.where(bits, np.repeat(ink, 8, axis=1), np.repeat(paper, 8, axis=1)).astype(np.uint8)


class Screens:
    """Raw screens of a clip (capture/extract_screens.py): frame i -> (page5, page7) 6912 bytes each."""

    def __init__(self, clip_path, chunk):
        self.path, self.chunk = clip_path, chunk
        self._dctx = zstandard.ZstdDecompressor()
        self._id, self._cache = None, None

    def pages(self, i):
        cid = i // self.chunk
        if cid != self._id:
            raw = self._dctx.decompress(open(os.path.join(self.path, f"screens_{cid:04d}.zst"), "rb").read())
            self._cache, self._id = np.frombuffer(raw, np.uint8).reshape(-1, 2, 6912), cid
        return self._cache[i % self.chunk]

    def displayed(self, i, active_screen):
        """The screen the ULA shows: page 5 (normal) or page 7 (shadow)."""
        return self.pages(i)[1 if active_screen else 0]
