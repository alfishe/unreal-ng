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

    // hi-res: 512 pixels, screen 0 then screen 1 byte per column; ink = bits 5:3 (here 5), paper 7 - ink, both bright (the attribute
    // is %01010101: zxula.vhd border_clr_tmx = "01" & not ink & ink) - and the border is the bright paper colour
    _in.portFf = 6 | (5 << 3);
    Page(5)[0] = 0x80;       // pixel 0
    Page(5)[0x2000] = 0x01;  // pixel 15
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 5 + 8));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 1), Pal(0, 16 + 2 + 8));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 15), Pal(0, 5 + 8));
    Render(0);
    EXPECT_EQ(Px(0), Pal(0, 16 + 2 + 8)) << "the border of hi-res is the paper colour with the bright bit (border_clr_tmx = 01 PPP III)";
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

// lores.vhd: y_pre = vc + scroll_y runs on from 192 with y(7:6) + 1, so a Y scroll that takes the row past 384 reads the memory behind the
// 96 rows (row 104 here: 104 * 128 + 0x800) instead of wrapping to the top
TEST_F(NextVideoRenderer_Test, LoResYScrollRunsOnIntoTheMemoryBehindTheRows)
{
    _in.nr[0x15] = 0x80;
    _in.nr[0x33] = 250;
    Page(5)[0x3C00] = 0x77;   // y = 208: row 104, +0x800 from row 48 on
    Page(5)[8 * 128] = 0x11;  // where a wrap at 192 would read (row 8)
    Render(NextVideoRenderer::kPaperTop + 150);  // 150 + 250 = 400
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 0x77));
}

TEST_F(NextVideoRenderer_Test, RadastanYScrollRunsOnToo)
{
    _in.nr[0x15] = 0x80;
    _in.nr[0x6A] = 0x20;
    _in.nr[0x33] = 250;
    Page(5)[104 * 64] = 0x90;  // row 104 of the first display file, the high nibble
    Render(NextVideoRenderer::kPaperTop + 150);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 0x9));
}

// Radastan (NR #6A bit 5, lores.vhd mode 1): 128 x 96 at 4 bits, a 64-byte row, the high nibble first, two pixels a nibble
TEST_F(NextVideoRenderer_Test, RadastanIsFourBitPixelsInA64ByteRow)
{
    _in.nr[0x15] = 0x80;
    _in.nr[0x6A] = 0x20;
    Page(5)[2] = 0x5A;                 // row 0, bytes of four pixels: x 8-9 the high nibble, 10-11 the low one
    Page(5)[0x2000 + 50 * 64] = 0x3C;  // the second display file: row 50
    Render(NextVideoRenderer::kPaperTop);
    const unsigned left = NextVideoRenderer::kPaperLeft;
    EXPECT_EQ(Px(left + 8 * 2), Pal(0, 0x5)) << "the high nibble";
    EXPECT_EQ(Px(left + 9 * 2 + 1), Pal(0, 0x5)) << "two pixels wide";
    EXPECT_EQ(Px(left + 10 * 2), Pal(0, 0xA)) << "the low nibble";
    _in.nr[0x6A] = 0x23;               // palette offset 3: the index's high nibble
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left + 8 * 2), Pal(0, 0x35));
    _in.nr[0x6A] = 0x30;               // the XOR bit: the second display file
    Render(NextVideoRenderer::kPaperTop + 100);
    EXPECT_EQ(Px(left), Pal(0, 0x3)) << "port #FF bit 0 xor NR #6A bit 4 picks the file";
    _in.nr[0x6A] = 0x20;
    _in.portFf = 1;
    Render(NextVideoRenderer::kPaperTop + 100);
    EXPECT_EQ(Px(left), Pal(0, 0x3));
    _in.portFf = 0;
    _in.nr[0x6A] = 0x20;
    Render(NextVideoRenderer::kPaperTop + 100);
    EXPECT_NE(Px(left), Pal(0, 0x3)) << "file 0 has nothing at that address";
}

// ULA+ (NR #68 bit 3): the CLUT is entries 192-255 of the ULA palette: "11" flash bright half colour, flashing off
TEST_F(NextVideoRenderer_Test, UlaPlusIndexesTheClutWithFlashBrightAndTheHalf)
{
    _in.nr[0x68] = 0x08;
    _in.border = 3;
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0xC2;            // flash, bright, paper 0, ink 2
    _in.flash = true;                  // would swap ink and paper on the standard ULA
    Render(NextVideoRenderer::kPaperTop);
    const unsigned left = NextVideoRenderer::kPaperLeft;
    EXPECT_EQ(Px(left), Pal(0, 0xC0 | 0x30 | 2)) << "ink: 11 1 1 0 010";
    EXPECT_EQ(Px(left + 2), Pal(0, 0xC0 | 0x30 | 8 | 0)) << "paper: the half bit set";
    Render(0);
    EXPECT_EQ(Px(0), Pal(0, 0xC8 + 3)) << "the border is the paper half of the first row";
    _in.nr[0x68] = 0x00;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 16 + 0 + 8)) << "off: the standard ULA (flash swapped, bright paper)";
}

TEST_F(NextVideoRenderer_Test, UlaPlusGivesWayToUlaNext)
{
    _in.nr[0x68] = 0x08;
    _regs.WritePaletteControl(0x01);   // ULANext on
    _regs.WriteUlaNextFormat(0xFF);
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0x42;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 0x42)) << "ULANext's attribute is the index: not the CLUT";
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

// A Layer 2 clip window with left > right or top > bottom contains no pixel (layer2.vhd: hc >= x1 and hc <= x2 and vc >= y1 and vc <= y2)
TEST_F(NextVideoRenderer_Test, Layer2WithAnInvertedClipWindowShowsNothing)
{
    _in.layer2Enable = true;
    Page(8)[10 * 256 + 20] = 0x42;
    Render(NextVideoRenderer::kPaperTop + 10);
    ASSERT_EQ(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42)) << "visible without a clip";
    _regs.WriteClip(0, 40);  // x1 > x2
    _regs.WriteClip(0, 30);
    _regs.WriteClip(0, 0);
    _regs.WriteClip(0, 191);
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42));
    _regs.Reset();
    _regs.WriteClip(0, 0);
    _regs.WriteClip(0, 255);
    _regs.WriteClip(0, 20);  // y1 > y2
    _regs.WriteClip(0, 5);
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x42));
    // the 320 x 256 mode is the same
    _in.nr[0x70] = 0x10;
    Page(8)[20 * 256 + 10] = 0x42;
    Render(10);
    EXPECT_NE(Px(40), Pal(1, 0x42));
}

