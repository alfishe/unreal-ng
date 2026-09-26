// pixel-renderer.h - software blitter: renders TextScreen to ARGB32 pixel buffer.
#pragma once

#include <cstdint>
#include <vector>

#include "screen/palette.h"
#include "screen/textscreen.h"

namespace dbg {

class PixelRenderer {
public:
    PixelRenderer(int cellWidth, int cellHeight, int pixelWidth = 0, int pixelHeight = 0);

    void Resize(int pixelWidth, int pixelHeight);

    int PixelWidth() const { return pixelWidth_; }
    int PixelHeight() const { return pixelHeight_; }
    const std::vector<uint32_t>& Pixels() const { return pixels_; }

    // Render cells and 1-pixel hairline frames from screen into pixels_
    void Render(const TextScreen& screen, const Palette& palette);

private:
    void PutPixel(int x, int y, uint32_t color);

    int cellWidth_;
    int cellHeight_;
    int pixelWidth_;
    int pixelHeight_;
    std::vector<uint32_t> pixels_;
};

}  // namespace dbg
