// pixel-renderer.cpp - software blitter implementation.
#include "gui/pixel-renderer.h"

#include "screen/font16.h"

namespace dbg {

PixelRenderer::PixelRenderer(int cellWidth, int cellHeight, int pixelWidth, int pixelHeight)
    : cellWidth_(cellWidth),
      cellHeight_(cellHeight),
      pixelWidth_(pixelWidth > 0 ? pixelWidth : cellWidth * 8),
      pixelHeight_(pixelHeight > 0 ? pixelHeight : cellHeight * 16),
      pixels_(static_cast<size_t>(pixelWidth_ * pixelHeight_), 0xFF000000u) {}

void PixelRenderer::Resize(int pixelWidth, int pixelHeight) {
    if (pixelWidth <= 0) pixelWidth = cellWidth_ * 8;
    if (pixelHeight <= 0) pixelHeight = cellHeight_ * 16;
    if (pixelWidth != pixelWidth_ || pixelHeight != pixelHeight_) {
        pixelWidth_ = pixelWidth;
        pixelHeight_ = pixelHeight;
        pixels_.assign(static_cast<size_t>(pixelWidth_ * pixelHeight_), 0xFF000000u);
    }
}

void PixelRenderer::PutPixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < pixelWidth_ && y >= 0 && y < pixelHeight_) {
        pixels_[static_cast<size_t>(y * pixelWidth_ + x)] = color;
    }
}

void PixelRenderer::Render(const TextScreen& screen, const Palette& palette) {
    // 1. Precalculate 32-bit ARGB colors for the 16 palette entries.
    //    The second set draws the grayed cells and frames (TextScreen::GrayExcept).
    uint32_t pal32[16];
    uint32_t gray32[16];
    const auto argb = [](Rgb c) {
        return (0xFFu << 24) | (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) |
               static_cast<uint32_t>(c.b);
    };
    for (int i = 0; i < 16; ++i) {
        pal32[i] = argb(palette.ColorOf(i));
        gray32[i] = argb(GrayOf(palette.ColorOf(i)));
    }

    // 2. Render text cells.
    for (int cy = 0; cy < cellHeight_; ++cy) {
        const int y0 = (cy * pixelHeight_) / cellHeight_;
        const int y1 = ((cy + 1) * pixelHeight_) / cellHeight_;
        const int cellH = y1 - y0;

        for (int cx = 0; cx < cellWidth_; ++cx) {
            const uint8_t attr = screen.AttrAt(cx, cy);
            if (attr == kAttrTransparent) {
                continue;
            }
            const uint8_t code = screen.CharAt(cx, cy);
            const uint32_t* pal = screen.Grayed(cx, cy) ? gray32 : pal32;
            const uint32_t ink = pal[InkIndex(attr)];
            const uint32_t paper = pal[PaperIndex(attr)];
            const uint8_t* glyph = &kFont16[static_cast<size_t>(code) * 16];

            const int x0 = (cx * pixelWidth_) / cellWidth_;
            const int x1 = ((cx + 1) * pixelWidth_) / cellWidth_;
            const int cellW = x1 - x0;

            const bool isChecker = (code == 0xB1);

            for (int dy = 0; dy < cellH; ++dy) {
                const int py = y0 + dy;
                const int fontRow = (dy * 16) / cellH;
                const uint8_t bits = glyph[fontRow];
                uint32_t* dst = &pixels_[static_cast<size_t>(py * pixelWidth_ + x0)];

                if (isChecker) {
                    // Global screen-aligned checkerboard pattern: completely uniform
                    // across the entire window, with no moire or phase boundaries.
                    for (int dx = 0; dx < cellW; ++dx) {
                        const int px = x0 + dx;
                        dst[dx] = ((px + py) & 1) ? ink : paper;
                    }
                } else {
                    for (int dx = 0; dx < cellW; ++dx) {
                        const int fontCol = (dx * 8) / cellW;
                        const bool bit = (bits & (0x80 >> fontCol)) != 0;
                        dst[dx] = bit ? ink : paper;
                    }
                }
            }
        }
    }

    // 3. Render 1-pixel hairline frames.
    for (size_t i = 0; i < screen.Frames().size(); ++i) {
        const FrameEntry& frame = screen.Frames()[i];
        const bool grayed = screen.FrameGrayed(i);
        const uint32_t col = (grayed ? gray32 : pal32)[(frame.color | 8) & 0x0F];
        const int x1 = (frame.x * pixelWidth_) / cellWidth_ - 1;
        const int x2 = ((frame.x + frame.w) * pixelWidth_) / cellWidth_;
        const int y1 = (frame.y * pixelHeight_) / cellHeight_ - 1;
        const int y2 = ((frame.y + frame.h) * pixelHeight_) / cellHeight_;
        // A grayed frame stays under the dialog: not drawn over its cells
        const auto put = [&](int x, int y) {
            if (grayed && x >= 0 && y >= 0 &&
                !screen.Grayed((x * cellWidth_) / pixelWidth_, (y * cellHeight_) / pixelHeight_))
                return;
            PutPixel(x, y, col);
        };

        for (int xx = x1; xx <= x2; ++xx) {
            put(xx, y1);
            put(xx, y2);
        }
        for (int yy = y1 + 1; yy < y2; ++yy) {
            put(x1, yy);
            put(x2, yy);
        }
    }
}

}  // namespace dbg