// layer2.vhd forms the row as y(7:6) = y_pre(7:6) (+1 from 192 up) over y_pre(5:0): a scroll above 191 is not a modulo, rows 192-254 show
TEST_F(NextVideoRenderer_Test, Layer2In256x192RowsAboveTheBottomAreTheVhdlsWrap)
{
    _in.layer2Enable = true;
    uint8_t* l2 = Page(8);
    l2[16 * 256 + 20] = 0x31;   // what a modulo 192 would show
    l2[208 * 256 + 20] = 0x77;  // what the VHDL shows: y_pre = 150 + 250 = 400, y(7:6) = 10 + 1
    _in.nr[0x17] = 250;
    Render(NextVideoRenderer::kPaperTop + 150);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x77));
    // inside the 192 rows nothing changes: 100 + 50 = 150 stays 150, 191 + 100 = 291 is 291 - 192 = 99... (256-319 maps to 64-127)
    _in.nr[0x17] = 100;
    l2[(291 - 192) * 256 + 20] = 0x55;
    Render(NextVideoRenderer::kPaperTop + 191);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 40), Pal(1, 0x55));
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

// region <Tilemap>

class NextTilemap_Test : public NextVideoRenderer_Test
{
protected:
    // map at bank 5 offset #4000 - 16K wraps, so use offset #1000 (NR #6E = 0x10), definitions at #2000 (NR #6F = 0x20)
    void SetUp() override
    {
        NextVideoRenderer_Test::SetUp();
        _in.nr[0x6B] = 0x80;  // enabled, 40 columns, 16-bit map entries
        _in.nr[0x6E] = 0x10;
        _in.nr[0x6F] = 0x20;
        _in.nr[0x4C] = 0x0F;  // transparent index
        _in.nr[0x68] = 0x80;  // no ULA, so the tilemap is the only layer
        // the tilemap palettes power on at zero and the firmware leaves them so: a program writes them (colour = index here)
        _regs.WritePaletteControl(0x30);
        for (unsigned i = 0; i < 256; i++)
        {
            _regs.WritePaletteIndex(static_cast<uint8_t>(i));
            _regs.WritePaletteValue8(static_cast<uint8_t>(i));
        }
        _regs.WritePaletteControl(0x00);
    }
    uint8_t* Map() { return Page(5) + 0x1000; }
    uint8_t* Defs() { return Page(5) + 0x2000; }
};

TEST_F(NextTilemap_Test, TilePixelsComeFromTheDefinitionWithPaletteOffsetAndIndexTransparency)
{
    Map()[0] = 1;          // tile 1 at column 0 row 0
    Map()[1] = 0x30;       // palette offset 3
    Defs()[32 + 0] = 0x2F; // line 0 of tile 1: pixel 0 = 2, pixel 1 = 15 (transparent)
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, (3 << 4) | 2)) << "a tile pixel is two sub-pixels wide";
    EXPECT_EQ(Px(1), Pal(3, (3 << 4) | 2));
    EXPECT_NE(Px(2), Pal(3, (3 << 4) | 15)) << "index 15 is transparent: the fallback shows";
}

// zxnext.vhd: the video side of bank 7 is an 8K BRAM (page #0E) addressed by bits 12:0 ("ULA BANK 7 (8k only due to limited bram resources)").
// Mapscroll 3 writes its map through MMU page 14 at offset 0 and points NR #6E at #A0 (bank 7, offset #2000): the picture needs the wrap
TEST_F(NextTilemap_Test, Bank7MapOffsetWrapsInTheEightKilobyteBram)
{
    _in.nr[0x6E] = 0x80 | 0x20;  // bank 7, offset #2000 -> BRAM #0000
    Page(7)[0] = 1;              // tile 1 at column 0 row 0 (first half of bank 7)
    Page(7)[1] = 0x30;
    Page(7)[0x2000] = 2;         // what a 16K bank would have at #2000: must NOT be used
    Defs()[32] = 0x2F;
    Defs()[64] = 0x4F;
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, (3 << 4) | 2));
}

TEST_F(NextTilemap_Test, MirrorsAndRotate)
{
    Defs()[0] = 0x12;      // tile 0 line 0: pixels 0..7 = 1,2,0,0,0,0,0,0
    Defs()[4 * 7] = 0x34;  // line 7: 3,4 at the left
    Map()[1] = 0x08;       // x mirror: pixel 0 of the tile is at screen pixel 7
    Render(0);
    EXPECT_EQ(Px(14), Pal(3, 1));
    EXPECT_EQ(Px(12), Pal(3, 2));
    Map()[1] = 0x04;       // y mirror: line 0 shows line 7
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, 3));
    EXPECT_EQ(Px(2), Pal(3, 4));
    // rotate: rows and columns of the tile swap, and the picture is mirrored in x (the turn is a quarter)
    Map()[1] = 0x02;
    Defs()[0] = 0;
    Defs()[4 * 7] = 0;
    Defs()[28] = 0x50;     // tile row 7, column 0 = 5
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, 5)) << "line 0, pixel 0 of the turned tile = tile row 7, column 0";
}

TEST_F(NextTilemap_Test, ScrollWrapsAndNoFlagsModeUsesTheDefaultAttribute)
{
    _in.nr[0x6B] = 0xA0;  // enabled + 8-bit map entries
    _in.nr[0x6C] = 0x50;  // default attribute: palette offset 5
    Map()[0] = 0;
    Map()[1] = 1;         // the entries are one byte now: tile 1 in column 1
    Defs()[32] = 0x70;
    Render(0);
    EXPECT_EQ(Px(16), Pal(3, (5 << 4) | 7));
    _in.nr[0x30] = 8;     // scroll X by one tile
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, (5 << 4) | 7));
    _in.nr[0x31] = 8;     // scroll Y by one row: line 0 shows map row 1 (empty tiles -> tile 0, a transparent index 0? no: index 0)
    Render(0);
    EXPECT_NE(Px(0), Pal(3, (5 << 4) | 7));
}

