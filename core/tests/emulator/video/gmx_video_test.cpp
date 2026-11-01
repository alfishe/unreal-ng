// Scorpion GMX 640x200 extended mode (#7EFD bit 3): mode switch, geometry, fetch, scroller, flash and border.
//
// GMX design section 2.5 and 5: pixel page #39 (#3B with #7FFD bit 3), attribute page 64 above it, 80 linear bytes per
// line, the byte of line y, column x at (y * 80 + x + start) mod 16000 with the scroller's start address
// (#7CFD & #3F) << 8 | (#7AFD & #F0); the standard attribute byte (ink 0-2, paper 3-5, bright 6, flash 7).
// Expectations are computed from the memory encoding at beam positions, not golden frames.

#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpiongmxfixture.h"
#include "emulator/video/gmx/gmxgeometry.h"
#include "emulator/video/screen.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/video/zx/zxgeometry.h"

class GmxVideo_Test : public ScorpionGmxFixture
{
protected:
    static constexpr uint32_t kTstatesPerLine = 224;

    ScreenZX* Screen() { return dynamic_cast<ScreenZX*>(_context->pScreen); }

    void SetUp() override
    {
        ScorpionGmxFixture::SetUp();
        ASSERT_NE(Screen(), nullptr);
        Screen()->InitRaster();
    }

    void EnterExtendedMode() { Out(0x7EFD, 0x08); }

    uint8_t* PixelPage(uint16_t page = 0x39) { return _memory->RAMPageAddress(page); }
    uint8_t* AttrPage(uint16_t page = 0x79) { return _memory->RAMPageAddress(page); }

    void ClearScreenPages()
    {
        std::memset(PixelPage(0x39), 0, 0x4000);
        std::memset(PixelPage(0x3B), 0, 0x4000);
        std::memset(AttrPage(0x79), 0, 0x4000);
        std::memset(AttrPage(0x7B), 0, 0x4000);
    }

    /// Beam T of paper line v, paper T t (5 px per T from the 48 px left border)
    uint32_t PaperT(uint32_t v, uint32_t t) const
    {
        const RasterDescriptor& rd = _context->pScreen->rasterDescriptors[M_GMX];
        return (rd.vSyncLines + rd.vBlankLines + rd.screenOffsetTop + v) * kTstatesPerLine + GmxGeometry::kPaperStartT + t;
    }

    /// Draws the first `tCount` paper T-states of line v
    void DrawPaper(uint32_t v, uint32_t tCount)
    {
        for (uint32_t t = 0; t < tCount; ++t)
            Screen()->Draw(PaperT(v, t));
    }

    uint32_t Pixel(uint32_t x, uint32_t fbRow)
    {
        const FramebufferDescriptor& fb = Screen()->GetFramebufferDescriptor();
        return reinterpret_cast<uint32_t*>(fb.memoryBuffer)[fbRow * fb.width + x];
    }

    uint32_t PaperPixel(uint32_t x, uint32_t v)
    {
        const RasterDescriptor& rd = _context->pScreen->rasterDescriptors[M_GMX];
        return Pixel(rd.screenOffsetLeft + x, rd.screenOffsetTop + v);
    }

    static uint32_t Colour(uint8_t index) { return ZxGeometry::Colour(index); }
};

