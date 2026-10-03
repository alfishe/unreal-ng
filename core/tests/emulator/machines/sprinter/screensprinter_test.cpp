// ScreenSprinter / SprinterVideoRenderer: the Sprinter picture from a hand-built
// video RAM (Sprinter test-plan §2.4, T-VID-1..7 and 9; tdd-video §2-§4).
//
// Every test builds the mode table, the source bytes and the pens it needs in
// video RAM and renders the whole frame (RenderFrameBatch) or the beam up to a
// T-state (UpdateScreen), then reads the 736x288 framebuffer. No ROM, no turbo.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "emulator/state/devicestate.h"
#include "emulator/video/map/videomapservice.h"
#include "emulator/video/screendigest.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/sprinter/sprintervideomapper.h"
#include "emulator/video/sprinter/sprintervideoram.h"
#include "emulator/video/sprinter/sprintervideorenderer.h"
#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/video/zx/screenzx.h"
#include "_helpers/emulatortesthelper.h"
#include "sprinterfixture.h"
#include "sprintermodetable.h"

namespace
{
constexpr uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
}

/// A recognisable colour for every pen: red = entry, green = palette x 32, blue = #5A
constexpr uint32_t PenColor(uint32_t pen)
{
    return Rgba(static_cast<uint8_t>(pen & 0xFF), static_cast<uint8_t>((pen >> 8) * 32), 0x5A);
}
}  // namespace

class ScreenSprinter_Test : public SprinterFixture
{
protected:
    ScreenSprinter* _screen = nullptr;
    SprinterVideoRam* _vram = nullptr;

    void SetUp() override
    {
        SprinterFixture::SetUp();
        _screen = dynamic_cast<ScreenSprinter*>(_context->pScreen);
        ASSERT_NE(_screen, nullptr) << "VideoController::CreateScreen must give the Sprinter its ScreenSprinter";
        _vram = &_decoder->GetVideoRam();
        ASSERT_EQ(_screen->GetVideoMode(), M_SPRINTER);
        _context->emulatorState.frame_counter = 0;

        // Every pen distinct (bytes R, G, B at row n, column #3E0 + 4k), then the mode
        // table "graphics 320, palette 0, source (0, 0)" everywhere
        for (uint32_t pen = 0; pen < SprinterVideoRam::kPens; pen++)
            SetPen(pen, PenColor(pen));
        for (uint8_t page = 0; page < 2; page++)
            for (uint8_t a = 0; a < 56; a++)
                for (uint8_t b = 0; b < 40; b++)
                    SetMode(a, b, page, 0x20, 0x00, 0x00);
    }

    void SetPen(uint32_t pen, uint32_t rgba)
    {
        const uint32_t address = SprinterVideoRam::PenAddress(pen);
        _vram->Write(address, static_cast<uint8_t>(rgba));
        _vram->Write(address + 1, static_cast<uint8_t>(rgba >> 8));
        _vram->Write(address + 2, static_cast<uint8_t>(rgba >> 16));
    }

    void SetMode(uint8_t a, uint8_t b, uint8_t page, uint8_t m0, uint8_t m1, uint8_t m2, bool line2 = false)
    {
        const uint32_t address = SprinterVideoRam::ModeAddress(a, b, page) + (line2 ? 1024u : 0u);
        _vram->Write(address, m0);
        _vram->Write(address + 1, m1);
        _vram->Write(address + 2, m2);
    }

    /// The frame drawn in one go (what a ScreenHQ-off frame end does)
    void Render() { _screen->RenderFrameBatch(); }

    uint32_t Pixel(uint32_t x, uint32_t y)
    {
        const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        return reinterpret_cast<const uint32_t*>(fb.memoryBuffer)[y * fb.width + x];
    }

    /// 16 pixels of a line from x
    std::vector<uint32_t> Strip(uint32_t x, uint32_t y)
    {
        std::vector<uint32_t> strip;
        for (uint32_t i = 0; i < 16; i++)
            strip.push_back(Pixel(x + i, y));
        return strip;
    }

    /// A port whose write the table routes to `code` (the decoder open)
    void WriteCode(uint8_t code, uint8_t value)
    {
        OpenDcp();
        SetCode(0x00C5, false, code);
        Out(0x00C5, value);
    }

    /// The CPU clock ratio (frame T-states are base T-states x the ratio)
    uint32_t Multiplier() const
    {
        const uint32_t m = _context->emulatorState.current_z80_frequency_multiplier;
        return m ? m : 1;
    }

