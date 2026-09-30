// TS-Conf video (TSConf implementation-plan phase 3 VID items, GFX-3 for TXT;
// hardware-spec §4): geometry, CRAM colors, the ZX palette index, border, TXT.

#include "tsconffixture.h"

#include "emulator/video/tsconf/screentsconf.h"

class ScreenTSConf_Test : public TsConfFixture
{
protected:
    ScreenTSConf* Screen() { return dynamic_cast<ScreenTSConf*>(_context->pScreen); }

    /// Run the engine through one whole frame (every line latches the
    /// current registers), draw it and read framebuffer pixel (x, y)
    uint32_t PixelAfterFrame(uint32_t x, uint32_t y)
    {
        TsConfEngine& engine = _decoder->GetEngine();
        engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        engine.CatchUp(TsConfEngine::kFrameTacts - 1);
        Screen()->InitRaster();
        Screen()->RenderFrameBatch();
        uint32_t* buffer = nullptr;
        size_t size = 0;
        Screen()->GetFramebufferData(&buffer, &size);
        return buffer[y * 720 + x];
    }

    /// Framebuffer x of raster dot `dot` (2 px per dot from dot 88), y of raster line
    static uint32_t Fx(uint32_t dot) { return (dot - 88) * 2; }
    static uint32_t Fy(uint32_t line) { return line - 32; }
};

/// VID-1: one 720x288 geometry; ZX mode at reset
TEST_F(ScreenTSConf_Test, VID1_Geometry)
{
    ASSERT_NE(Screen(), nullptr);
    Screen()->InitRaster();
    EXPECT_EQ(Screen()->GetVideoMode(), M_TSZX);
    uint32_t* buffer = nullptr;
    size_t size = 0;
    Screen()->GetFramebufferData(&buffer, &size);
    EXPECT_EQ(size, 720u * 288u * 4u);

    Reg(TsConfReg::VConfig, 0x03);
    EXPECT_EQ(Screen()->GetVideoMode(), M_TSTX);
}

/// hs §4.3: the no-VDAC PWM levels: ZX normal (0x10) and bright (0x18) channels
TEST_F(ScreenTSConf_Test, CramColors)
{
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x0000), 0xFF000000u);
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x0010), 0xFFAA0000u) << "blue, normal level: 2/3";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x6318), 0xFFFFFFFFu) << "bright white saturates";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x7FFF), 0xFFFFFFFFu);
}

/// VID-3: ZX palette index {PAL_SEL[3:0], BRIGHT, ink}
TEST_F(ScreenTSConf_Test, VID3_ZxPaletteIndex)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::PalSel, 0x02);
    ts.cram[0x29] = 0x7C00;                 // red
    Ram(5, 0x0000) = 0x80;                  // first pixel of the screen set
    Ram(5, 0x1800) = 0x41;                  // bright, ink 1, paper 0
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x7C00));
    EXPECT_EQ(PixelAfterFrame(Fx(140) + 2, Fy(80)), ScreenTSConf::CramToRgba(ts.cram[0x28])) << "paper";
}

/// VID-4: BORDER from #FE uses PAL_SEL; drawn outside the window
TEST_F(ScreenTSConf_Test, VID4_Border)
{
    Reg(TsConfReg::PalSel, 0x0A);
    Out(0x00FE, 0x05);
    _decoder->GetState().cram[0xA5] = 0x03E0;  // green
    EXPECT_EQ(PixelAfterFrame(0, 0), ScreenTSConf::CramToRgba(0x03E0));
    EXPECT_EQ(PixelAfterFrame(Fx(139), Fy(100)), ScreenTSConf::CramToRgba(0x03E0)) << "left of the rres 0 window";
}

/// GFX-3: TXT - char + attr at row 0, font from V_PAGE ^ 1, 14 MHz pixels
TEST_F(ScreenTSConf_Test, GFX3_TextMode)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0x03);          // TXT, rres 0
    Reg(TsConfReg::VPage, 0x10);
    Ram(0x10, 0x0000) = 'A';
    Ram(0x10, 0x0080) = 0x1E;               // paper 1, ink 14
    Ram(0x11, 'A' * 8 + 0) = 0x80;          // font: leftmost pixel of line 0
    ts.cram[0xFE] = 0x001F;
    ts.cram[0xF1] = 0x7C00;
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x001F)) << "ink {PAL_SEL, attr[3:0]}";
    EXPECT_EQ(PixelAfterFrame(Fx(140) + 1, Fy(80)), ScreenTSConf::CramToRgba(0x7C00)) << "second hires pixel: paper";
}

/// Mode 1 (16C): high nibble = left pixel
TEST_F(ScreenTSConf_Test, GFX1_SixteenColors)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0x01);
    Reg(TsConfReg::VPage, 0x08);
    Reg(TsConfReg::PalSel, 0x03);
    Ram(0x08, 0x0000) = 0x12;
    ts.cram[0x31] = 0x0001;
    ts.cram[0x32] = 0x0002;
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x0001));
    EXPECT_EQ(PixelAfterFrame(Fx(141), Fy(80)), ScreenTSConf::CramToRgba(0x0002));
}

/// VID-5: ZX in rres 3 fills 360x288 from (88, 32); columns wrap at 32 bytes,
/// so graphics x 256 shows byte column 0 again
TEST_F(ScreenTSConf_Test, VID5_ZxInFullWindowWrapsColumns)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0xC0);  // ZX, rres 3
    Ram(5, 0x0000) = 0x80;          // row 0, column 0: leftmost pixel set
    Ram(5, 0x1800) = 0x02;          // ink 2, paper 0
    ts.cram[0xF2] = 0x7C00;
    EXPECT_EQ(PixelAfterFrame(Fx(88), Fy(32)), ScreenTSConf::CramToRgba(0x7C00)) << "window origin";
    EXPECT_EQ(PixelAfterFrame(Fx(88 + 256), Fy(32)), ScreenTSConf::CramToRgba(0x7C00)) << "column wrap";
    EXPECT_EQ(PixelAfterFrame(Fx(89), Fy(32)), ScreenTSConf::CramToRgba(ts.cram[0xF0])) << "paper";
}

/// AUTO-1: the screen report names the TS mode with its geometry, its format
/// and the RAM pages it reads
TEST_F(ScreenTSConf_Test, AUTO1_ScreenModeReport)
{
    Reg(TsConfReg::VConfig, 0x41);  // 16C, 320x200
    Reg(TsConfReg::VPage, 0x0A);
    ScreenState s = Screen()->DescribeScreenState();
    EXPECT_EQ(s.videoMode, "TS16 320x200");
    EXPECT_EQ(s.width, 320);
    EXPECT_EQ(s.height, 200);
    EXPECT_EQ(s.format.bpp, 4);
    ASSERT_EQ(s.activeRamPages.size(), 8u);
    EXPECT_EQ(s.activeRamPages[0], 0x08);

    Reg(TsConfReg::VConfig, 0x83);  // TXT, 320x240
    s = Screen()->DescribeScreenState();
    EXPECT_EQ(s.videoMode, "TSTX 320x240");
    EXPECT_EQ(s.format.textColumns, 80);
    EXPECT_EQ(s.format.textRows, 30);

    Reg(TsConfReg::VConfig, 0xC2);
    EXPECT_EQ(Screen()->DescribeScreenState().videoMode, "TS256 360x288");
    Reg(TsConfReg::VConfig, 0x00);
    EXPECT_EQ(Screen()->DescribeScreenState().videoMode, "TSZX 256x192");
}