/// @brief #7EFD bit 3 switches M_SCORPION <-> M_GMX with the 736x288 storage; the frame stays 312 x 224 T
TEST_F(GmxVideo_Test, Bit3SwitchesTheModeAndKeepsTheScorpionFrame)
{
    ASSERT_EQ(Screen()->GetVideoMode(), M_SCORPION);
    EnterExtendedMode();
    ASSERT_EQ(Screen()->GetVideoMode(), M_GMX);
    EXPECT_EQ(Screen::GetVideoModeName(M_GMX), "GMX");

    const FramebufferDescriptor& fb = Screen()->GetFramebufferDescriptor();
    EXPECT_EQ(fb.width, 736u);
    EXPECT_EQ(fb.height, 288u);
    const RasterDescriptor& rd = Screen()->rasterDescriptors[M_GMX];
    EXPECT_EQ(rd.screenWidth, 640u);
    EXPECT_EQ(rd.screenHeight, 200u);
    EXPECT_EQ(rd.screenOffsetLeft, 48u);
    EXPECT_EQ(rd.screenOffsetTop, 44u) << "200 lines 4 above the standard 192-line window";
    EXPECT_EQ(Screen()->GetTstatesPerLine(), 224u);
    EXPECT_EQ(Screen()->GetMaxFrameTiming(), 69888u);

    const RasterState& rs = Screen()->GetRasterState();
    EXPECT_EQ(rs.screenLineAreaStart, 24);
    EXPECT_EQ(rs.screenLineAreaEnd, 151) << "640 px over the 128 T paper window";
    EXPECT_EQ(rs.paperDotsPerT, 5);

    Out(0x7EFD, 0x00);
    EXPECT_EQ(Screen()->GetVideoMode(), M_SCORPION);
    EXPECT_EQ(Screen()->GetFramebufferDescriptor().width, 352u);
}

/// @brief A RESET clears #7EFD and with it the extended mode
TEST_F(GmxVideo_Test, ResetLeavesTheExtendedMode)
{
    EnterExtendedMode();
    ASSERT_EQ(Screen()->GetVideoMode(), M_GMX);
    _gmx->reset();
    EXPECT_EQ(Screen()->GetVideoMode(), M_SCORPION);
}

/// @brief Line 0 reads page #39 offset 0 for pixels and page #79 offset 0 for the attribute, MSB first
TEST_F(GmxVideo_Test, PaperFetchesPixelsAndAttributesAtTheSameOffset)
{
    EnterExtendedMode();
    ClearScreenPages();
    PixelPage()[0] = 0xF0;
    AttrPage()[0] = 0x47;   // bright white ink on black
    PixelPage()[1] = 0x81;
    AttrPage()[1] = 0x0A;   // red ink (2) on blue paper (1)

    DrawPaper(0, 4);   // 20 pixels: bytes 0, 1 and half of 2

    for (uint32_t x = 0; x < 4; ++x)
        EXPECT_EQ(PaperPixel(x, 0), Colour(0x0F)) << "ink at " << x;
    for (uint32_t x = 4; x < 8; ++x)
        EXPECT_EQ(PaperPixel(x, 0), Colour(0x00)) << "paper at " << x;
    EXPECT_EQ(PaperPixel(8, 0), Colour(0x02));
    for (uint32_t x = 9; x < 15; ++x)
        EXPECT_EQ(PaperPixel(x, 0), Colour(0x01)) << "paper at " << x;
    EXPECT_EQ(PaperPixel(15, 0), Colour(0x02));
}

/// @brief Line y starts 80 bytes after line y - 1; the last column of a line is byte 79
TEST_F(GmxVideo_Test, LinesAreEightyLinearBytes)
{
    EnterExtendedMode();
    ClearScreenPages();
    PixelPage()[80 * 3 + 79] = 0x01;
    AttrPage()[80 * 3 + 79] = 0x07;

    for (uint32_t t = 0; t < GmxGeometry::kPaperTStates; ++t)
        Screen()->Draw(PaperT(3, t));

    EXPECT_EQ(PaperPixel(639, 3), Colour(0x07)) << "last pixel of line 3 = bit 0 of byte 319";
    EXPECT_EQ(PaperPixel(638, 3), Colour(0x00));
}

/// @brief #7FFD bit 3 selects the second screen: pixels #3B, attributes #7B
TEST_F(GmxVideo_Test, SecondScreenUsesPages3BAnd7B)
{
    EnterExtendedMode();
    ClearScreenPages();
    PixelPage(0x3B)[0] = 0x80;
    AttrPage(0x7B)[0] = 0x04;   // green ink
    Out(0x7FFD, 0x08);

    DrawPaper(0, 1);
    EXPECT_EQ(PaperPixel(0, 0), Colour(0x04));
    EXPECT_EQ(PaperPixel(1, 0), Colour(0x00));
}

