// SprinterVideoRenderer::DrawSpan draws by square segments (a graphics segment steps its source address, a symbol
// unit reads its attribute and font byte once): the same pixels as the per-pixel rules (GraphicsPen, SymbolPen),
// plane B included. Random video RAM - every square kind, low-res, both 640 halves from different Line2 bytes -
// random inputs (mode / text page, border, flash, HOLD, 320 / 312 lines, a font latch) and random span cuts.

#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <random>
#include <vector>

#include "emulator/video/screen.h"
#include "emulator/video/sprinter/sprintervideoram.h"
#include "emulator/video/sprinter/sprintervideorenderer.h"

namespace
{
using R = SprinterVideoRenderer;

/// The pixel by the per-pixel rules: what DrawSpan drew before it went by segments
uint32_t ReferencePen(const SprinterVideoInputs& in, uint32_t x, uint32_t y)
{
    const uint32_t a16 = R::A16(in, x);
    const uint32_t b8 = R::B8(in, y);
    const uint8_t* mode = in.vram + SprinterVideoRam::ModeAddress(static_cast<uint8_t>(a16 >> 4), static_cast<uint8_t>(b8 >> 3), in.modePage);
    if (!SprinterSquare::IsSymbol(mode[0]))
        return R::GraphicsPen(in, mode, a16 & 15, b8 & 7);
    const SprinterVideoInputs::FontLatch& latch = in.fontLatch;
    const int font = (latch.line == y && x >= latch.x0 && x < latch.x1) ? latch.font : -1;
    return R::SymbolPen(in, mode, a16 & 15, b8 & 7, font);
}

/// Plane B of a symbol pixel by the same rules as the renderer's own (ZX DLSS)
uint16_t ReferencePlaneB(const SprinterVideoInputs& in, uint32_t x, uint32_t y)
{
    const uint32_t a16 = R::A16(in, x);
    const uint32_t b8 = R::B8(in, y);
    const uint8_t* line1 = in.vram + SprinterVideoRam::ModeAddress(static_cast<uint8_t>(a16 >> 4), static_cast<uint8_t>(b8 >> 3), in.modePage);
    if (!SprinterSquare::IsSymbol(line1[0]))
        return 0;
    const uint32_t sub = a16 & 15;
    const SprinterVideoInputs::FontLatch& latch = in.fontLatch;
    const int font = (latch.line == y && x >= latch.x0 && x < latch.x1) ? latch.font : -1;
    const uint8_t* mode = R::SymbolMode(line1, sub);
    const uint8_t m0 = mode[0];
    if (SprinterSquare::IsBlank(m0))
        return Screen::kPlaneBRoleBorder;
    if (SprinterSquare::IsBorder(m0))
        return static_cast<uint16_t>(Screen::kPlaneBRoleBorder | ((in.border & 7u) << 8));
    if (!SprinterSquare::IsSpectrumCell(m0, mode[1], mode[2]))
        return 0;
    const uint8_t attr = in.vram[R::AttrAddress(in, mode)];
    const uint8_t symbol = font >= 0 ? static_cast<uint8_t>(font) : in.vram[R::FontAddress(in, mode, b8 & 7)];
    const bool ink = (symbol & (1u << (7 - ((sub >> 1) & 7)))) != 0;
    const uint16_t bright = (attr & 0x40) ? 8 : 0;
    const uint16_t color = static_cast<uint16_t>((ink ? (attr & 7) : ((attr >> 3) & 7)) + bright);
    return static_cast<uint16_t>(Screen::kPlaneBRoleScreen | (ink ? Screen::kPlaneBInk : 0) | (color << 8) | attr);
}
}  // namespace

TEST(SprinterVideoRenderer_Test, DrawSpanBySegments_EqualsThePerPixelRules)
{
    std::mt19937 rng(20261009);
    std::vector<uint8_t> vram(SprinterVideoRam::kSize);
    std::vector<uint32_t> palette(SprinterVideoRam::kPens);
    for (uint32_t& p : palette)
        p = rng();
    const R& renderer = R::Standard();
    std::vector<uint32_t> out(R::kVisibleWidth);
    std::vector<uint16_t> planeB(R::kVisibleWidth);

    // Each pass: new video RAM, new inputs, a few lines cut at random points
    for (int pass = 0; pass < 20; pass++)
    {
        for (size_t i = 0; i < vram.size(); i += 4)
        {
            const uint32_t word = rng();
            for (size_t j = 0; j < 4; j++)
                vram[i + j] = static_cast<uint8_t>(word >> (8 * j));
        }
        if (pass % 4 == 1)
        {
            // Mostly symbol squares (text, Spectrum cells with m1 = m2, border, blank) in both mode pages
            for (uint32_t row = 0; row < 256; row++)
            {
                for (uint32_t column = 0x300; column < 0x3A0; column++)
                {
                    uint8_t& m0 = vram[row * 1024 + column];
                    if ((column & 3) == 0)
                        m0 |= 0x10;
                    if ((column & 3) == 2 && (rng() & 1))
                        m0 = vram[row * 1024 + column - 1];  // m2 = m1: a Spectrum cell when m0 says ZX-40
                }
            }
        }
        SprinterVideoInputs in;
        in.vram = vram.data();
        in.palette = palette.data();
        in.modePage = static_cast<uint8_t>(rng() & 1);
        in.textPage = static_cast<uint8_t>(rng() & 1);
        in.border = static_cast<uint8_t>(rng() & 7);
        in.flash = (rng() & 1) != 0;
        in.lines = (rng() & 1) ? 320 : 312;
        in.SetHold(static_cast<uint8_t>(rng()));
        const uint32_t latchLine = rng() % R::kVisibleLines;
        if (pass % 3 != 0)
        {
            in.fontLatch.line = latchLine;
            in.fontLatch.x0 = rng() % R::kVisibleWidth;
            in.fontLatch.x1 = std::min<uint32_t>(R::kVisibleWidth, in.fontLatch.x0 + 1 + rng() % 24);
            in.fontLatch.font = static_cast<uint8_t>(rng());
        }

        for (int k = 0; k < 24; k++)
        {
            const uint32_t y = (k == 0) ? latchLine : rng() % R::kVisibleLines;
            for (uint32_t x0 = 0; x0 < R::kVisibleWidth;)
            {
                const uint32_t x1 = std::min<uint32_t>(R::kVisibleWidth, x0 + 1 + rng() % 70);
                const bool withPlaneB = (rng() & 3) == 0;
                if (withPlaneB)
                    renderer.DrawSpanPlaneB(in, y, x0, x1, out.data(), planeB.data());
                else
                    renderer.DrawSpan(in, y, x0, x1, out.data());
                for (uint32_t x = x0; x < x1; x++)
                {
                    ASSERT_EQ(out[x - x0], palette[ReferencePen(in, x, y) & (SprinterVideoRam::kPens - 1)])
                        << "pass " << pass << " pixel (" << x << ", " << y << ") of span [" << x0 << ", " << x1 << ")";
                    if (withPlaneB)
                    {
                        ASSERT_EQ(planeB[x - x0], ReferencePlaneB(in, x, y)) << "plane B, pass " << pass << " (" << x << ", " << y << ")";
                    }
                }
                x0 = x1;
            }
        }
    }
}
