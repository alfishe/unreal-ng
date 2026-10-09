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

TEST_F(NextCompose_Test, UlaClipWindowMakesWhatItExcludesTransparentIncludingTheBorderBesideIt)
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
    EXPECT_EQ(Px(10), fallback) << "the left border goes with the clipped left side";
    EXPECT_EQ(Px(630), fallback) << "and the right one";
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
