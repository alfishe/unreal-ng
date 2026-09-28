#include "stdafx.h"
#include "pch.h"

#include "emulator/zxpoly/zxpolyscreencomposer.h"

#include <vector>

/// Pure unit tests of the ZX-Poly composer rules (zxpoly VideoController)
class ZXPolyScreenComposer_Test : public ::testing::Test
{
protected:
    static constexpr unsigned W = ZXPolyScreenComposer::OUT_WIDTH;

    std::array<std::vector<uint8_t>, 4> _vram;
    std::vector<uint32_t> _out;
    uint32_t _palette[16];

    void SetUp() override
    {
        for (auto& v : _vram)
            v.assign(ZXPolyScreenComposer::SCREEN_BYTES, 0);
        _out.assign(static_cast<size_t>(W) * ZXPolyScreenComposer::OUT_HEIGHT, 0xDEADBEEF);
        for (uint32_t i = 0; i < 16; i++)
            _palette[i] = 0x1000u + i;   // colour value encodes the palette index
    }

    void SetPixel(size_t module, unsigned x, unsigned y)
    {
        _vram[module][ZXPolyScreenComposer::BitmapOffset(x, y)] |= static_cast<uint8_t>(0x80u >> (x & 7u));
    }

    void SetAttr(size_t module, unsigned x, unsigned y, uint8_t attr)
    {
        _vram[module][ZXPolyScreenComposer::AttributeOffset(x, y)] = attr;
    }

    void Compose(uint8_t mode, bool flash = false)
    {
        std::array<const uint8_t*, 4> vram{_vram[0].data(), _vram[1].data(), _vram[2].data(), _vram[3].data()};
        ZXPolyScreenComposer::Compose(vram, mode, flash, _palette, _out.data());
    }

    /// Output pixel (quadrant qx/qy of source pixel x, y) as a palette index
    int At(unsigned x, unsigned y, unsigned qx = 0, unsigned qy = 0) const
    {
        return static_cast<int>(_out[(y * 2 + qy) * W + x * 2 + qx]) - 0x1000;
    }
};

TEST_F(ZXPolyScreenComposer_Test, BitmapOffsetFollowsZxInterleave)
{
    EXPECT_EQ(ZXPolyScreenComposer::BitmapOffset(0, 0), 0x0000u);
    EXPECT_EQ(ZXPolyScreenComposer::BitmapOffset(0, 1), 0x0100u);
    EXPECT_EQ(ZXPolyScreenComposer::BitmapOffset(0, 8), 0x0020u);
    EXPECT_EQ(ZXPolyScreenComposer::BitmapOffset(0, 64), 0x0800u);
    EXPECT_EQ(ZXPolyScreenComposer::BitmapOffset(255, 191), 0x17FFu);
    EXPECT_EQ(ZXPolyScreenComposer::AttributeOffset(255, 191), 6143u + 768u);
}

TEST_F(ZXPolyScreenComposer_Test, Mode4BitOrderIsCpu3Cpu0Cpu1Cpu2)
{
    SetPixel(0, 10, 20);    // bit 2 (green)
    SetPixel(1, 11, 20);    // bit 1 (red)
    SetPixel(2, 12, 20);    // bit 0 (blue)
    SetPixel(3, 13, 20);    // bit 3 (bright)
    for (size_t m = 0; m < 4; m++)
        SetPixel(m, 14, 20);

    Compose(4);

    EXPECT_EQ(At(10, 20), 4);
    EXPECT_EQ(At(11, 20), 2);
    EXPECT_EQ(At(12, 20), 1);
    EXPECT_EQ(At(13, 20), 8);
    EXPECT_EQ(At(14, 20), 15);
    EXPECT_EQ(At(15, 20), 0);
    // Doubled 2x2
    EXPECT_EQ(At(10, 20, 1, 1), 4);
}

TEST_F(ZXPolyScreenComposer_Test, Mode5QuadrantsUseEachModulesOwnAttribute)
{
    SetAttr(0, 0, 0, 0x01);     // ink blue
    SetAttr(1, 0, 0, 0x02);     // ink red
    SetAttr(2, 0, 0, 0x04);     // ink green
    SetAttr(3, 0, 0, 0x46);     // ink yellow, bright
    for (size_t m = 0; m < 4; m++)
        SetPixel(m, 0, 0);

    Compose(5);

    EXPECT_EQ(At(0, 0, 0, 0), 1);     // CPU0 top-left
    EXPECT_EQ(At(0, 0, 1, 0), 2);     // CPU1 top-right
    EXPECT_EQ(At(0, 0, 0, 1), 4);     // CPU2 bottom-left
    EXPECT_EQ(At(0, 0, 1, 1), 14);    // CPU3 bottom-right
}

TEST_F(ZXPolyScreenComposer_Test, Mode6FloodsCellWhenInkEqualsPaper)
{
    SetAttr(0, 0, 0, 0x12);     // ink 2, paper 2
    SetPixel(3, 1, 1);          // would be index 8 in mode 4
    SetAttr(0, 8, 0, 0x38);     // ink 0, paper 7: normal poly pixels
    SetPixel(3, 9, 1);

    Compose(6);

    EXPECT_EQ(At(1, 1), 2);
    EXPECT_EQ(At(7, 7), 2);
    EXPECT_EQ(At(9, 1), 8);
    EXPECT_EQ(At(10, 1), 0);
}

TEST_F(ZXPolyScreenComposer_Test, Mode6FlashSwapsBeforeTheInkPaperComparison)
{
    SetAttr(0, 0, 0, 0x80 | 0x08 | 0x01);   // FLASH, ink 1, paper 1
    Compose(6, true);
    EXPECT_EQ(At(0, 0), 1);
}

TEST_F(ZXPolyScreenComposer_Test, Mode7FlashBitSelectsClassicOrPoly)
{
    // Cell 0: FLASH clear -> 2x2 in CPU0 ink/paper, per-module pixel
    SetAttr(0, 0, 0, 0x07 | 0x08);          // ink 7, paper 1
    SetPixel(1, 0, 0);                      // only CPU1 has the pixel
    // Cell 1: FLASH set -> poly pixels
    SetAttr(0, 8, 0, 0x80 | 0x38);          // ink 0, paper 7
    SetPixel(2, 8, 0);

    Compose(7, true);                       // blink phase must not matter in mode 7

    EXPECT_EQ(At(0, 0, 0, 0), 1);           // CPU0: paper
    EXPECT_EQ(At(0, 0, 1, 0), 7);           // CPU1: ink
    EXPECT_EQ(At(0, 0, 0, 1), 1);
    EXPECT_EQ(At(0, 0, 1, 1), 1);
    EXPECT_EQ(At(8, 0), 1);                 // CPU2 only -> blue
}

TEST_F(ZXPolyScreenComposer_Test, Mode0To3ShowOneModuleClassic)
{
    SetAttr(2, 0, 0, 0x0A);                 // ink 2, paper 1
    SetPixel(2, 0, 0);
    Compose(2);
    EXPECT_EQ(At(0, 0), 2);
    EXPECT_EQ(At(1, 0), 1);
}
