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

/// Sparse dots over black paper, each layer's own (DotsOf's kinds)
enum class DotsKind
{
    Blocks,     ///< per 8 x 8 block a layer of its own (48 vectors, neighbors differ)
    Layered,    ///< 16 layers over every pixel, each its own vector
    OverLayers  ///< three layers in bands of 3 block rows, every fourth block (diagonals) a layer of its own
};
inline std::vector<uint32_t> DotsOf(int width, int height, int k, DotsKind kind)
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
    const int blocksWide = (width + 7) / 8;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            if (kind == DotsKind::Layered)
                for (size_t layer = 0; layer < 16; layer++)
                    dot(x, y, layer * 3 % vectors.size(), 40);
            else if (kind == DotsKind::Blocks)
                dot(x, y, static_cast<size_t>(x / 8 * 7 + y / 8 * 3) % vectors.size(), 9);
            else if ((x / 8 + y / 8) % 4 == 0)  // denser: a block of its own matches its vector, not a chance one
                dot(x, y, static_cast<size_t>((y / 8 * blocksWide + x / 8) / 4 * 5) % vectors.size(), 5);
            else
                dot(x, y, static_cast<size_t>(y / 24 % 3 * 17 + 5), 9);
    return frame;
}
/// Frame k of sparse dots over black paper. Blocks: per 8 x 8 block a layer of its own, moving by its own vector (48
/// vectors, neighbors differ): more vectors than LayerFlow::kMaxVectors. Layered: 16 layers over every pixel, each
/// its own vector, so every still pixel asks the cover rule about many moving vectors
inline std::vector<uint32_t> Dots(int width, int height, int k, bool layered)
{
    return DotsOf(width, height, k, layered ? DotsKind::Layered : DotsKind::Blocks);
}
/// Frame k of dots past LayerFlow's cap that is not a cut: three layers hold three quarters of the blocks, every
/// fourth block moves its own way (more vectors than LayerFlow::kMaxVectors). A picture of blocks each moving its own
/// way is a cut (LayerFlow::kCutLayers): no layer moves
inline std::vector<uint32_t> DotsOverLayers(int width, int height, int k)
{
    return DotsOf(width, height, k, DotsKind::OverLayers);
}
/// Frame k (0..3) of layers LayerFlow's rules work on: a row of dots moving by its period (the still look-alike ties
/// with the motion, the row's ends tell), dashes of period 8 moving by 1 over a still pattern (+1 and -7 alike), a
/// sprite over paper (its holes: cover), a scrolled band and a blinking cell. At least 58 x 46 pixels
inline std::vector<uint32_t> LayerScene(int width, int height, int k)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height, 0xFF202020u);
    const auto at = [&](int x, int y) -> uint32_t& { return frame[static_cast<size_t>(y) * width + x]; };
    const auto pattern = [](int x, int y) {
        return Hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0) | 0xFF000000u;
    };
    for (int y = 4; y < 6; y++)
        for (int i = 0; i < 12; i++)
            at(8 + 4 * k + 4 * i, y) = 0xFFE0E0E0u;
    for (int y = 12; y < 20; y++)
        for (int x = 0; x < width; x++)
        {
            if ((x * 7 + y * 3) % 11 == 0)
                at(x, y) = 0xFF4080C0u;
            if ((x - k) % 8 < 3 && (x - k) >= 0)
                at(x, y) = 0xFFF0C040u;
        }
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            if ((x + y) % 3 != 0)
                at(30 + 6 * k + x, 30 + 2 * k + y) = pattern(x + 9000, y + 9000);
    for (int y = height - 16; y < height - 6; y++)
        for (int x = 0; x < width; x++)
            at(x, y) = pattern(x + 3 * k, y) & 0xFF7F7F7Fu;
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
            at(width - 10 + x, 30 + y) = k % 2 ? 0xFFFF0000u : 0xFF0000FFu;
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

/// A page of text: per 8 x 8 cell a letter of an 8-letter alphabet or a space, white ink on blue paper (a glyph's
/// last column and row blank, as a font's). `page` picks the letters: a page flip changes them in place, and with so
/// few letters many a cell's new letter is the old one of a cell next to it (a block matched one cell over)
inline std::vector<uint32_t> TextPage(int width, int height, int page)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            const uint32_t letter = Hash(static_cast<uint32_t>(x / 8), static_cast<uint32_t>(y / 8),
                                         0x7E47u + static_cast<uint32_t>(page)) %
                                    10;
            const int gx = x % 8, gy = y % 8;
            const bool ink =
                letter < 8 && gx < 7 && gy < 7 && Hash(letter, static_cast<uint32_t>(gy * 8 + gx), 0x6C1Fu) % 8 < 3;
            frame[static_cast<size_t>(y) * width + x] = ZxColor(ink ? 7 : 1, false);
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
