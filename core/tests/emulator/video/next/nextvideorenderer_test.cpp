// NextVideoRenderer (core/src/emulator/video/next/nextvideorenderer.h): the ULA in its Timex modes, LoRes, Layer 2 in
// three resolutions, the palettes, the transparency and the layer order, on synthetic memory. The expected pixels are
// computed from the palette the test sets, so a failure names the rule: docs/inprogress/2026-10-07-zx-next/research-fpga-vhdl.md
// sections 9 and 16-17, registers.txt NR #12-#1B #26 #27 #40-#44 #4A #68-#71.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "emulator/video/next/nextvideorenderer.h"

class NextVideoRenderer_Test : public ::testing::Test
{
protected:
    static constexpr unsigned kPages = 16;
    std::vector<uint8_t> _ram = std::vector<uint8_t>(kPages * 0x4000, 0);
    NextVideoRegs _regs;
    NextVideoInputs _in;
    std::vector<uint32_t> _line = std::vector<uint32_t>(NextVideoRenderer::kWidth);

    void SetUp() override
    {
        _regs.Reset();
        _in.ram = _ram.data();
        _in.ramPages = kPages;
        _in.regs = &_regs;
        _in.nr[0x14] = 0xE3;
        _in.nr[0x4A] = 0xE3;
        _in.nr[0x12] = 8;
        _in.nr[0x42] = 0x07;
    }

    void Render(unsigned y) { NextVideoRenderer::RenderLine(_in, y, _line.data()); }
    uint32_t Px(unsigned x) const { return _line[x]; }
    /// The colour of a palette entry as the renderer paints it
    uint32_t Pal(unsigned palette, unsigned index) const { return NextVideoRenderer::Rgba(_regs.PaletteEntry(palette, index) & 0x1FF); }
    uint8_t* Page(unsigned p) { return _ram.data() + static_cast<size_t>(p) * 0x4000; }
};

TEST_F(NextVideoRenderer_Test, BorderIsTheUlaPaperColourOfPortFe)
{
    _in.border = 2;  // red
    Render(0);
    EXPECT_EQ(Px(0), Pal(0, 16 + 2));
    EXPECT_EQ(Px(639), Pal(0, 16 + 2));
    Render(255);
    EXPECT_EQ(Px(320), Pal(0, 16 + 2));
    // beside the paper too
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft - 1), Pal(0, 16 + 2));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 512), Pal(0, 16 + 2));
}

TEST_F(NextVideoRenderer_Test, StandardScreenInkPaperBrightAndFlash)
{
    uint8_t* screen = Page(5);
    screen[0] = 0x80;             // the leftmost pixel of line 0 set
    screen[0x1800] = 0x4A;        // bright, paper 1 (blue), ink 2 (red)
    screen[0x1801] = 0xBA;        // flash, paper 7, ink 2
    screen[1] = 0xFF;
    Render(NextVideoRenderer::kPaperTop);
    const unsigned left = NextVideoRenderer::kPaperLeft;
    EXPECT_EQ(Px(left), Pal(0, 2 + 8)) << "ink pixel, bright red";
    EXPECT_EQ(Px(left + 1), Pal(0, 2 + 8)) << "a pixel is two sub-pixels";
    EXPECT_EQ(Px(left + 2), Pal(0, 16 + 1 + 8)) << "paper, bright blue";
    EXPECT_EQ(Px(left + 16), Pal(0, 2)) << "ink of the flash cell: not flashing";
    _in.flash = true;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left + 16), Pal(0, 16 + 7)) << "flashing: ink and paper swap";
}

TEST_F(NextVideoRenderer_Test, ShadowScreenAndScrolls)
{
    Page(7)[0x1800] = 0x07;
    Page(7)[0] = 0x80;
    _in.shadowScreen = true;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 7)) << "bank 7";
    // Y scroll: line 0 shows line 5's data; X scroll 8 shifts a column
    _in.shadowScreen = false;
    Page(5)[0x0500] = 0xFF;  // third pixel row of the first cell row ... (y = 5)
    Page(5)[0x1800] = 0x07;
    _in.nr[0x27] = 5;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 7));
    _in.nr[0x27] = 0;
    Page(5)[2] = 0x80;
    Page(5)[0x1802] = 0x07;
    _in.nr[0x26] = 16;  // two columns
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 7)) << "column 2 now leftmost";
}

TEST_F(NextVideoRenderer_Test, TimexHiColourAndHiRes)
{
    Page(5)[0] = 0x80;
    Page(5)[0x2000] = 0x0A;  // hi-colour: attribute of the 8x1 cell at +#2000: paper 1, ink 2
    _in.portFf = 2;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 2));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 2), Pal(0, 16 + 1));

    // hi-res: 512 pixels, screen 0 then screen 1 byte per column; ink = bits 5:3 (here 5), paper 7 - ink
    _in.portFf = 6 | (5 << 3);
    Page(5)[0] = 0x80;       // pixel 0
    Page(5)[0x2000] = 0x01;  // pixel 15
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 5));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 1), Pal(0, 16 + 2));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 15), Pal(0, 5));
}

TEST_F(NextVideoRenderer_Test, UlaNextTakesInkAndPaperFromTheMask)
{
    _regs.WritePaletteControl(0x01);  // ULANext on
    _regs.WriteUlaNextFormat(0x0F);   // 4 bits of ink: attribute low nibble, paper 128 + high nibble
    // distinct colours at index 5 and 128 + 9
    _regs.WritePaletteControl(0x01);
    _regs.WritePaletteIndex(5);
    _regs.WritePaletteValue8(0xE0);
    _regs.WritePaletteIndex(128 + 9);
    _regs.WritePaletteValue8(0x1C);
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0x95;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 5));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 2), Pal(0, 128 + 9));
}

