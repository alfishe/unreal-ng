#pragma once

#include <cstdint>
#include <vector>

#include "emulator/video/smoothmotion.h"

/// Smooth motion's worst-case scenes, shared by the CPU tests (smoothmotion_test.cpp) and the GPU ones
/// (unreal-qt/tests/qt/smoothmotiongpu_test.cpp): the content that costs LayerFlow the most per pixel
namespace smoothscenes
{
inline uint32_t Hash(uint32_t x, uint32_t y, uint32_t s)
{
    uint32_t h = x * 0x9E3779B1u ^ y * 0x85EBCA77u ^ s * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

/// Frame k of sparse dots over black paper. Blocks: per 8 x 8 block a layer of its own, moving by its own vector (48
/// vectors, neighbours differ): more vectors than LayerFlow::kMaxVectors. Layered: 16 layers over every pixel, each
/// its own vector, so every still pixel asks the cover rule about many moving vectors
inline std::vector<uint32_t> Dots(int width, int height, int k, bool layered)
{
    using V = smoothmotion::MotionField::Vector;
    std::vector<V> vectors;
    for (int y = -3; y <= 3; y++)
        for (int x = -3; x <= 3; x++)
            if (x != 0 || y != 0)
                vectors.push_back({x * 2, y});
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height, 0xFF000000u);
    const uint32_t ink[4] = {0xFFFFFFFFu, 0xFF00D7D7u, 0xFFD700D7u, 0xFFD7D700u};
    const auto dot = [&](int x, int y, size_t layer, uint32_t every) {
        const V v = vectors[layer];
        const uint32_t h = Hash(static_cast<uint32_t>(x - v.x * k + 4096), static_cast<uint32_t>(y - v.y * k + 4096),
                                static_cast<uint32_t>(layer));
        if (h % every == 0)
            frame[static_cast<size_t>(y) * width + x] = ink[(h >> 8) & 3];
    };
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            if (layered)
                for (size_t layer = 0; layer < 16; layer++)
                    dot(x, y, layer * 3 % vectors.size(), 40);
            else
                dot(x, y, static_cast<size_t>(x / 8 * 7 + y / 8 * 3) % vectors.size(), 9);
    return frame;
}
/// The scene set the layers' rules are judged by (presenter stabilization README, section 1.6: content not covered).
/// ZX-like pictures: the 15 colors, a cell of ink and paper per 8 x 8 (8 x 1 in multicolor). Every generator takes
/// the frame's number `k` (>= 0) and returns RGBA8 pixels, row by row

/// ZX color `index` 0..7 (black, blue, red, magenta, green, cyan, yellow, white), bright or not
inline uint32_t ZxColor(int index, bool bright)
{
    const uint32_t v = bright ? 0xFFu : 0xD7u;
    const uint32_t r = (index & 2) ? v : 0, g = (index & 4) ? v : 0, b = (index & 1) ? v : 0;
    return 0xFF000000u | b << 16 | g << 8 | r;
}

/// A cell's attribute: ink and paper indices (never equal) and the bright bit
struct ZxAttribute
{
    int ink = 7;
    int paper = 0;
    bool bright = false;
};

inline ZxAttribute ZxAttributeAt(int cx, int cy, uint32_t seed)
{
    const uint32_t h = Hash(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), seed ^ 0xA77Bu);
    ZxAttribute attr;
    attr.paper = static_cast<int>(h & 7);
    attr.ink = (attr.paper + 1 + static_cast<int>((h >> 3) % 7)) & 7;
    attr.bright = (h >> 8) & 1;
    return attr;
}

/// The bitmap bit of a ZX picture: about 3/8 ink
inline bool ZxInkAt(int x, int y, uint32_t seed)
{
    return Hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed) % 8 < 3;
}