/// @brief The scroller's start address moves the picture by bytes and wraps at 16000 bytes
TEST_F(GmxVideo_Test, ScrollerStartsThePictureAtItsAddressAndWraps)
{
    EnterExtendedMode();
    ClearScreenPages();
    PixelPage()[80] = 0x80;     // the first byte of line 1
    AttrPage()[80] = 0x06;      // yellow ink
    PixelPage()[0] = 0x80;
    AttrPage()[0] = 0x05;       // cyan ink

    Out(0x7AFD, 0x5F);          // the low four bits are dropped: 0x50 = 80, one line
    Out(0x7CFD, 0x00);
    DrawPaper(0, 1);
    EXPECT_EQ(PaperPixel(0, 0), Colour(0x06)) << "line 0 shows line 1";

    // start = 15920 = (0x3E << 8) | 0x30: line 1 reads byte 16000 mod 16000 = 0
    Out(0x7AFD, 0x30);
    Out(0x7CFD, 0x3E);
    ASSERT_EQ(GmxGeometry::ScrollStart(State().gmx.p7AFD, State().gmx.p7CFD), 15920u);
    DrawPaper(1, 1);
    EXPECT_EQ(PaperPixel(0, 1), Colour(0x05)) << "line 1 wraps to byte 0";
}

/// @brief Attribute bit 7 inverts the pixels in the flash phase
TEST_F(GmxVideo_Test, FlashInvertsInkAndPaperInItsPhase)
{
    EnterExtendedMode();
    ClearScreenPages();
    PixelPage()[0] = 0x80;
    AttrPage()[0] = 0x87;       // flash, white ink on black

    Screen()->_vid.flash = 0;
    DrawPaper(0, 1);
    EXPECT_EQ(PaperPixel(0, 0), Colour(0x07));
    EXPECT_EQ(PaperPixel(1, 0), Colour(0x00));

    Screen()->_vid.flash = 1;
    DrawPaper(0, 1);
    EXPECT_EQ(PaperPixel(0, 0), Colour(0x00));
    EXPECT_EQ(PaperPixel(1, 0), Colour(0x07));
}

/// @brief Lines outside the 200 and the side bands show the border colour
TEST_F(GmxVideo_Test, BorderSurroundsThePicture)
{
    EnterExtendedMode();
    ClearScreenPages();
    _context->pScreen->SetBorderColor(2);
    const RasterDescriptor& rd = Screen()->rasterDescriptors[M_GMX];
    const uint32_t firstVisible = (rd.vSyncLines + rd.vBlankLines) * kTstatesPerLine;

    Screen()->Draw(firstVisible + GmxGeometry::kPaperStartT);       // top border row, inside the paper columns
    Screen()->Draw(PaperT(0, 0) - 1);                                 // the last left-border T of paper line 0
    Screen()->Draw(PaperT(0, GmxGeometry::kPaperTStates));            // the first right-border T

    EXPECT_EQ(Pixel(rd.screenOffsetLeft, 0), Colour(2));
    EXPECT_EQ(Pixel(rd.screenOffsetLeft + 4, 0), Colour(2));
    EXPECT_EQ(Pixel(rd.screenOffsetLeft - 1, rd.screenOffsetTop), Colour(2));
    EXPECT_EQ(Pixel(rd.screenOffsetLeft + rd.screenWidth, rd.screenOffsetTop), Colour(2));
}

/// @brief The geometry helpers invert each other for every start address the scroller can hold
TEST(GmxGeometry_Test, ByteOffsetAndItsInverseAgree)
{
    using namespace GmxGeometry;
    for (uint32_t start : {0u, 80u, 0x12A0u, 15920u, 0x3FF0u})
    {
        for (uint32_t y : {0u, 1u, 99u, 199u})
        {
            for (uint32_t column : {0u, 40u, 79u})
            {
                uint32_t gotY = 0, gotColumn = 0;
                ASSERT_TRUE(DecodeByteOffset(ByteOffset(y, column, start), start, gotY, gotColumn));
                EXPECT_EQ(gotY, y);
                EXPECT_EQ(gotColumn, column);
            }
        }
    }
    uint32_t y = 0, column = 0;
    EXPECT_FALSE(DecodeByteOffset(16000, 0, y, column)) << "bytes past 16000 are never shown";
}