TEST_F(NextTilemap_Test, UlaAboveWhenTheTileSaysSoAndTextModeUsesTheGlobalTransparency)
{
    _in.nr[0x68] = 0x00;  // the ULA is on
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0x0A;  // ink red
    Defs()[0] = 0x10;
    Map()[1] = 0x01;      // ULA over this tile
    const unsigned y = NextVideoRenderer::kPaperTop;
    const unsigned x = NextVideoRenderer::kPaperLeft;
    // the tile map covers the screen's top-left; paper (64, 32) is tile column 4 row 4: put the tile there
    unsigned entry = (4 * 40 + 4) * 2;
    Map()[entry] = 0;
    Map()[entry + 1] = 0x01;
    Defs()[0] = 0x10;
    Render(y);
    EXPECT_EQ(Px(x), Pal(0, 2)) << "the ULA pixel is above a 'ULA over' tile";
    Map()[entry + 1] = 0x00;
    Render(y);
    EXPECT_EQ(Px(x), Pal(3, 1)) << "otherwise the tile is above the ULA";
    // the tilemap off: the ULA again
    _in.nr[0x6B] = 0x00;
    Render(y);
    EXPECT_EQ(Px(x), Pal(0, 2));
}

TEST_F(NextTilemap_Test, TextModeTilesAreOneBitPerPixel)
{
    _in.nr[0x6B] = 0x88;  // enabled + text
    Defs()[8 * 65] = 0x80;  // character 'A' (65): row 0 has the leftmost pixel
    Map()[0] = 65;
    Map()[1] = 0x06;      // palette offset 3 (bits 7:1)
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, (3 << 1) | 1));
    EXPECT_EQ(Px(2), Pal(3, (3 << 1) | 0));
}

// endregion

// region <Sprites>

class NextSprites_Test : public NextVideoRenderer_Test
{
protected:
    NextSprites _sprites;
    void SetUp() override
    {
        NextVideoRenderer_Test::SetUp();
        _sprites.Reset();
        _in.sprites = &_sprites;
        _in.nr[0x15] = 0x01;  // sprites on, SLU
        _in.nr[0x4B] = 0x00;  // the transparent index (unwritten pattern bytes are 0)
        _in.nr[0x68] = 0x80;  // no ULA: sprites over the fallback colour
    }
    /// Attributes through port #57 for one sprite: x, y (grid coordinates), the pattern, flags of byte 2
    void Sprite(unsigned slot, unsigned x, unsigned y, unsigned pattern, uint8_t byte2 = 0, uint8_t byte4 = 0, bool fifth = false)
    {
        _sprites.WriteSlotSelect(static_cast<uint8_t>(slot));
        _sprites.WriteAttribute(x & 0xFF);
        _sprites.WriteAttribute(y & 0xFF);
        _sprites.WriteAttribute(static_cast<uint8_t>(byte2 | ((x >> 8) & 1)));
        _sprites.WriteAttribute(static_cast<uint8_t>(0x80 | (fifth ? 0x40 : 0) | pattern));
        if (fifth)
            _sprites.WriteAttribute(static_cast<uint8_t>(byte4 | ((y >> 8) & 1)));
    }
    void Pattern(unsigned number, unsigned offset, uint8_t value)
    {
        _sprites.WriteSlotSelect(static_cast<uint8_t>(number & 0x3F));
        for (unsigned i = 0; i < offset; i++)
            _sprites.WritePattern(0);  // advance to the byte
        _sprites.WritePattern(value);
    }
    uint32_t SpritePal(unsigned index) { return NextVideoRenderer::Rgba(_regs.PaletteEntry(2, index) & 0x1FF); }
};

TEST_F(NextSprites_Test, EightBitPatternAtTheGridPositionWithTransparentIndex)
{
    // pattern 0: byte 0 = colour 5, byte 1 = the transparent index (0), byte 2 = colour 6
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(5);
    _sprites.WritePattern(0);
    _sprites.WritePattern(6);
    Sprite(0, 100, 50, 0);
    Render(50);
    EXPECT_EQ(Px(200), SpritePal(5)) << "x = 100 on the grid is 200 sub-pixels";
    EXPECT_EQ(Px(201), SpritePal(5));
    EXPECT_EQ(Px(202), NextVideoRenderer::Rgba((0xE3 << 1) | 1)) << "the transparent index shows what is below (the fallback)";
    EXPECT_EQ(Px(204), SpritePal(6));
    Render(49);
    EXPECT_NE(Px(200), SpritePal(5)) << "above the sprite";
}

TEST_F(NextSprites_Test, MirrorRotateAndScale)
{
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(1);                      // (0,0)
    for (int i = 0; i < 15; i++) _sprites.WritePattern(0);
    _sprites.WritePattern(2);                      // (0,1) is at byte 16: first of row 1
    Sprite(0, 100, 50, 0, 0x08);                   // x mirror: pixel (0,0) lands on the right edge
    Render(50);
    EXPECT_EQ(Px((100 + 15) * 2), SpritePal(1));
    Sprite(0, 100, 50, 0, 0x04);                   // y mirror: row 0 shows row 15 (empty): nothing at the left edge
    Render(50);
    EXPECT_NE(Px(200), SpritePal(1));
    Render(65);                                    // row 15 of the picture = pattern row 0
    EXPECT_EQ(Px(200), SpritePal(1));
    Sprite(0, 100, 50, 0, 0x02);                   // rotate (with the x mirror it implies): pattern row 1 becomes column 14 of line 0
    Render(50);
    EXPECT_EQ(Px((100 + 14) * 2), SpritePal(2));
    Sprite(0, 100, 50, 0, 0, 0x08 | 0x02, true);   // 5th byte: x scale 2x (bits 4:3 = 01), y scale 2x (bits 2:1 = 01)
    Render(50);
    EXPECT_EQ(Px(200), SpritePal(1));
    EXPECT_EQ(Px(203), SpritePal(1)) << "a scaled pixel is two grid pixels wide";
    EXPECT_NE(Px(204), SpritePal(1));
    Render(51);
    EXPECT_EQ(Px(200), SpritePal(1)) << "and two lines high";
}

TEST_F(NextSprites_Test, FourBitPatternAndPaletteOffset)
{
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(0x31);  // 4-bit pattern 0: pixels 3, 1
    Sprite(0, 100, 50, 0, 0x20, 0x80, true);  // palette offset 2, 4-bit
    Render(50);
    EXPECT_EQ(Px(200), SpritePal((2 << 4) | 3));
    EXPECT_EQ(Px(202), SpritePal((2 << 4) | 1));
}