    /// Top-left framebuffer pixel of square (a, b) without a HOLD shift
    static uint32_t SquareX(uint8_t a) { return 48u + 16u * a; }
    static uint32_t SquareY(uint8_t b) { return 16u + 8u * b; }
};

// The screen model: one mode, the 736x288 framebuffer, the 320-line raster
TEST_F(ScreenSprinter_Test, Geometry_736x288_320Lines)
{
    const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
    EXPECT_EQ(fb.width, 736);
    EXPECT_EQ(fb.height, 288);
    EXPECT_EQ(fb.videoMode, M_SPRINTER);
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 71680u);
    EXPECT_EQ(_screen->GetTstatesPerLine(), 224u);
    EXPECT_EQ(_screen->FrameLines(), 320);
    EXPECT_EQ(Screen::GetVideoModeName(M_SPRINTER), "Sprinter");

    // The beam: line 0 / T 0 is the first visible pixel; the picture starts at T 12 of line 16
    EXPECT_STREQ(_screen->DescribeBeam(0).verticalZone, "top_border");
    const BeamPosition paper = _screen->DescribeBeam(16 * 224 + 12);
    EXPECT_TRUE(paper.inPaper);
    EXPECT_EQ(paper.paperX, 0u);
    EXPECT_EQ(paper.paperY, 0u);
    EXPECT_STREQ(_screen->DescribeBeam(100 * 224 + 190).zone, "hblank");
    EXPECT_STREQ(_screen->DescribeBeam(300 * 224).verticalZone, "vblank");

    // Screenshots: "screen only" cuts the 640x256 picture at (48, 16); recordings double
    // the stored lines (14 MHz pixels, one stored line per TV line, as TS-Conf)
    const RasterDescriptor& rd = _screen->rasterDescriptors[M_SPRINTER];
    EXPECT_EQ(rd.screenWidth, 640);
    EXPECT_EQ(rd.screenHeight, 256);
    EXPECT_EQ(rd.screenOffsetLeft, 48);
    EXPECT_EQ(rd.screenOffsetTop, 16);
    EXPECT_TRUE(StoresHalfHeightLines(M_SPRINTER));

    // The debug mapper of the family
    EXPECT_EQ(FamilyOf(M_SPRINTER), VideoFamily::Sprinter);
    EXPECT_STREQ(videomap::VideoMapService::MapperFor(VideoFamily::Sprinter).Family(), "sprinter");
    const videomap::VideoLayout layout = videomap::VideoMapService(_context).Layout();
    ASSERT_TRUE(layout.mapped);
    ASSERT_EQ(layout.layers.size(), 1u);
    EXPECT_EQ(layout.layers[0].surface.width, 736);
    EXPECT_EQ(layout.layers[0].window.dotsPerT, 4);
}

// T-VID-1: a graphics 320 square, palette 2: one byte per 2 pixels, pen 2 x 256 + byte
TEST_F(ScreenSprinter_Test, Graphics320_Palette2_GoldenStrip)
{
    // Square (5, 3): Mode0 #A2 = palette 2, 320, column bits #2 << 6; Mode1 #19 = column + 8, row 24
    SetMode(5, 3, 0, 0xA2, 0x19, 0x00);
    for (uint32_t r = 0; r < 8; r++)
        for (uint32_t i = 0; i < 8; i++)
            _vram->Write((24 + r) * 1024 + 0x88 + i, static_cast<uint8_t>(0x10 * r + i + 1));
    Render();

    for (uint32_t r = 0; r < 8; r++)
    {
        std::vector<uint32_t> expected;
        for (uint32_t i = 0; i < 8; i++)
        {
            expected.push_back(PenColor(0x200 + 0x10 * r + i + 1));
            expected.push_back(PenColor(0x200 + 0x10 * r + i + 1));
        }
        EXPECT_EQ(Strip(SquareX(5), SquareY(3) + r), expected) << "row " << r;
    }
    // The square to the left is palette 0, source (0, 0)
    EXPECT_EQ(Pixel(SquareX(5) - 1, SquareY(3)), PenColor(_vram->Read(7)));
}

