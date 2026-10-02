// eve-emu - rotate, swizzle, CSPREAD shift, host frame buffer (spec §6.8).
#include "eve-render.h"
#include "eve-simd.h"

namespace EveLib
{

namespace
{

constexpr uint32_t kRotateInverted = 1;     // REG_ROTATE bit 0: 180 degrees
constexpr uint32_t kRotatePortrait = 2;     // bit 1: axes swapped (drawn as landscape)
constexpr uint32_t kRotateMirrored = 4;     // bit 2: mirrored
constexpr uint32_t kSwizzleReverseBits = 1; // REG_SWIZZLE bit 0
constexpr uint32_t kSwizzleOrderShift = 1;  // bits 3:1 select the channel order
constexpr uint32_t kSwizzleOrderMask = 7;
constexpr uint32_t kRed = 0, kGreen = 1, kBlue = 2;
constexpr uint32_t kOpaque = 0xFF000000;
constexpr uint32_t kRedShift = 16, kGreenShift = 8;
constexpr uint32_t kBitsPerChannel = 8;

// Source channel on the R, G, B pins per REG_SWIZZLE bits 3:1 [DS Table 4-12].
constexpr uint8_t kSwizzleOrder[kSwizzleOrderMask + 1][3] = {
    {kRed, kGreen, kBlue},  // 000
    {kBlue, kGreen, kRed},  // 001
    {kRed, kGreen, kBlue},  // 010 (bit 2 matters only with bit 3)
    {kBlue, kGreen, kRed},  // 011
    {kBlue, kRed, kGreen},  // 100
    {kGreen, kRed, kBlue},  // 101
    {kGreen, kBlue, kRed},  // 110
    {kRed, kBlue, kGreen},  // 111
};

uint8_t ReverseBits(uint8_t v)
{
    uint8_t out = 0;
    for (uint32_t i = 0; i < kBitsPerChannel; ++i)
        out = static_cast<uint8_t>(out | (((v >> i) & 1) << (kBitsPerChannel - 1 - i)));
    return out;
}

} // namespace

bool Portrait(const EveChip& chip)
{
    return (RegGet(chip, Reg::Rotate) & kRotatePortrait) != 0;
}

uint32_t LogicalLine(const EveChip& chip, uint32_t screenLine, bool& mirrorX)
{
    // Landscape orientations: flips of the line order and of x (BT8XX, golden rotate-*).
    const uint32_t rotate = RegGet(chip, Reg::Rotate) & ~kRotatePortrait;
    const uint32_t vsize = RegGet(chip, Reg::Vsize);
    const bool inverted = (rotate & kRotateInverted) != 0;
    const bool mirrored = (rotate & kRotateMirrored) != 0;
    mirrorX = inverted != mirrored;
    return inverted && screenLine < vsize ? vsize - 1 - screenLine : screenLine;
}

void OutputLine(EveChip& chip, uint32_t screenLine, const uint8_t* color, uint32_t width, bool mirrorX)
{
    if (chip.framebuffer == nullptr || screenLine >= chip.heightCapacity)
        return;
    const uint32_t swizzle = RegGet(chip, Reg::Swizzle);
    const uint8_t* order = kSwizzleOrder[(swizzle >> kSwizzleOrderShift) & kSwizzleOrderMask];
    const bool reverse = (swizzle & kSwizzleReverseBits) != 0;
    const bool spread = (RegGet(chip, Reg::Cspread) & 1) != 0;
    uint32_t* row = chip.framebuffer + static_cast<size_t>(screenLine) * chip.stridePixels;
    const uint32_t count = width < chip.widthCapacity ? width : chip.widthCapacity;
    // The usual case - no mirror, no shift, pins in order: a straight conversion
    if (!mirrorX && !spread && !reverse && order[0] == kRed && order[1] == kGreen && order[2] == kBlue)
    {
        Simd::RgbaToArgb(color, row, count);
        return;
    }
    auto pixelAt = [&](int64_t screenX) -> const uint8_t* {
        const int64_t logical = mirrorX ? static_cast<int64_t>(width) - 1 - screenX : screenX;
        return color + kChannels * static_cast<size_t>(logical);
    };
    for (uint32_t x = 0; x < count; ++x)
    {
        const uint8_t* own = pixelAt(x);
        uint8_t rgb[3] = {own[kRed], own[kGreen], own[kBlue]};
        if (spread)
        {
            // CSPREAD = 1: red changes a clock early and blue a clock late; VDAC2 latches all
            // pins on one edge, so a pixel shows the next pixel's red and the previous
            // pixel's blue (spec §3.2).
            if (x + 1 < width)
                rgb[kRed] = pixelAt(x + 1)[kRed];
            else if (!kCspreadEdgeKeepsOwn)
                rgb[kRed] = 0;
            if (x > 0)
                rgb[kBlue] = pixelAt(static_cast<int64_t>(x) - 1)[kBlue];
            else if (!kCspreadEdgeKeepsOwn)
                rgb[kBlue] = 0;
        }
        uint8_t pins[3] = {rgb[order[0]], rgb[order[1]], rgb[order[2]]};
        if (reverse)
            for (uint8_t& p : pins)
                p = ReverseBits(p);
        row[x] = kOpaque | (static_cast<uint32_t>(pins[kRed]) << kRedShift) |
                 (static_cast<uint32_t>(pins[kGreen]) << kGreenShift) | pins[kBlue];
    }
}

void OutputPortraitLine(EveChip& chip, uint32_t logicalLine, const uint8_t* color, uint32_t width)
{
    // Logical (x, y) to the screen: 2: (y, H-1-x), 3: (W-1-y, x), 6: (W-1-y, H-1-x), 7: (y, x).
    if (chip.framebuffer == nullptr)
        return;
    const uint32_t rotate = RegGet(chip, Reg::Rotate);
    const uint32_t screenWidth = RegGet(chip, Reg::Hsize);
    const uint32_t screenHeight = RegGet(chip, Reg::Vsize);
    const bool inverted = (rotate & kRotateInverted) != 0;
    const bool mirrored = (rotate & kRotateMirrored) != 0;
    const bool flipColumns = inverted != mirrored;   // screen x = W-1-y
    const bool flipRows = !inverted;                 // screen y = H-1-x
    const uint32_t sx = flipColumns ? screenWidth - 1 - logicalLine : logicalLine;
    if (sx >= chip.widthCapacity || logicalLine >= screenWidth)
        return;
    const uint32_t swizzle = RegGet(chip, Reg::Swizzle);
    const uint8_t* order = kSwizzleOrder[(swizzle >> kSwizzleOrderShift) & kSwizzleOrderMask];
    const bool reverse = (swizzle & kSwizzleReverseBits) != 0;
    for (uint32_t x = 0; x < width && x < screenHeight; ++x)
    {
        const uint32_t sy = flipRows ? screenHeight - 1 - x : x;
        if (sy >= chip.heightCapacity)
            continue;
        const uint8_t* p = color + kChannels * static_cast<size_t>(x);
        uint8_t pins[3] = {p[order[0]], p[order[1]], p[order[2]]};
        if (reverse)
            for (uint8_t& pin : pins)
                pin = ReverseBits(pin);
        chip.framebuffer[static_cast<size_t>(sy) * chip.stridePixels + sx] =
            kOpaque | (static_cast<uint32_t>(pins[kRed]) << kRedShift) |
            (static_cast<uint32_t>(pins[kGreen]) << kGreenShift) | pins[kBlue];
    }
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

void EveSetOutput(EveChip* chip, uint32_t* framebuffer, uint32_t stridePixels, uint32_t widthCapacity,
                  uint32_t heightCapacity, int drawing)
{
    CatchUp(*chip);
    chip->framebuffer = framebuffer;
    chip->stridePixels = stridePixels;
    chip->widthCapacity = widthCapacity;
    chip->heightCapacity = heightCapacity;
    chip->drawing = (drawing != 0 && framebuffer != nullptr) ? 1 : 0;
}

} // extern "C"