TEST_F(NextSprites_Test, RelativeSpriteFollowsItsAnchor)
{
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(7);
    Sprite(0, 100, 50, 0, 0, 0x00, true);          // an anchor (4-bit off, unified off)
    // relative: x +20, y +3, pattern 0 (A4 bits 7:6 = 01)
    _sprites.WriteSlotSelect(1);
    _sprites.WriteAttribute(20);
    _sprites.WriteAttribute(3);
    _sprites.WriteAttribute(0);
    _sprites.WriteAttribute(0xC0);  // visible + fifth byte
    _sprites.WriteAttribute(0x40);  // relative
    Render(53);
    EXPECT_EQ(Px((100 + 20) * 2), SpritePal(7)) << "the relative sprite is at anchor + (20, 3)";
}

TEST_F(NextSprites_Test, CollisionAndStatusPort)
{
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(1);
    Sprite(0, 100, 50, 0);
    Sprite(1, 100, 50, 0);
    EXPECT_EQ(_sprites.ReadStatus(), 0);
    Render(50);
    EXPECT_EQ(_sprites.ReadStatus(), 1) << "two sprites on one pixel: collision";
    EXPECT_EQ(_sprites.ReadStatus(), 0) << "a read clears it";
}

TEST_F(NextSprites_Test, LayerOrderPutsTheSpriteBelowLayer2OrAboveTheUla)
{
    _in.nr[0x68] = 0x00;
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(5);
    Sprite(0, 100, 80, 0);
    const unsigned y = 80;
    Render(y);
    EXPECT_EQ(Px(200), SpritePal(5)) << "SLU: the sprite is on top";
    _in.nr[0x15] = 0x01 | (4 << 2);  // USL: the ULA (paper, opaque) above the sprite
    Render(y);
    EXPECT_NE(Px(200), SpritePal(5));
}

// endregion

// region <ULA clip, stencil, blend>

class NextCompose_Test : public NextVideoRenderer_Test
{
protected:
    void WritePalette(unsigned control, unsigned index, uint8_t rrrgggbb)
    {
        _regs.WritePaletteControl(static_cast<uint8_t>(control));
        _regs.WritePaletteIndex(static_cast<uint8_t>(index));
        _regs.WritePaletteValue8(rrrgggbb);
        _regs.WritePaletteControl(0);
    }
};

// zxula.vhd: o_ula_clipped = '0' when the pixel is inside the window "or border_active = '1'": the border is never clipped
TEST_F(NextCompose_Test, UlaClipWindowMakesWhatItExcludesOfThePaperTransparentAndLeavesTheBorder)
{
    _in.nr[0x4A] = 0x1C;  // green fallback
    _in.border = 2;
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_EQ(Px(10), Pal(0, 16 + 2)) << "no clip: the red border";
    _regs.WriteClipControl(0x02);  // the ULA's window index to 0 (bit 1)
    for (uint8_t v : {uint8_t(100), uint8_t(200), uint8_t(0), uint8_t(191)})
        _regs.WriteClip(2, v);
    Render(NextVideoRenderer::kPaperTop + 10);
    const uint32_t fallback = NextVideoRenderer::Rgba(static_cast<uint16_t>((0x1C << 1) | 0));
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 2 * 50), fallback) << "left of the window: transparent";
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 2 * 150), Pal(0, 16)) << "inside: the paper (black, the attribute is 0)";
    EXPECT_EQ(Px(10), Pal(0, 16 + 2)) << "the left border stays: it is never clipped";
    EXPECT_EQ(Px(630), Pal(0, 16 + 2)) << "and the right one";
    Render(5);
    EXPECT_EQ(Px(300), Pal(0, 16 + 2)) << "and the rows above the paper (a window starting at row 0 or not)";
    // a window that leaves out a row: the paper row is transparent, the border beside it is not
    _regs.WriteClipControl(0x02);
    for (uint8_t v : {uint8_t(0), uint8_t(255), uint8_t(20), uint8_t(191)})
        _regs.WriteClip(2, v);
    Render(NextVideoRenderer::kPaperTop + 3);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 100), fallback);
    EXPECT_EQ(Px(10), Pal(0, 16 + 2));
}

TEST_F(NextCompose_Test, StencilAndsTheUlaAndTheTilemapChannels)
{
    // ULA paper white (index 16 + 7), a tile pixel of index 1 coloured 0b110'101'10, stencil on
    WritePalette(0x30, 1, 0xB6);  // tilemap palette 1st: entry 1 = 101 101 10
    _in.nr[0x6B] = 0x81;          // tilemap on, tile above
    _in.nr[0x68] = 0x01;          // stencil
    _in.nr[0x6E] = 0x10;
    _in.nr[0x6F] = 0x20;
    _in.nr[0x4C] = 0x0F;
    const unsigned entry = (4 * 40 + 4) * 2;
    Page(5)[0x1000 + entry] = 0;
    Page(5)[0x1000 + entry + 1] = 0;
    Page(5)[0x2000] = 0x10;  // tile 0 row 0: pixel 0 = 1
    Page(5)[0x1800] = 0x38;  // paper white
    Render(NextVideoRenderer::kPaperTop);
    const uint16_t tile = _regs.PaletteEntry(3, 1), white = _regs.PaletteEntry(0, 16 + 7);
    const auto r = [](uint16_t c) { return (c >> 6) & 7; };
    const auto g = [](uint16_t c) { return (c >> 3) & 7; };
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft) & 0xFF, NextVideoRenderer::Rgba((r(tile) & r(white)) << 6 | (g(tile) & g(white)) << 3 | (((tile >> 1) & 3) & ((white >> 1) & 3)) << 1) & 0xFF)
        << "stencil: the red channel is the AND of both";
}

TEST_F(NextCompose_Test, AdditiveBlendMixesLayer2WithTheUlaAndClamps)
{
    _in.layer2Enable = true;
    _in.nr[0x15] = 6 << 2;  // mode 110: additive
    _in.nr[0x14] = 0xE3;
    WritePalette(0x10, 0x81, 0x24);   // Layer 2 pixel colour: 001 001 00
    Page(8)[0] = 0x81;
    _in.nr[0x68] = 0x00;
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0x00 | 0x08 | 0x01;  // ULA ink blue (1) paper black... pixel 0 = ink
    Render(NextVideoRenderer::kPaperTop);
    const uint16_t ula = _regs.PaletteEntry(0, 1), l2 = _regs.PaletteEntry(1, 0x81);
    const unsigned expected = std::min(7u, ((ula >> 6) & 7u) + ((l2 >> 6) & 7u));
    const uint32_t px = Px(NextVideoRenderer::kPaperLeft);
    const uint32_t want = NextVideoRenderer::Rgba(static_cast<uint16_t>((expected << 6) | (std::min(7u, ((ula >> 3) & 7u) + ((l2 >> 3) & 7u)) << 3) |
                                                  (std::min(3u, ((ula >> 1) & 3u) + ((l2 >> 1) & 3u)) << 1) | 0));
    EXPECT_EQ(px & 0x0000FFFF, want & 0x0000FFFF) << "red and green channels add";
}