// T-VID-2: graphics 640: one nibble per pixel, the HIGH nibble first (MAME draw_tile;
// the PLD shows the high nibble in the first half of the 7 MHz period, VIDEO2.TDF BRVA)
TEST_F(ScreenSprinter_Test, Graphics640_HighNibbleFirst)
{
    SetMode(2, 2, 0, 0x81, 0x00, 0x00);  // palette 2, 640, column #040, source row 0
    _vram->Write(0x40, 0xAB);
    _vram->Write(0x41, 0x3C);
    Render();

    EXPECT_EQ(Pixel(SquareX(2), SquareY(2)), PenColor(0x20A)) << "first pixel: the high nibble";
    EXPECT_EQ(Pixel(SquareX(2) + 1, SquareY(2)), PenColor(0x20B));
    EXPECT_EQ(Pixel(SquareX(2) + 2, SquareY(2)), PenColor(0x203));
    EXPECT_EQ(Pixel(SquareX(2) + 3, SquareY(2)), PenColor(0x20C));
}

// T-VID-3: text 320 (a font bit = 2 pixels) and 640 (two characters, the right one from
// the Line2 bytes); FLASH (frame counter bit 4) moves to the flash palettes
TEST_F(ScreenSprinter_Test, Text320And640_Line2_Flash)
{
    // 320 text at (1, 1): Mode0 #30 (text 320, block 0), Mode1 #40 = font row, Mode2 #50 = attribute row
    SetMode(1, 1, 0, 0x30, 0x40, 0x50);
    _vram->Write((0x40 << 10) | 2, 0xA0);   // row 2 of the character: %1010 0000
    _vram->Write((0x50 << 10) | 0x18, 0x47);  // attribute
    // 640 text at (3, 1): left character font row #41, right (Line2) font row #42, attribute rows #51 / #52
    SetMode(3, 1, 0, 0x10, 0x41, 0x51);
    SetMode(3, 1, 0, 0x10, 0x42, 0x52, true);
    _vram->Write((0x41 << 10) | 2, 0x81);
    _vram->Write((0x51 << 10) | 0x18, 0x12);
    _vram->Write((0x42 << 10) | 2, 0xC0);
    _vram->Write((0x52 << 10) | 0x18, 0x34);
    Render();

    const uint32_t ink = PenColor(0x400 + 0x47 + 0x100);
    const uint32_t paper = PenColor(0x400 + 0x47);
    const std::vector<uint32_t> text320 = {ink, ink, paper, paper, ink, ink, paper, paper,
                                           paper, paper, paper, paper, paper, paper, paper, paper};
    EXPECT_EQ(Strip(SquareX(1), SquareY(1) + 2), text320);

    const uint32_t li = PenColor(0x400 + 0x12 + 0x100), lp = PenColor(0x400 + 0x12);
    const uint32_t ri = PenColor(0x400 + 0x34 + 0x100), rp = PenColor(0x400 + 0x34);
    const std::vector<uint32_t> text640 = {li, lp, lp, lp, lp, lp, lp, li, ri, ri, rp, rp, rp, rp, rp, rp};
    EXPECT_EQ(Strip(SquareX(3), SquareY(1) + 2), text640);

    // Frame counter bit 4: the flash paper / flash ink palettes (#600 / #700)
    _context->emulatorState.frame_counter = 16;
    Render();
    EXPECT_EQ(Pixel(SquareX(1), SquareY(1) + 2), PenColor(0x600 + 0x47 + 0x100));
    EXPECT_EQ(Pixel(SquareX(1) + 2, SquareY(1) + 2), PenColor(0x600 + 0x47));
}

// The Spectrum shadow address: #7FFD bit 3 selects the font / attribute block half
TEST_F(ScreenSprinter_Test, Text_7ffdBit3SelectsTheBlock)
{
    SetMode(0, 0, 0, 0x30, 0x40, 0x50);
    _vram->Write((0x40 << 10) | 0x20, 0xFF);        // font row 0 with 7FFD.3 = 1
    _vram->Write((0x50 << 10) | 0x20 | 0x18, 0x05);  // its attribute
    Render();
    EXPECT_EQ(Pixel(SquareX(0), SquareY(0)), PenColor(0x400)) << "7FFD.3 = 0: the other half (zeros)";

    Pld().pn = 0x08;
    Render();
    EXPECT_EQ(Pixel(SquareX(0), SquareY(0)), PenColor(0x400 + 0x05 + 0x100));
}