TEST_F(NextVideoRenderer_Test, LoResIsTwoByTwoUlaPixelsInTwoHalves)
{
    _in.nr[0x15] = 0x80;
    Page(5)[5] = 0x33;            // row 0, column 5
    Page(5)[0x2000 + 128 * 2] = 0x44;  // row 50 (the second half's row 2), column 0
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 5 * 4), Pal(0, 0x33));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 5 * 4 + 3), Pal(0, 0x33)) << "a LoRes pixel is four sub-pixels wide";
    Render(NextVideoRenderer::kPaperTop + 1);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 5 * 4), Pal(0, 0x33)) << "and two lines high";
    Render(NextVideoRenderer::kPaperTop + 100);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 0x44));
    _in.nr[0x6A] = 0x02;  // palette offset 2 (x16)
    Render(NextVideoRenderer::kPaperTop + 100);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 0x44 + 0x20));
}

TEST_F(NextVideoRenderer_Test, Layer2In256x192WithScrollClipAndPaletteOffset)
{
    _in.layer2Enable = true;
    uint8_t* l2 = Page(8);
    l2[10 * 256 + 20] = 0x42;
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42)) << "pixel (20, 10) of Layer 2";
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 41), Pal(1, 0x42));
    // the palette offset adds to the colour index high nibble
    _in.nr[0x70] = 0x03;
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42 + 0x30));
    _in.nr[0x70] = 0;
    // scroll X 20 puts that pixel at x = 0, scroll Y 10 line 0 at row 10
    _in.nr[0x16] = 20;
    _in.nr[0x17] = 10;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(1, 0x42));
    _in.nr[0x16] = 0;
    _in.nr[0x17] = 0;
    // clip: x 30-40 only: the pixel at 20 is gone
    // (the clip window of Layer 2 is NR #18: X1 X2 Y1 Y2)
    _regs.WriteClip(0, 30);
    _regs.WriteClip(0, 40);
    _regs.WriteClip(0, 0);
    _regs.WriteClip(0, 191);
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42)) << "clipped";
    // not enabled: not drawn
    _regs.Reset();
    _in.layer2Enable = false;
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42));
}

TEST_F(NextVideoRenderer_Test, Layer2In320x256IsColumnMajorAndCoversTheBorder)
{
    _in.layer2Enable = true;
    _in.nr[0x70] = 0x10;  // 320x256x8
    uint8_t* l2 = Page(8);
    l2[3 * 256 + 7] = 0x55;  // x = 3, y = 7
    Render(7);
    EXPECT_EQ(Px(3 * 2), Pal(1, 0x55)) << "column-major, over the border";
    EXPECT_EQ(Px(3 * 2 + 1), Pal(1, 0x55));
    l2[300 * 256 + 255] = 0x66;  // the last row of column 300 lives in the second bank (offset 76800 = 4.7 banks)
    // an address past the 16-page test RAM would read nothing: the renderer must not crash
    Render(255);
    SUCCEED();
}

TEST_F(NextVideoRenderer_Test, Layer2In640x256Has4BitPixelsHighNibbleFirst)
{
    _in.layer2Enable = true;
    _in.nr[0x70] = 0x20 | 0x01;  // 640x256x4, palette offset 1
    uint8_t* l2 = Page(8);
    l2[5 * 256 + 9] = 0xA3;  // x = 10, 11 at y = 9
    Render(9);
    EXPECT_EQ(Px(10), Pal(1, 0x10 | 0xA));
    EXPECT_EQ(Px(11), Pal(1, 0x10 | 0x3));
}

TEST_F(NextVideoRenderer_Test, TransparencyOrderPriorityAndFallback)
{
    _in.layer2Enable = true;
    _regs.WritePaletteControl(0x10);  // select Layer 2 palette 1 for writing
    _regs.WritePaletteIndex(0x80);
    _regs.WritePaletteValue8(0xE3);   // the global transparency colour
    _regs.WritePaletteIndex(0x81);
    _regs.WritePaletteValue8(0x1C);   // green
    _regs.WritePaletteControl(0x00);
    Page(8)[0] = 0x80;  // Layer 2 pixel (0, 0): transparent colour
    Page(8)[1] = 0x81;  // (1, 0): green
    Page(5)[0] = 0xC0;
    Page(5)[0x1800] = 0x0A;  // ULA: ink red at pixels 0 and 1
    const unsigned left = NextVideoRenderer::kPaperLeft;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 2)) << "Layer 2 transparent: the ULA shows";
    EXPECT_EQ(Px(left + 2), Pal(1, 0x81)) << "SLU: Layer 2 over the ULA";
    _in.nr[0x15] = 2 << 2;  // SUL: the ULA over Layer 2
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left + 2), Pal(0, 2));
    // a Layer 2 priority colour (palette bit 9) is above the ULA whatever the order
    _regs.WritePaletteControl(0x10);
    _regs.WritePaletteIndex(0x81);
    _regs.WritePaletteValue9(0x1C);
    _regs.WritePaletteValue9(0x80);
    _regs.WritePaletteControl(0x00);
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left + 2) & 0x00FFFFFF, Pal(1, 0x81) & 0x00FFFFFF) << "priority colour on top";
    // nothing opaque: the fallback colour (NR #4A)
    _in.nr[0x68] = 0x80;  // ULA off
    _in.layer2Enable = false;
    _in.nr[0x4A] = 0x1D;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), NextVideoRenderer::Rgba(static_cast<uint16_t>((0x1D << 1) | 1))) << "fallback, low blue bit = B1 | B0";
}