// endregion

// region <The VHDL's details: Timex modes, scrolls, Layer 2 areas, tilemap 80 columns, the sprite engine's flags (2026-10-12)>

class NextVhdlDetails_Test : public NextCompose_Test
{
protected:
    /// The Layer 2 palette entry `index` set to a colour no other layer has here, returned as the painted pixel
    uint32_t Layer2Colour(unsigned index, uint8_t rrrgggbb = 0x24)
    {
        WritePalette(0x10, index, rrrgggbb);
        return Pal(1, index);
    }
    void ClipLayer2(uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2)
    {
        for (uint8_t v : {x1, x2, y1, y2})
            _regs.WriteClip(0, v);
    }
};

// zxula.vhd: the bitmap fetch is mode(0) & addr_p, the attribute fetch is '1' & addr_p when mode(1) (hi-colour) and mode(0) & "110" &
// addr_a otherwise; hi-res (mode(2)) loads that "attribute" byte as the second 8 pixels of the column
TEST_F(NextVhdlDetails_Test, TimexModesFetchWhatTheVhdlAddressesSay)
{
    const unsigned left = NextVideoRenderer::kPaperLeft;
    // mode 3: hi-colour with screen 1: the bitmap AND the attribute are both read at #2000 (the same byte)
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0x07;
    Page(5)[0x2000] = 0x4A;  // bitmap 0100 1010: pixel 1 set; attribute: bright, paper 1, ink 2
    _in.portFf = 3;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 16 + 1 + 8)) << "pixel 0 of byte #4A is paper (bright blue)";
    EXPECT_EQ(Px(left + 2), Pal(0, 2 + 8)) << "pixel 1 is ink (bright red): the attribute comes from the bitmap's own offset";

    // mode 4: the second byte of the column is read from the attribute area (screen 0)
    Page(5)[0x1800] = 0xA5;     // 1010 0101
    _in.portFf = 4 | (5 << 3);  // ink 5, paper 2
    Render(NextVideoRenderer::kPaperTop);
    const uint32_t ink = Pal(0, 5 + 8), paper = Pal(0, 16 + 2 + 8);
    EXPECT_EQ(Px(left), ink) << "bitmap byte #80: pixel 0";
    EXPECT_EQ(Px(left + 1), paper);
    EXPECT_EQ(Px(left + 8), ink) << "the attribute byte #A5 as pixels: bit 7";
    EXPECT_EQ(Px(left + 9), paper);
    EXPECT_EQ(Px(left + 10), ink);

    // mode 5: bitmap from screen 1, the "attribute" from screen 1's attribute area (#3800)
    Page(5)[0x2000] = 0x80;
    Page(5)[0x3800] = 0x01;
    _in.portFf = 5 | (5 << 3);
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), ink);
    EXPECT_EQ(Px(left + 8), paper);
    EXPECT_EQ(Px(left + 15), ink);

    // mode 7: both bytes from screen 1's bitmap, the same byte twice
    _in.portFf = 7 | (5 << 3);
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), ink);
    EXPECT_EQ(Px(left + 8), ink) << "#80 again";
    EXPECT_EQ(Px(left + 9), paper);

    // the shadow screen has no Timex modes (an 8K BRAM): a hi-res port #FF draws the standard screen of bank 7
    Page(7)[0] = 0x80;
    Page(7)[0x1800] = 0x07;
    _in.shadowScreen = true;
    _in.portFf = 6 | (5 << 3);
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 7)) << "pixel 0: ink 7";
    EXPECT_EQ(Px(left + 2), Pal(0, 16)) << "a pixel is two sub-pixels again";
    Render(0);
    EXPECT_EQ(Px(0), Pal(0, 16)) << "the border is the port #FE colour (0), not hi-res's paper";
}

// ULANext in hi-res: the attribute is the same synthesised byte, the border takes 128 + paper
TEST_F(NextVhdlDetails_Test, HiResWithUlaNextTakesTheIndexesFromTheSynthesisedAttribute)
{
    _regs.WritePaletteControl(0x01);
    _regs.WriteUlaNextFormat(0x07);
    _in.portFf = 6 | (2 << 3);  // ink 2, paper 5: attribute 0x40 | 5 << 3 | 2
    Page(5)[0] = 0x80;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 2)) << "ink: attribute & 7";
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 1), Pal(0, 128 + (0x6A >> 3))) << "paper: 128 + attribute >> 3";
    Render(0);
    EXPECT_EQ(Px(0), Pal(0, 128 + 5)) << "the border: 128 + paper";
}

// zxula.vhd px = fine & (hc(7:3) + scroll_x(7:3)) & scroll_x(2:0): the shift register is moved by 2 * scroll(2:0) + fine bits
TEST_F(NextVhdlDetails_Test, UlaFineScrollMovesByHalfAPixelAndHiResByScrollTimesTwo)
{
    const unsigned left = NextVideoRenderer::kPaperLeft;
    Page(5)[0] = 0x80;
    Page(5)[0x1800] = 0x07;
    _in.nr[0x68] = 0x04;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 7)) << "sub-pixel 0 shows pixel 0";
    EXPECT_EQ(Px(left + 1), Pal(0, 16)) << "and sub-pixel 1 already pixel 1: the picture moved left by half a pixel";
    EXPECT_EQ(Px(left + 511), Pal(0, 7)) << "the last sub-pixel is pixel 256 = column 0 again";
    _in.nr[0x68] = 0;
    // hi-res: pixel 2 of the stream set; scroll 1 moves it to sub-pixel 0, the fine scroll adds one more
    Page(5)[0] = 0x20;
    _in.portFf = 6 | (5 << 3);
    _in.nr[0x26] = 1;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 5 + 8));
    EXPECT_EQ(Px(left + 1), Pal(0, 16 + 2 + 8));
    _in.nr[0x68] = 0x04;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(left), Pal(0, 16 + 2 + 8));
    EXPECT_EQ(Px(left + 511), Pal(0, 5 + 8)) << "moved by one more clock: it wrapped to the last sub-pixel";
}