// T-VID-4: a border square shows text palette 0 entry border x 9; a blank square pen #400
TEST_F(ScreenSprinter_Test, BorderAndBlankSquares)
{
    SetMode(0, 0, 0, 0xF0, 0x00, 0x00);  // border
    SetMode(1, 0, 0, 0xFC, 0x00, 0x00);  // blank
    SetMode(2, 0, 0, 0xFD, 0x00, 0x00);  // blank + INT
    _screen->SetBorderColor(3);
    Render();
    EXPECT_EQ(Pixel(SquareX(0) + 7, SquareY(0) + 4), PenColor(0x400 + 27));
    EXPECT_EQ(Pixel(SquareX(1), SquareY(0)), PenColor(0x400)) << "blank: text paper colour 0 (MAME, PLD DCOL clear)";
    EXPECT_EQ(Pixel(SquareX(2) + 15, SquareY(0) + 7), PenColor(0x400));

    // A border write mid-frame: the beam up to it keeps the old colour. The top border
    // rows of the picture are square rows 38 and 39 (lines 0-7, 8-15)
    SetMode(0, 38, 0, 0xF0, 0x00, 0x00);
    SetMode(0, 39, 0, 0xF0, 0x00, 0x00);
    _screen->ResetPrevTstate();
    _z80->t = 8 * 224 * Multiplier();  // line 8, T 0
    _screen->UpdateScreen();
    _screen->SetBorderColor(5);
    _z80->t = 71680 * Multiplier();
    _screen->UpdateScreen();
    EXPECT_EQ(Pixel(48, 7), PenColor(0x400 + 27)) << "before the write";
    EXPECT_EQ(Pixel(48, 8), PenColor(0x400 + 45)) << "after the write";
}

// T-VID-5 (decision hardware-reference §4.5): video RAM holds R, G, B. The BIOS CGA
// "blue" (#A8,#00,#00 in its B,G,R table) reaches video RAM as #00,#00,#A8 through
// BIOS function #A4 and shows blue
TEST_F(ScreenSprinter_Test, PaletteByteOrder_RedGreenBlue)
{
    const uint32_t address = SprinterVideoRam::PenAddress(0x401);  // text paper entry 1
    EXPECT_EQ(address, 1u * 1024 + 0x3F0);
    _vram->Write(address, 0x00);
    _vram->Write(address + 1, 0x00);
    _vram->Write(address + 2, 0xA8);
    EXPECT_EQ(_vram->Pen(0x401), Rgba(0x00, 0x00, 0xA8));

    SetMode(0, 0, 0, 0x30, 0x40, 0x50);
    _vram->Write((0x50 << 10) | 0x18, 0x01);  // paper = text paper entry 1
    Render();
    const uint32_t pixel = Pixel(SquareX(0), SquareY(0));
    EXPECT_EQ(pixel & 0xFF, 0x00u) << "red";
    EXPECT_EQ((pixel >> 16) & 0xFF, 0xA8u) << "blue";

    // Direct storage writes need RefreshPalette
    _vram->Data()[address + 1] = 0x55;
    EXPECT_EQ(_vram->Pen(0x401), Rgba(0x00, 0x00, 0xA8));
    _vram->RefreshPalette();
    EXPECT_EQ(_vram->Pen(0x401), Rgba(0x00, 0x55, 0xA8));
}

// T-VID-6: RGMOD bit 0 switched mid-frame: the beam before it shows mode page 0, after it page 1
TEST_F(ScreenSprinter_Test, RgmodMidFrame_LinesAfterTheBeamUsePage1)
{
    for (uint8_t a = 0; a < 56; a++)
        for (uint8_t b = 0; b < 40; b++)
            SetMode(a, b, 1, 0x60, 0x00, 0x00);  // page 1: palette 1, graphics 320
    _vram->Write(0, 0x77);  // source (0, 0) byte 0: page 0 -> pen #077, page 1 -> pen #177

    _screen->ResetPrevTstate();
    _z80->t = 100 * 224 * Multiplier();  // line 100, T 0
    _screen->UpdateScreen();
    WriteCode(SprinterCode::RgMod, 0x01);
    _z80->t = 71680 * Multiplier();
    _screen->UpdateScreen();

    // Row 0 of a square (lines 16, 24, ...): line 96 is before the switch, line 104 after it
    EXPECT_EQ(Pixel(48, 96), PenColor(0x077));
    EXPECT_EQ(Pixel(48, 104), PenColor(0x177));
    EXPECT_EQ(Pld().rgMod, 0x01);
}

