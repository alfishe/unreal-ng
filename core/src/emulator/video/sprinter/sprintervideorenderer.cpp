#include "sprintervideorenderer.h"

#include <algorithm>

#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
constexpr uint32_t kVramMask = static_cast<uint32_t>(SprinterVideoRam::kSize - 1);

inline uint32_t Wrap(int32_t value, uint32_t modulus)
{
    const int32_t m = static_cast<int32_t>(modulus);
    const int32_t r = value % m;
    return static_cast<uint32_t>(r < 0 ? r + m : r);
}

inline const uint8_t* ModeBytes(const SprinterVideoInputs& in, uint32_t a16, uint32_t b8)
{
    return in.vram + SprinterVideoRam::ModeAddress(static_cast<uint8_t>(a16 >> 4), static_cast<uint8_t>(b8 >> 3), in.modePage);
}
}  // namespace

bool SprinterSquare::TextCode(const uint8_t* line1, unsigned half, uint8_t& code)
{
    code = 0x20;
    const Kind kind = Classify(line1[0]);
    if (kind != Kind::Text40 && kind != Kind::Text80)
        return false;
    if (half == 0)
    {
        code = line1[1];
        return true;
    }
    if (kind == Kind::Text40)
        return false;  // a 40-column character is 16 pixels wide: the right half is its own
    const uint8_t* line2 = line1 + SprinterVideoRam::kRowBytes;
    const Kind right = Classify(line2[0]);
    if (right != Kind::Text40 && right != Kind::Text80)
        return false;
    code = line2[1];
    return true;
}

char SprinterSquare::Letter() const
{
    switch (kind)
    {
        case Kind::Graphics320: return 'G';
        case Kind::Graphics640: return 'g';
        case Kind::Text40: return 'T';
        case Kind::Text80: return 't';
        case Kind::Border: return 'B';
        default: return IntArmed() ? '*' : '.';
    }
}

const char* SprinterSquare::Key(Kind kind)
{
    switch (kind)
    {
        case Kind::Graphics320: return "graphics_320";
        case Kind::Graphics640: return "graphics_640";
        case Kind::Text40: return "text_40";
        case Kind::Text80: return "text_80";
        case Kind::Border: return "border";
        default: return "blank";
    }
}

const char* SprinterSquare::Name(Kind kind)
{
    switch (kind)
    {
        case Kind::Graphics320: return "graphics 320 x 256, 256 colors";
        case Kind::Graphics640: return "graphics 640 x 256, 16 colors";
        case Kind::Text40: return "text, 40 columns";
        case Kind::Text80: return "text, 80 columns";
        case Kind::Border: return "border";
        default: return "blank";
    }
}

const SprinterVideoRenderer& SprinterVideoRenderer::Standard()
{
    static const SprinterVideoRenderer standard;
    return standard;
}

uint32_t SprinterVideoRenderer::A16(const SprinterVideoInputs& in, uint32_t x)
{
    return Wrap(static_cast<int32_t>(x) - static_cast<int32_t>(kBorderLeft) - in.holdX, kLinePixels);
}

uint32_t SprinterVideoRenderer::B8(const SprinterVideoInputs& in, uint32_t y)
{
    return Wrap(static_cast<int32_t>(y) - static_cast<int32_t>(kBorderTop) - in.holdY, in.lines ? in.lines : 320u);
}

uint32_t SprinterVideoRenderer::GraphicsAddress(const uint8_t* mode, uint32_t sub, uint32_t row)
{
    // MAME draw_tile (sprinter.cpp:430-451); the source row is an 8-bit value in
    // MAME, the sum can pass 255 there: the 18-bit video address wraps
    const bool lowres = (mode[2] & 0x04) != 0;
    uint32_t x = (static_cast<uint32_t>(mode[0] & 0x0F) << 6) | (static_cast<uint32_t>(mode[1] & 0x07) << 3);
    uint32_t y = static_cast<uint32_t>(mode[1] >> 3) << 3;
    if (lowres)
    {
        x += 4u * (mode[2] & 0x01);
        y = (y + 4u * ((mode[2] >> 1) & 0x01)) & 0xFFu;
    }
    const uint32_t line = y + (row >> (lowres ? 1 : 0));
    return (line * SprinterVideoRam::kRowBytes + x + (sub >> (lowres ? 2 : 1))) & kVramMask;
}