// zxula.vhd py: 192-255 fold to 0-63, 256-383 to 64-191, 384-446 to 0-62: always scroll + line modulo 192
TEST_F(NextVhdlDetails_Test, UlaYScrollAbove191IsAModulo192)
{
    Page(5)[0x40] = 0x80;  // row 16 of the bitmap
    Page(5)[0x1840] = 0x07;
    _in.nr[0x27] = 250;
    Render(NextVideoRenderer::kPaperTop + 150);  // 150 + 250 = 400: 400 - 384 = 16
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 7));
    Render(NextVideoRenderer::kPaperTop + 191);  // 441 - 384 = 57: the last scroll value's folded row
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 16)) << "row 57 is empty";
}

// lores.vhd reads bank 5 whatever the shadow screen bit says, and its Y has no modulo: y(7:6) + 1 from 192, rows 192-254 follow
TEST_F(NextVhdlDetails_Test, LoResReadsBank5AndItsYScrollRunsOnPast191)
{
    _in.nr[0x15] = 0x80;
    Page(5)[5] = 0x33;
    Page(7)[5] = 0x77;
    _in.shadowScreen = true;
    Render(NextVideoRenderer::kPaperTop);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 5 * 4), Pal(0, 0x33)) << "bank 5 with the shadow screen on";
    _in.shadowScreen = false;
    _in.nr[0x33] = 250;
    Page(5)[8 * 128] = 0x44;  // what a modulo 192 would show (y = 400 % 192 = 16, row 8)
    Page(5)[0x3C00] = 0x55;   // y_pre 400: y(7:6) = 10 + 1, y = 208, row 104, addr 13:11 = 6 + 1
    Render(NextVideoRenderer::kPaperTop + 150);
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft), Pal(0, 0x55));
}

// layer2.vhd: hc_valid / vc_valid decide the pixel, the clip is hc_eff >= x1 and <= x2 and vc_eff >= y1 and <= y2 in 640 mode as well
TEST_F(NextVhdlDetails_Test, Layer2In640ModeFollowsTheClipInTheLowerScreen)
{
    _in.layer2Enable = true;
    _in.nr[0x70] = 0x20;  // 640 x 256 x 4
    const uint32_t colour = Layer2Colour(0x0A);
    Page(8)[5 * 256 + 200] = 0xA3;  // x = 10, 11 at y = 200
    Render(200);
    EXPECT_NE(Px(10), colour) << "the default clip window ends at row 191";
    ClipLayer2(0, 159, 0, 255);
    Render(200);
    EXPECT_EQ(Px(10), colour) << "opened to 255: row 200 shows";
    ClipLayer2(0, 159, 0, 199);
    Render(200);
    EXPECT_NE(Px(10), colour) << "y2 = 199 excludes it again, y2 is inclusive below";
    ClipLayer2(0, 159, 200, 255);
    Render(200);
    EXPECT_EQ(Px(10), colour);
    ClipLayer2(3, 2, 0, 255);  // x1 > x2: nothing
    Render(200);
    EXPECT_NE(Px(10), colour);
}

// the 256 x 192 mode: hc_valid needs hc_eff(8) = 0, vc_valid vc_eff < 192: no pixel in the border, whatever the window
TEST_F(NextVhdlDetails_Test, Layer2In256x192NeverDrawsInTheBorderAndThe320ModeDoes)
{
    _in.layer2Enable = true;
    const uint32_t colour = Layer2Colour(0x55);
    for (unsigned i = 0; i < 3 * 0x4000; i++)
        Page(8)[i] = 0x55;
    ClipLayer2(0, 255, 0, 255);
    Render(NextVideoRenderer::kPaperTop - 1);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 10), colour) << "above the paper";
    Render(NextVideoRenderer::kPaperTop + 192);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 10), colour) << "below the paper";
    Render(NextVideoRenderer::kPaperTop + 10);
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft - 2), colour) << "left of the paper";
    EXPECT_NE(Px(NextVideoRenderer::kPaperLeft + 512), colour) << "right of the paper";
    EXPECT_EQ(Px(NextVideoRenderer::kPaperLeft + 10), colour);
    // 320 x 256: the border is part of the picture (the default clip window is 0-255 x 0-191, widen it)
    _in.nr[0x70] = 0x10;
    for (unsigned i = 0; i < 5 * 0x4000; i++)
        Page(8)[i] = 0x55;
    ClipLayer2(0, 159, 0, 255);
    Render(3);
    EXPECT_EQ(Px(10), colour);
    Render(255);
    EXPECT_EQ(Px(NextVideoRenderer::kWidth - 1), colour);
}

// layer2.vhd x(8:6) + 3 from 320 on: a scroll of 400 at hc 300 (x_pre 700 = 0x2BC) reads column 380, not 60
TEST_F(NextVhdlDetails_Test, Layer2In320ModeScrollAbove319ReadsTheColumnsAfterTheBitmap)
{
    _in.layer2Enable = true;
    _in.nr[0x70] = 0x10;
    const uint32_t colour = Layer2Colour(0x66);
    Layer2Colour(0x77, 0x1C);
    ClipLayer2(0, 159, 0, 255);
    _in.nr[0x16] = 400 & 0xFF;
    _in.nr[0x71] = 1;
    Page(8)[380 * 256 + 7] = 0x66;
    Page(8)[60 * 256 + 7] = 0x77;  // what a modulo 320 would show
    Render(7);
    EXPECT_EQ(Px(300 * 2), colour);
    // a scroll in the valid range stays a modulo 320: 100 + 250 = 350 -> column 30
    _in.nr[0x16] = 100;
    _in.nr[0x71] = 0;
    Page(8)[30 * 256 + 7] = 0x66;
    Render(7);
    EXPECT_EQ(Px(250 * 2), colour);
}

// tilemap.vhd: 80 columns count in 14 MHz pixels (hcount(10:1)), so a scroll unit is half a 7 MHz pixel, and it wraps at 640
TEST_F(NextTilemap_Test, EightyColumnScrollIsInHalfPixelsAndWrapsAt640)
{
    _in.nr[0x6B] = 0xC0;  // on, 80 columns
    Map()[0] = 1;         // column 0: tile 1
    Defs()[32] = 0x12;    // row 0: pixel 0 = 1, pixel 1 = 2
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, 1)) << "a tile pixel is one sub-pixel in 80 columns";
    EXPECT_EQ(Px(1), Pal(3, 2));
    _in.nr[0x30] = 1;
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, 2)) << "scroll 1 = one 14 MHz pixel";
    // scroll 639: the last pixel of column 79 comes first
    Map()[79 * 2] = 2;
    Defs()[64 + 3] = 0x0C;  // tile 2, row 0, pixels 6 / 7 = 0 / 12
    _in.nr[0x2F] = 2;
    _in.nr[0x30] = 0x7F;
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, 12));
    // 40 columns: one unit is a whole 7 MHz pixel (two sub-pixels), wrapping at 320
    _in.nr[0x6B] = 0x80;
    _in.nr[0x2F] = 0;
    _in.nr[0x30] = 1;
    Render(0);
    EXPECT_EQ(Px(0), Pal(3, 2));
    EXPECT_EQ(Px(1), Pal(3, 2));
}