// T-VID-7: HOLD #77 = no shift; #00 moves the picture 14 pixels right and 7 lines down
TEST_F(ScreenSprinter_Test, Hold_00Vs77)
{
    SetMode(0, 0, 0, 0x60, 0x01, 0x00);  // palette 1, source column 8
    _vram->Write(0x08, 0x99);
    EXPECT_EQ(Pld().hold, 0x77) << "power-on: no shift (MAME m_hold = {0, 0})";
    Render();
    EXPECT_EQ(Pixel(48, 16), PenColor(0x199));
    EXPECT_NE(Pixel(47, 16), PenColor(0x199));

    WriteCode(SprinterCode::Hold, 0x00);
    Render();
    EXPECT_EQ(Pixel(48 + 14, 16 + 7), PenColor(0x199));
    EXPECT_NE(Pixel(48 + 13, 16 + 7), PenColor(0x199));
    EXPECT_NE(Pixel(48 + 14, 16 + 6), PenColor(0x199));
}

// T-VID-9: codes #2D / #2C: 312 / 320 lines from the next frame start (config.frame, the
// raster, the CPU frame), the INT list follows at once
TEST_F(ScreenSprinter_Test, FrameLength_312And320)
{
    SetMode(10, 30, 0, 0xFD, 0x00, 0x00);  // one blank + INT square: an INT
    WriteCode(SprinterCode::Frame312, 0x00);
    EXPECT_EQ(_decoder->GetIntSource().Positions(), SprinterIntSource::ComputePositions(*_vram, 0, 312));
    EXPECT_EQ(_context->config.frame, 71680u) << "the frame itself changes at the next frame start";

    _screen->InitFrame();
    _z80->BeginFrame();
    EXPECT_EQ(_context->config.frame, 69888u);
    EXPECT_EQ(_context->config.frame_duration_us, 19968u) << "the wall-clock pacing: 69 888 T / 3.5 MHz = 50.08 Hz";
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);
    EXPECT_EQ(_screen->FrameLines(), 312);
    _z80->t = 69887 * Multiplier();
    EXPECT_FALSE(_z80->IsFrameComplete());
    _z80->t = 69888 * Multiplier();
    EXPECT_TRUE(_z80->IsFrameComplete());

    // 312 lines: the vertical square position wraps at 312 (visible line 0 = square row 37)
    const SprinterVideoInputs in = _screen->CurrentInputs();
    EXPECT_EQ(SprinterVideoRenderer::B8(in, 0), 296u);

    WriteCode(SprinterCode::Frame320, 0x00);
    _screen->InitFrame();
    _z80->BeginFrame();
    EXPECT_EQ(_context->config.frame, 71680u);
    EXPECT_EQ(_context->config.frame_duration_us, 20480u) << "71 680 T / 3.5 MHz = 48.83 Hz (MAME: 896 x 320 at 14 MHz)";
    EXPECT_EQ(_screen->FrameLines(), 320);
    EXPECT_EQ(SprinterVideoRenderer::B8(_screen->CurrentInputs(), 0), 304u);
    _z80->t = 0;
}

// A video RAM write mid-frame: the beam up to it is drawn with the old byte (MAME update_now)
TEST_F(ScreenSprinter_Test, VramWriteMidFrame_CatchesUpFirst)
{
    _vram->Write(0, 0x11);
    _screen->ResetPrevTstate();
    _z80->t = 100 * 224 * Multiplier();
    _vram->Write(0, 0x22);  // the before-change listener draws lines 0-99 with #11 first
    _z80->t = 71680 * Multiplier();
    _screen->UpdateScreen();
    EXPECT_EQ(Pixel(48, 96), PenColor(0x011));
    EXPECT_EQ(Pixel(48, 104), PenColor(0x022));
}

// The debug mapper names the bytes the renderer used
TEST_F(ScreenSprinter_Test, VideoMapper_SourcesOfAGraphicsPixel)
{
    SetMode(5, 3, 0, 0xA2, 0x19, 0x00);
    _vram->Write(24 * 1024 + 0x88, 0x42);
    std::vector<videomap::SourceRef> sources;
    const uint32_t pen = SprinterVideoMapper::Collect(_screen->CurrentInputs(), SquareX(5), SquareY(3), sources);
    EXPECT_EQ(pen, 0x242u);
    ASSERT_EQ(sources.size(), 5u);
    EXPECT_EQ(sources[0].offset, SprinterVideoRam::ModeAddress(5, 3, 0));
    EXPECT_EQ(sources[0].role, videomap::SourceRole::ModeDescriptor);
    EXPECT_EQ(sources[3].offset, 24u * 1024 + 0x88);
    EXPECT_EQ(sources[3].role, videomap::SourceRole::PixelBits);
    EXPECT_EQ(sources[4].offset, SprinterVideoRam::PenAddress(0x242));
    EXPECT_EQ(sources[4].role, videomap::SourceRole::PaletteEntry);

    // The pen through the debug path equals the renderer's
    Render();
    EXPECT_EQ(Pixel(SquareX(5), SquareY(3)), PenColor(pen));
}