uint32_t SprinterVideoRenderer::GraphicsPen(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t sub, uint32_t row)
{
    const uint32_t palette = static_cast<uint32_t>(mode[0] >> 6) << 8;
    const uint8_t color = in.vram[GraphicsAddress(mode, sub, row)];
    if (mode[0] & 0x20)
        return palette + color;                                  // 320: a byte per 2 pixels
    return palette + ((sub & 1) ? (color & 0x0F) : (color >> 4));  // 640: the high nibble first
}

uint32_t SprinterVideoRenderer::FontAddress(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t row)
{
    return (static_cast<uint32_t>(mode[1]) << 10) | (static_cast<uint32_t>(mode[0] & 0x0F) << 6) |
           (static_cast<uint32_t>(in.textPage & 1) << 5) | (static_cast<uint32_t>(mode[0] >> 6) << 3) | (row & 7);
}

uint32_t SprinterVideoRenderer::AttrAddress(const SprinterVideoInputs& in, const uint8_t* mode)
{
    return (static_cast<uint32_t>(mode[2]) << 10) | (static_cast<uint32_t>(mode[0] & 0x0F) << 6) |
           (static_cast<uint32_t>(in.textPage & 1) << 5) | 0x18u | static_cast<uint32_t>(mode[0] >> 6);
}

uint32_t SprinterVideoRenderer::SymbolPen(const SprinterVideoInputs& in, const uint8_t* line1, uint32_t sub, uint32_t row)
{
    // MAME draw_symbol (sprinter.cpp:453-497): a 640 square's right half takes
    // every byte from Line2 (its own Mode0 included)
    const uint8_t* mode = SymbolMode(line1, sub);
    const uint8_t m0 = mode[0];
    if ((m0 & 0xFC) == 0xFC)
        return kPenText;                          // blank: text paper colour 0
    if ((m0 >> 5) == 7)
        return kPenText | (static_cast<uint32_t>(in.border & 7) * 9u);  // border
    const uint8_t attr = in.vram[AttrAddress(in, mode)];
    const uint8_t symbol = in.vram[FontAddress(in, mode, row)];
    const uint32_t bit = 1u << (7 - ((sub >> ((m0 >> 5) & 1)) & 7));
    return kPenText + attr + ((symbol & bit) ? 0x100u : 0u) + (in.flash ? 0x200u : 0u);
}

uint32_t SprinterVideoRenderer::PenAt(const SprinterVideoInputs& in, uint32_t x, uint32_t y)
{
    const uint32_t a16 = A16(in, x);
    const uint32_t b8 = B8(in, y);
    const uint8_t* mode = ModeBytes(in, a16, b8);
    return (mode[0] & 0x10) ? SymbolPen(in, mode, a16 & 15, b8 & 7) : GraphicsPen(in, mode, a16 & 15, b8 & 7);
}

void SprinterVideoRenderer::DrawSpan(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out) const
{
    // Naive v1 (performance guidelines rule 5): the mode bytes are read once per
    // square segment, the pixel's bytes per pixel. The idea of caching decoded
    // squares (MAME's tilemap) is in the Sprinter TODO with a benchmark
    const uint32_t b8 = B8(in, y);
    const uint32_t row = b8 & 7;
    uint32_t x = x0;
    while (x < x1)
    {
        const uint32_t a16 = A16(in, x);
        const uint32_t sub0 = a16 & 15;
        const uint32_t end = std::min(x1, x + (16 - sub0));
        const uint8_t* mode = ModeBytes(in, a16, b8);
        const bool symbol = (mode[0] & 0x10) != 0;
        for (uint32_t sub = sub0; x < end; x++, sub++)
        {
            const uint32_t pen = symbol ? SymbolPen(in, mode, sub, row) : GraphicsPen(in, mode, sub, row);
            *out++ = in.palette[pen & (SprinterVideoRam::kPens - 1)];
        }
    }
}