// sprites.vhd: the engine writes the whole line buffer (x 0-319) and the collision compares there; the clip window and the 224-line limit
// are applied where the buffer is read, so overlaps outside them still flag; and NR #15 bit 0 only gates the output
TEST_F(NextSprites_Test, CollisionIgnoresTheClipWindowTheLowerBorderAndTheEnableBit)
{
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(1);
    Sprite(0, 100, 230, 0);
    Sprite(1, 100, 230, 0);
    Render(230);  // the paper ends at 224: invisible without the over-border bit, but the engine ran
    EXPECT_EQ(_sprites.ReadStatus(), 1);
    EXPECT_NE(Px(200), SpritePal(1)) << "and nothing is drawn there";
    Sprite(0, 100, 50, 0);
    Sprite(1, 100, 50, 0);
    _regs.WriteClip(1, 0);  // sprite clip: a window of x 0-10, rows 0-191 (paper coordinates)
    _regs.WriteClip(1, 10);
    _regs.WriteClip(1, 0);
    _regs.WriteClip(1, 191);
    Render(50);
    EXPECT_EQ(_sprites.ReadStatus(), 1) << "overlap outside the window";
    EXPECT_NE(Px(200), SpritePal(1));
    _in.nr[0x15] = 0x00;  // sprites off
    Render(50);
    EXPECT_EQ(_sprites.ReadStatus(), 1) << "the engine runs with the sprites off";
    // a lone pair of sprites at x >= 320 never writes the buffer
    Sprite(0, 330, 50, 0);
    Sprite(1, 335, 50, 0);
    Render(50);
    EXPECT_EQ(_sprites.ReadStatus(), 0);
}

// sprites.vhd: a sprite continues past 320 only from x = 496 (unscaled), and then wraps to 0 (the 512 space)
TEST_F(NextSprites_Test, SpritesWrapAroundThe512Space)
{
    _in.nr[0x15] = 0x03;  // on, over the border, no clip: the whole 320 x 256 grid
    _sprites.WriteSlotSelect(0);
    for (int i = 0; i < 256; i++)
        _sprites.WritePattern(9);
    Sprite(0, 500, 50, 0);  // 500 + 12 = 512 = 0
    Render(50);
    EXPECT_EQ(Px(0), SpritePal(9)) << "pixel 12 of the sprite lands on x = 0";
    EXPECT_EQ(Px(7), SpritePal(9)) << "up to x = 3";
    EXPECT_NE(Px(8), SpritePal(9));
    Sprite(0, 490, 50, 0);  // 480-494 stop at once: nothing wraps
    Render(50);
    EXPECT_NE(Px(0), SpritePal(9));
    // a vertical wrap: y = 500 shows its rows 12-15 on the lines 0..3 (the 9th Y bit is in the fifth attribute byte)
    Sprite(0, 100, 500, 0, 0, 0, true);
    Render(2);
    EXPECT_EQ(Px(200 + 10), SpritePal(9));
    Render(4 + 16);
    EXPECT_NE(Px(200 + 10), SpritePal(9));
}

// sprites.vhd: 1 clock S_START + 1 per sprite + 1 per pixel of a sprite on the line; "too many" when the line period is used up
// (>= 4 * 448 = 1792, or 1824 on the 128K timing), and the pixels after the last clock are not drawn
TEST_F(NextSprites_Test, TooManyFlagAndTheCutOffFollowTheLineClocks)
{
    _sprites.WriteSlotSelect(0);
    for (int i = 0; i < 16; i++)
        _sprites.WritePattern(5);
    // 103 sprites of 16 pixels: 129 + 103 * 16 = 1777 clocks
    for (unsigned i = 0; i < 103; i++)
        Sprite(i, 40, 50, 0);
    Render(50);
    EXPECT_EQ(_sprites.ReadStatus(), 1) << "1777 < 1792: no \"too many\" (the stacked sprites collide)";
    // the 104th, the last in the order: 1793; its last pixel is the clock 1793 (> 1792) and is lost
    Sprite(127, 100, 50, 0);
    Render(50);
    EXPECT_EQ(Px(2 * 114), SpritePal(5)) << "pixel 14 is the clock 1792";
    EXPECT_NE(Px(2 * 115), SpritePal(5)) << "pixel 15 would be the clock 1793";
    EXPECT_EQ(_sprites.ReadStatus(), 3);
    // 128K timing: 1824 clocks
    _in.lineClocks = 1824;
    Render(50);
    EXPECT_EQ(Px(2 * 115), SpritePal(5));
    EXPECT_EQ(_sprites.ReadStatus(), 1);
    // 106 sprites: 129 + 106 * 16 = 1825 >= 1824
    Sprite(125, 200, 50, 0);
    Sprite(126, 200, 50, 0);
    Render(50);
    EXPECT_EQ(_sprites.ReadStatus(), 3);
}

// zxnext.vhd: when the ULA is the border and the tilemap pixel is transparent a sprite is not hidden by the ULA in the orders LUS / USL / ULS
// (the weird border exception); in the paper the ULA wins
TEST_F(NextSprites_Test, SpriteInTheBorderShowsThroughTheUlaInTheOrdersWithTheUlaBeforeIt)
{
    _in.nr[0x68] = 0x00;
    _in.nr[0x15] = 0x03 | (3 << 2);  // sprites on, over the border, LUS
    _in.border = 2;
    _sprites.WriteSlotSelect(0);
    _sprites.WritePattern(5);
    Sprite(0, 10, 10, 0);   // the grid border (paper starts at 32)
    Sprite(1, 100, 80, 0);  // in the paper
    Render(10);
    EXPECT_EQ(Px(20), SpritePal(5)) << "border: the sprite shows";
    Render(80);
    EXPECT_NE(Px(200), SpritePal(5)) << "paper: the opaque ULA is above the sprite in LUS";
    _in.nr[0x15] = 0x03 | (0 << 2);  // SLU
    Render(80);
    EXPECT_EQ(Px(200), SpritePal(5));
}