namespace
{
/// A module that brings its own picture (hook 3): every pixel pen #123
class SolidRenderer : public SprinterVideoRenderer
{
public:
    void DrawSpan(const SprinterVideoInputs& in, uint32_t, uint32_t x0, uint32_t x1, uint32_t* out) const override
    {
        for (uint32_t x = x0; x < x1; x++)
            *out++ = in.palette[0x123];
    }
};

class RendererModule : public SprinterPldConfiguration
{
public:
    RendererModule() { _descriptor.name = "RendererStub"; }
    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }
    const SprinterVideoRenderer* VideoRenderer() const override { return &_renderer; }

private:
    SprinterPldModuleDescriptor _descriptor;
    SolidRenderer _renderer;
};
}  // namespace

// T-PLDM-2 (video part): the active configuration module's renderer draws; Standard's otherwise
TEST_F(ScreenSprinter_Test, ConfigurationModuleRendererOverride)
{
    const size_t index = _decoder->GetRegistry().Register(std::make_unique<RendererModule>());
    Pld().configModule = static_cast<uint8_t>(index);
    Render();
    EXPECT_EQ(Pixel(0, 0), PenColor(0x123));
    EXPECT_EQ(Pixel(735, 287), PenColor(0x123));

    Pld().configModule = 0;
    Render();
    EXPECT_NE(Pixel(0, 0), PenColor(0x123));
}

// The screen digest sees native screens (automation audit G7): the default and the active mode hash the
// video RAM surface, not RAM pages 5 / 7; explicit pages still hash pages
TEST_F(ScreenSprinter_Test, DigestHashesTheVideoRam)
{
    ScreenDigestQuery query;
    ScreenDigestResult first = ScreenDigestCompute::Compute(_context, query);
    ASSERT_TRUE(first.ok) << first.error;
    EXPECT_TRUE(first.deviceSurface);
    EXPECT_EQ(first.surface.name, "vram");
    EXPECT_EQ(first.surface.bytes, SprinterVideoRam::kSize);

    _vram->Write(0x12345, 0x77);  // a byte of a picture: RAM pages 5 / 7 do not change
    ScreenDigestResult second = ScreenDigestCompute::Compute(_context, query);
    EXPECT_TRUE(second.changed);
    EXPECT_NE(second.surface.digest, first.surface.digest);

    query.active = true;
    EXPECT_TRUE(ScreenDigestCompute::Compute(_context, query).deviceSurface);
    query.banks = {5};
    const ScreenDigestResult pages = ScreenDigestCompute::Compute(_context, query);
    EXPECT_FALSE(pages.deviceSurface);
    ASSERT_EQ(pages.banks.size(), 1u);

    const StateNode report = DeviceState::ScreenDigestReport(_context, ScreenDigestQuery());
    const StateNode* surface = report.find("active_surface");
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->find("memory")->s, "vram");
}

/// region <Spectrum mode: the beam against the INT (research-zx-mode §7.1)>

namespace
{
/// Attribute j of the reference cells: paper j, ink 0 (the pixel bytes are 0: all paper)
constexpr uint8_t RefAttr(int j) { return static_cast<uint8_t>(j << 3); }
constexpr uint8_t kStartAttr = 0x78;  // bright white paper: none of the references

/// A Spectrum multicolor race, the same program on every machine: LD (HL),A instructions (7 T, the write
/// cycle in T 5-7) that start kRaceT after the INT + 224 x k and write attribute RefAttr(k) to #5800, the
/// cell the beam fetches first. Pentagon timed: the Pentagon reads that cell 17 988 T after its INT, so the
/// write comes 8 T before the fetch of line k
constexpr int32_t kRaceT = 17980;
constexpr int32_t kLdHlA = 7;
}  // namespace