/// A pixel of a ZX picture (x, y >= 0), its attribute cells 8 x `cellHeight`
inline uint32_t ZxPixel(int x, int y, uint32_t seed, int cellHeight = 8)
{
    const ZxAttribute attr = ZxAttributeAt(x / 8, y / cellHeight, seed);
    return ZxColor(ZxInkAt(x, y, seed) ? attr.ink : attr.paper, attr.bright);
}

/// Rows 0..kStatusRows - 1: a still status bar; the rest scrolls `step` pixels a frame to the left, beyond the
/// search range (MotionField::kRangeFrames = 8) for step > 8
constexpr int kStatusRows = 16;
inline std::vector<uint32_t> FastScroll(int width, int height, int k, int step)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            frame[static_cast<size_t>(y) * width + x] =
                y < kStatusRows ? ZxPixel(x, y, 11) : ZxPixel(x + step * k, y, 12);
    return frame;
}

/// A ZX fade: every attribute's ink and paper index clamped to `level` (7 .. 0), bright only at level 7. A color
/// at or below the level stays; the rest steps down to colors other cells already show
inline std::vector<uint32_t> Fade(int width, int height, int level)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            const ZxAttribute attr = ZxAttributeAt(x / 8, y / 8, 13);
            const int index = ZxInkAt(x, y, 13) ? attr.ink : attr.paper;
            const bool bright = attr.bright && level >= 7;
            frame[static_cast<size_t>(y) * width + x] = ZxColor(index < level ? index : level, bright);
        }
    return frame;
}

/// A still ZX picture inside a border `border` pixels wide whose color changes line by line. `loader`: each line
/// red or cyan at random, new every frame (a tape loader: no motion); otherwise stripes 4 lines high in the 8 colors
/// moving down 3 lines a frame (a border effect)
inline bool InPaper(int width, int height, int border, int x, int y)
{
    return x >= border && x < width - border && y >= border && y < height - border;
}
inline std::vector<uint32_t> Border(int width, int height, int border, int k, bool loader)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++)
    {
        const uint32_t noise = Hash(7, static_cast<uint32_t>(y), static_cast<uint32_t>(k));
        const int index = loader ? ((noise & 1) ? 2 : 5) : ((y - 3 * k + 4096) / 4) & 7;
        const uint32_t line = ZxColor(index, false);
        for (int x = 0; x < width; x++)
            frame[static_cast<size_t>(y) * width + x] =
                InPaper(width, height, border, x, y) ? ZxPixel(x - border, y - border, 14) : line;
    }
    return frame;
}

/// Multicolor: a cell of ink and paper per 8 x 1. `cycle`: the colors other than black step round 1..7 every frame
/// (color cycling, nothing moves); otherwise every frame the same. `counter`: a 16 x 8 box at the top left whose
/// bitmap is new every frame (a score counting), so a frame differs from the one before
inline std::vector<uint32_t> Multicolor(int width, int height, int k, bool cycle, bool counter)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height);
    const auto step = [&](int index) { return cycle && index > 0 ? (index - 1 + k) % 7 + 1 : index; };
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            const ZxAttribute attr = ZxAttributeAt(x / 8, y, 15);
            uint32_t color = ZxColor(step(ZxInkAt(x, y, 15) ? attr.ink : attr.paper), attr.bright);
            if (counter && x < 16 && y < 8)
                color = ZxColor(ZxInkAt(x, y, 100 + static_cast<uint32_t>(k)) ? 7 : 0, true);
            frame[static_cast<size_t>(y) * width + x] = color;
        }
    return frame;
}

/// Interlace. `jitter` false: the even lines show picture A and the odd ones picture B, swapped every other frame;
/// true: one picture moved down a line every other frame (the even and odd fields of a 25 Hz interlace)
inline std::vector<uint32_t> Interlace(int width, int height, int k, bool jitter)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            frame[static_cast<size_t>(y) * width + x] =
                jitter ? ZxPixel(x, y + 1 - (k & 1), 16) : ZxPixel(x, y, ((y + k) & 1) ? 17 : 18);
    return frame;
}
}  // namespace smoothscenes