// endregion

// region <Compositor digest>

// Random machine states through the whole line renderer, with and without the layer capture: every layer order (NR #15 bits 4:2,
// blend modes 110 / 111), the stencil and blend bits (NR #68), the tilemap, LoRes, the Timex modes, palettes with Layer 2 priority
// colours, clip windows and sprites. The digest of the output pixels, the captured planes, winner ids and sprite numbers is pinned:
// a change to the compositor or a layer function that moves one pixel changes it. Recorded on the per-pixel string-walk compositor
// before its order was resolved per line (2026-10-13)
TEST_F(NextVideoRenderer_Test, RandomStatesDigestIsUnchanged)
{
    uint64_t digest = 1469598103934665603ull;
    auto mix = [&](const void* data, size_t bytes) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; i++)
            digest = (digest ^ p[i]) * 1099511628211ull;
    };
    uint32_t seed = 0x5EED1234u;
    auto next = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    NextSprites sprites;
    std::vector<NextLayer::Pixel> planes(NextLayer::kPlanes * NextVideoRenderer::kWidth);
    std::vector<uint8_t> ids(NextVideoRenderer::kWidth), info(NextVideoRenderer::kWidth);
    for (unsigned state = 0; state < 72; state++)
    {
        // The second half: lines with nothing above the ULA (Layer 2, the tilemap and the sprites off, alone or together), which the
        // compositor draws by its ULA-only path. Its digest was recorded with that path switched off
        const bool quiet = state >= 48;
        if (state == 48)
        {
            EXPECT_EQ(digest, 0xD15149A1F677E1AEull) << "the general compositor";
        }
        // (the quiet half keeps the last memory and palettes: filling them is most of the test's time)
        for (uint8_t& b : _ram)
        {
            if (quiet)
                break;
            b = static_cast<uint8_t>(next());
        }
        for (unsigned palette = 0; palette < 8 && !quiet; palette++)
        {
            _regs.WritePaletteControl(static_cast<uint8_t>(palette << 4));
            _regs.WritePaletteIndex(0);
            for (unsigned i = 0; i < 256; i++)
            {
                const uint32_t r = next();
                _regs.WritePaletteValue9(static_cast<uint8_t>(r));
                _regs.WritePaletteValue9(static_cast<uint8_t>((r >> 8) & ((r >> 16) % 5 == 0 ? 0x81 : 0x01)));
            }
        }
        _regs.WritePaletteControl(static_cast<uint8_t>(next() & 0x0F));
        _regs.WritePortFf(static_cast<uint8_t>(next()));
        _regs.WriteUlaNextFormat(static_cast<uint8_t>(next()));
        for (unsigned w = 0; w < NextVideoRegs::kClipWindows; w++)
        {
            _regs.WriteClipControl(static_cast<uint8_t>(1u << w));
            for (unsigned c = 0; c < 4; c++)
                _regs.WriteClip(w, static_cast<uint8_t>(next() % 3 == 0 ? next() : (c & 1 ? 0xFF : 0x00)));
        }
        for (unsigned r = 0; r < 256; r++)
            _in.nr[r] = static_cast<uint8_t>(next());
        _in.nr[0x12] = static_cast<uint8_t>(next() % 10);  // Layer 2 banks inside the 16 pages
        _in.nr[0x13] = static_cast<uint8_t>(next() % 10);
        _in.nr[0x15] = static_cast<uint8_t>((next() & 0x9F) | ((state % 8) << 2));  // every order and blend mode in turn
        _in.nr[0x6E] = static_cast<uint8_t>(next() & 0x3F);
        _in.nr[0x6F] = static_cast<uint8_t>(next() & 0x3F);
        _in.border = static_cast<uint8_t>(next() & 7);
        _in.portFf = static_cast<uint8_t>(next() & 0x3F);
        _in.shadowScreen = (next() & 3) == 0;
        _in.layer2Enable = (next() & 1) != 0;
        _in.flash = (next() & 1) != 0;
        if (quiet)
        {
            const unsigned off = state % 4 == 0 ? 7 : (next() & 7);  // which of Layer 2 / tilemap / sprites are off: all three every 4th state
            if (off & 1)
                _in.layer2Enable = false;
            if (off & 2)
                _in.nr[0x6B] &= 0x7F;
            if (state % 3 == 0)
                _in.nr[0x68] &= 0x7E;  // the ULA on, no stencil
        }
        sprites.Reset();
        for (unsigned s = 0; s < 128; s++)
        {
            // through the attribute port: four bytes, a fifth when byte 3 bit 6 says so; two sprites in three are visible
            sprites.WriteSlotSelect(static_cast<uint8_t>(s));
            uint8_t bytes[5];
            for (uint8_t& v : bytes)
                v = static_cast<uint8_t>(next());
            if (next() % 3 == 0 || (quiet && state % 2 == 0))
                bytes[3] &= 0x7F;
            for (unsigned i = 0; i < ((bytes[3] & 0x40) ? 5u : 4u); i++)
                sprites.WriteAttribute(bytes[i]);
        }
        sprites.WriteSlotSelect(0);
        for (unsigned i = 0; i < 0x4000; i++)
            sprites.WritePattern(static_cast<uint8_t>(next() % 4 == 0 ? 0xE3 : next()));
        _in.sprites = quiet && state % 8 == 4 ? nullptr : &sprites;
        for (unsigned k = 0; k < 12; k++)
        {
            const unsigned y = next() % NextVideoRenderer::kHeight;
            const bool record = (k & 1) != 0;
            NextLayer::LineSink sink;
            for (unsigned p = 0; p < NextLayer::kPlanes; p++)
                sink.plane[p] = planes.data() + p * NextVideoRenderer::kWidth;
            sink.ids = ids.data();
            sink.info = info.data();
            _in.layerOut = record ? &sink : nullptr;
            Render(y);
            mix(_line.data(), _line.size() * sizeof(uint32_t));
            if (record)
            {
                mix(planes.data(), planes.size() * sizeof(NextLayer::Pixel));
                mix(ids.data(), ids.size());
                mix(info.data(), info.size());
            }
        }
        _in.layerOut = nullptr;
    }
    EXPECT_EQ(digest, 0x9E17D7C1D3AC0ADAull);
}

// endregion