// The launcher's Spectrum screen (squares (4, 4) on, INT squares at (40, 33)-(41, 33), FN_SYNC Pentagon):
// the INT edge is the PLD's - CT5 rising 2 T into the first square without the pattern, 10 T before MAME's
// beam position (SprinterIntSource) - so the video logic reads the first Spectrum square 17 990 T after the
// INT: the Pentagon's 17 988 within the 2 T the PLD's square period gives, where MAME's place gave 17 980
TEST_F(ScreenSprinter_Test, SpectrumScreen_IntToFirstPixel_IsThePentagons)
{
    SprinterModeTable::WriteSpectrumScreen(*_vram, 0);
    SetMode(40, 33, 0, 0xFD, 0x00, 0x00);
    SetMode(41, 33, 0, 0xFD, 0x00, 0x00);

    const std::vector<uint32_t>& positions = _decoder->GetIntSource().Positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0], 287u * 224 + 192 - SprinterIntSource::kIntBeforeMameT) << "line 287, T 182 (MAME: T 192)";

    // The first Spectrum pixel: square (4, 4), framebuffer (48 + 64, 16 + 32) = line 48, T 28
    const uint32_t firstPixel = 48u * 224 + (48u + 64u) / 4u;
    EXPECT_EQ(SprinterVideoRenderer::A16(_screen->CurrentInputs(), 112), 64u);
    EXPECT_EQ(SprinterVideoRenderer::B8(_screen->CurrentInputs(), 48), 32u);
    const uint32_t intToPixel = 71680u - positions[0] + firstPixel;
    EXPECT_EQ(intToPixel, 17990u);
    EXPECT_LE(intToPixel - 17988u, 2u) << "the Pentagon reference (INTTiming_Test.INTToFirstPixel_MatchesReferencePerModel)";
}

// The owner's report (scroller.trd, atarin.trd in P128 mode): attributes behind the pixels. One Pentagon-timed
// multicolor race on the PENTAGON model and on the Sprinter's Spectrum screen: every line of the first cell
// shows the attribute written for it on both machines - the same paper pixels. With MAME's INT place the
// Sprinter read the cell 10 T earlier, before each write, and showed the previous line's attribute.
// Builds a PENTAGON emulator beside the fixture (~10 ms): the reference is the real Pentagon renderer
TEST_F(ScreenSprinter_Test, SpectrumScreen_MulticolorRace_SameAttributesAsPentagon)
{
    // Which reference cell (columns 1-8: RefAttr(0..7)) line k of the first cell looks like; -1: none
    auto shown = [](auto pixel, uint32_t x0, uint32_t cellWidth, uint32_t y) {
        for (int j = 0; j < 8; j++)
            if (pixel(x0, y) == pixel(x0 + cellWidth * static_cast<uint32_t>(j + 1), y))
                return j;
        return -1;
    };

    // Sprinter: the launcher's table, the shadow on (ALL_MODE bit 0 = 0), the cells' bytes written as a program would
    SprinterModeTable::WriteSpectrumScreen(*_vram, 0);
    SetMode(40, 33, 0, 0xFD, 0x00, 0x00);
    SetMode(41, 33, 0, 0xFD, 0x00, 0x00);
    OpenDcp();
    Pld().allMode = 0;
    Pld().portY = 0;
    for (int line = 0; line < 8; line++)
        for (int c = 0; c < 9; c++)
            Poke(static_cast<uint16_t>(0x4000 + line * 256 + c), 0x00);
    for (int j = 0; j < 8; j++)
        Poke(static_cast<uint16_t>(0x5801 + j), RefAttr(j));
    Poke(0x5800, kStartAttr);

    const uint32_t m = Multiplier();
    const int32_t sprinterInt = static_cast<int32_t>(_decoder->GetIntSource().Positions().at(0)) - 71680;  // the frame before
    _screen->ResetPrevTstate();
    for (int k = 0; k < 8; k++)
    {
        const int32_t start = sprinterInt + kRaceT + 224 * k;
        _z80->t = static_cast<uint32_t>(start) * m;
        _screen->UpdateScreen();  // the instruction before
        _z80->t = static_cast<uint32_t>(start + kLdHlA) * m;  // the write cycle's end
        Poke(0x5800, RefAttr(k));
        _screen->UpdateScreen();
    }
    _z80->t = 71680 * m;
    _screen->UpdateScreen();

    // Pentagon: INT at intstart + 1 of the frame before, the same instructions, the renderer after each
    Emulator* pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(pentagon, nullptr);
    EmulatorContext* pc = pentagon->GetContext();
    pc->pFeatureManager->setFeature(Features::kScreenHQ, true);  // the beam renderer (the helper turns it off for speed)
    Z80* pz = pc->pCore->GetZ80();
    Memory* pm = pc->pMemory;
    Screen* ps = pc->pScreen;
    auto pentagonWrite = [&](uint16_t addr, uint8_t value) { (pm->*(pz->MemIf->MemoryWrite))(addr, value); };
    for (int j = 0; j < 8; j++)
        pentagonWrite(static_cast<uint16_t>(0x5801 + j), RefAttr(j));
    pentagonWrite(0x5800, kStartAttr);
    const int32_t pentagonInt = static_cast<int32_t>(pc->config.intstart + 1) - static_cast<int32_t>(pc->config.frame);
    EXPECT_EQ(ps->GetPaperStartTstate() - pentagonInt, 17988) << "the Pentagon reference";
    ps->ResetPrevTstate();
    for (int k = 0; k < 8; k++)
    {
        const int32_t start = pentagonInt + kRaceT + 224 * k;
        pz->t = static_cast<uint32_t>(start);
        ps->UpdateScreen();
        pz->t = static_cast<uint32_t>(start + kLdHlA);
        pentagonWrite(0x5800, RefAttr(k));
        ps->UpdateScreen();
    }
    pz->t = pc->config.frame - 1;
    ps->UpdateScreen();
    const FramebufferDescriptor& pfb = ps->GetFramebufferDescriptor();
    ASSERT_EQ(pfb.width, 352);
    auto pentagonPixel = [&](uint32_t x, uint32_t y) { return reinterpret_cast<const uint32_t*>(pfb.memoryBuffer)[y * pfb.width + x]; };
    auto sprinterPixel = [&](uint32_t x, uint32_t y) { return Pixel(x, y); };

    for (uint32_t k = 0; k < 8; k++)
    {
        SCOPED_TRACE("line " + std::to_string(k));
        EXPECT_EQ(shown(pentagonPixel, 48, 8, 48 + k), static_cast<int>(k)) << "PENTAGON";
        EXPECT_EQ(shown(sprinterPixel, 112, 16, 48 + k), static_cast<int>(k)) << "Sprinter P128";
        for (uint32_t x = 1; x < 16; x++)
            EXPECT_EQ(Pixel(112 + x, 48 + k), Pixel(112, 48 + k)) << "the whole cell, x " << x;
    }
    EmulatorTestHelper::CleanupEmulator(pentagon);
}

// A byte that lands inside a Spectrum square: its attribute changes from there on (the PLD reads it every half
// T), its pixels only from the next square (the font byte is latched at the square's start, VIDEO2.TDF LD_PIC)
TEST_F(ScreenSprinter_Test, SpectrumScreen_WriteInsideASquare_AttributeAtOncePixelsNextSquare)
{
    SprinterModeTable::WriteSpectrumScreen(*_vram, 0);
    OpenDcp();
    Pld().allMode = 0;
    Pld().portY = 0;
    Poke(0x4000, 0x00);  // line 0, cell 0: paper
    Poke(0x5800, RefAttr(1));

    // The cell is read at line 48, T 28-31: writes land 1 T before the write cycle's end, at T 30 (2 of its 4 T drawn)
    const uint32_t m = Multiplier();
    const uint32_t cell = 48u * 224 + 28;
    _screen->ResetPrevTstate();
    _z80->t = (cell + 2 + ScreenSprinter::kWriteLandsBeforeEndT) * m;
    Poke(0x4000, 0xFF);       // all ink - latched too late for this square
    Poke(0x5800, RefAttr(2)); // paper 2 from here
    _z80->t = 71680 * m;
    _screen->UpdateScreen();

    const uint32_t paper1 = PenColor(SprinterVideoRenderer::kPenText + RefAttr(1));
    const uint32_t paper2 = PenColor(SprinterVideoRenderer::kPenText + RefAttr(2));
    for (uint32_t x = 0; x < 8; x++)
        EXPECT_EQ(Pixel(112 + x, 48), paper1) << "before the write, x " << x;
    for (uint32_t x = 8; x < 16; x++)
        EXPECT_EQ(Pixel(112 + x, 48), paper2) << "the new attribute, the latched pixels (paper), x " << x;
}

/// endregion </Spectrum mode>
