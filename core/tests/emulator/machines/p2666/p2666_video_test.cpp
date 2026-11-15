// Pentagon 2.666 LE video (test-plan.md T-V): mode decode, the board's visible window, the palette in every mode,
// the 16 colour border, the text font. Sources: zx_main.vhd / PaE.vhd at e808402, hardware-reference.md §1.3, §6.

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <cstring>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "emulator/video/p2666/p2666font.h"
#include "emulator/video/p2666/p2666geometry.h"
#include "emulator/video/screen.h"
#include "pch.h"
#include "stdafx.h"

class P2666Video_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoderP2666* _decoder = nullptr;
    Screen* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoderP2666*>(_context->pPortDecoder);
        _screen = _context->pScreen;
        ASSERT_NE(_decoder, nullptr);
        ASSERT_NE(_screen, nullptr);
        // the border is drawn by the per-T renderer only (the batch one draws the paper)
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    EmulatorState& State() { return _context->emulatorState; }
    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0x8000); }

    /// gfx_mode = xx77[2:0] & 00 & EFF7.0
    VideoModeEnum SelectMode(uint8_t xx77Mode, bool z0)
    {
        State().p2666.pxx77 = static_cast<uint8_t>(0xA8 | (xx77Mode & 7));
        State().pEFF7 = z0 ? 0x01 : 0x00;
        _screen->InitRaster();
        return _screen->GetVideoMode();
    }

    /// The whole frame through the per-T renderer, with the registers as they are now
    void RenderFrame()
    {
        _screen->ResetPrevTstate();
        _context->pCore->GetZ80()->t = State().BaseToCpuT(_context->config.frame - 1);
        _screen->UpdateScreen();
    }

    void DosOff() { State().flags &= ~CF_TRDOS; }

    uint32_t Pixel(int x, int y)
    {
        const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        EXPECT_TRUE(x >= 0 && x < fb.width && y >= 0 && y < fb.height) << x << "," << y;
        return reinterpret_cast<const uint32_t*>(fb.memoryBuffer)[y * fb.width + x];
    }

    uint32_t Palette(int index) const { return _context->emulatorState.atm.palette[index]; }
};

/// T-V1: every (xx77[2:0], EFF7.0) pair; the ones with no case in zx_main show the border colour over the frame (D-8)
TEST_F(P2666Video_Test, ModeDecodeMatchesTheVhdl)
{
    struct Row
    {
        uint8_t xx77;
        bool z0;
        VideoModeEnum mode;
        bool defined;
    };
    const Row rows[] = {
        {3, false, M_PENTAGON128K, true}, {3, true, M_P16, true},  {0, false, M_ATM16, true},
        {2, false, M_ATMHR, true},        {6, false, M_ATMTX, true},
    };
    for (const Row& r : rows)
        EXPECT_EQ(SelectMode(r.xx77, r.z0), r.mode) << "xx77 " << int(r.xx77) << " z0 " << r.z0;

    // Undefined: a lit paper is not drawn, the frame is the border colour
    uint8_t* screen = _context->pMemory->RAMPageAddress(5);
    std::memset(screen, 0xFF, 6144);
    std::memset(screen + 6144, 0x07, 768);
    Out(0x00FE, 0x02);
    for (uint8_t xx77 : {1, 4, 5, 7})
    {
        for (bool z0 : {false, true})
        {
            SelectMode(xx77, z0);
            RenderFrame();
            EXPECT_EQ(Pixel(P2666Geometry::kBorderLeft + 100, P2666Geometry::kBorderTop + 90), Palette(2))
                << "xx77 " << int(xx77) << " z0 " << z0;
        }
    }
    for (uint8_t xx77 : {0, 2, 6})
    {
        SelectMode(xx77, true);
        RenderFrame();
        EXPECT_EQ(Pixel(P2666Geometry::kBorderLeft + 100, P2666Geometry::kBorderTop + 90), Palette(2)) << "ATM " << int(xx77) << " + z0";
    }

    // Back to ZX: the paper again
    SelectMode(3, false);
    RenderFrame();
    EXPECT_EQ(Pixel(P2666Geometry::kBorderLeft + 100, P2666Geometry::kBorderTop + 90), Palette(7));
}

/// T-V6: the framebuffer is the board's visible window, the ZX paper and the ATM window inside it
TEST_F(P2666Video_Test, FramebufferIsTheBoardsWindow)
{
    using namespace P2666Geometry;
    uint8_t* page5 = _context->pMemory->RAMPageAddress(5);
    std::memset(page5, 0, 0x4000);
    page5[0] = 0x80;     // ZX: the paper's first pixel ink
    page5[0x1800] = 0x07;
    Out(0x00FE, 0x01);   // blue border

    SelectMode(3, false);
    RenderFrame();
    {
        const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        EXPECT_EQ(fb.width, 388);
        EXPECT_EQ(fb.height, 296);
    }
    EXPECT_EQ(Pixel(kBorderLeft - 1, kBorderTop), Palette(1)) << "left border";
    EXPECT_EQ(Pixel(kBorderLeft, kBorderTop), Palette(7)) << "the paper's first pixel";
    EXPECT_EQ(Pixel(kBorderLeft + 1, kBorderTop), Palette(0));
    EXPECT_EQ(Pixel(kBorderLeft, kBorderTop - 1), Palette(1)) << "top border";
    EXPECT_EQ(Pixel(kBorderLeft + 256, kBorderTop), Palette(1)) << "right border";
    EXPECT_EQ(Pixel(387, 295), Palette(1));

    // ATM 320 x 200: 28 px left border (the window 16 T left of the ZX paper), 40 right, same first line
    std::memset(page5, 0, 0x4000);
    std::memset(_context->pMemory->RAMPageAddress(1), 0x47, 0x4000);  // page 1 plane: pixel pairs of colour 15 / 8
    SelectMode(0, false);
    RenderFrame();
    {
        const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        EXPECT_EQ(fb.width, 388);
        EXPECT_EQ(fb.height, 296);
    }
    EXPECT_EQ(Pixel(27, kBorderTop), Palette(1)) << "ATM left border";
    EXPECT_EQ(Pixel(28, kBorderTop), Palette(15)) << "the ATM window's first pixel (page 1 plane, left nibble)";
    EXPECT_EQ(Pixel(28, kBorderTop - 1), Palette(1)) << "ATM top border";
    EXPECT_EQ(Pixel(28 + 320, kBorderTop), Palette(1)) << "ATM right border";
    EXPECT_EQ(Pixel(28, kBorderTop + 200), Palette(1)) << "ATM bottom border";

    // ATM 640 x 200: the same window at 4 px per T
    SelectMode(2, false);
    RenderFrame();
    {
        const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        EXPECT_EQ(fb.width, 776);
        EXPECT_EQ(fb.height, 296);
    }
    EXPECT_EQ(Pixel(55, kBorderTop), Palette(1));
    EXPECT_EQ(Pixel(56 + 640, kBorderTop), Palette(1));
}

/// T-V4: the default palette is pollitra.hex decoded by the DAC wiring (components 20 / 30 of 31)
TEST_F(P2666Video_Test, DefaultPaletteIsPollitra)
{
    const auto& def = PortDecoderP2666::DefaultPalette();
    for (int i = 0; i < 16; i++)
        EXPECT_EQ(Palette(i), PortDecoderP2666::PaletteWordToAbgr(def[i])) << i;
    const uint32_t c20 = (20u * 255u + 15u) / 31u;
    const uint32_t c30 = (30u * 255u + 15u) / 31u;
    EXPECT_EQ(Palette(0), 0xFF000000u);
    EXPECT_EQ(Palette(8), 0xFF000000u) << "bright black is black";
    EXPECT_EQ(Palette(7), 0xFF000000u | c20 << 16 | c20 << 8 | c20);
    EXPECT_EQ(Palette(15), 0xFF000000u | c30 << 16 | c30 << 8 | c30);
    EXPECT_EQ(Palette(2), 0xFF000000u | c20) << "red";
    EXPECT_EQ(Palette(1), 0xFF000000u | c20 << 16) << "blue";
}

/// T-V4 / T-V5: a palette write goes to the entry of the current border colour (bright bit = NOT A3), the ZX mode
/// and the border draw through it; refused in 128 mode and in DOS with pen2
TEST_F(P2666Video_Test, PaletteWritesReachEveryModeAndTheBorder)
{
    using namespace P2666Geometry;
    DosOff();
    uint8_t* page5 = _context->pMemory->RAMPageAddress(5);
    std::memset(page5, 0, 0x4000);
    page5[0] = 0xFF;
    page5[0x1800] = 0x02;  // ink red, paper black

    Out(0x00FE, 0x02);       // border red, A3 = 1: entry 2
    Out(0x12FF, 0x34);       // A15..8 : D7..0 = #1234
    EXPECT_EQ(State().p2666.palette[2], 0x1234);
    EXPECT_EQ(Palette(2), PortDecoderP2666::PaletteWordToAbgr(0x1234));

    SelectMode(3, false);
    RenderFrame();
    EXPECT_EQ(Pixel(kBorderLeft, kBorderTop), Palette(2)) << "ink through the palette";
    EXPECT_EQ(Pixel(0, 0), Palette(2)) << "border through the palette";

    // A3 = 0: the bright border, entry 10
    Out(0x00F6, 0x02);
    EXPECT_EQ(State().p2666.border3, 1);
    Out(0x56FF, 0x78);
    EXPECT_EQ(State().p2666.palette[10], 0x5678);
    RenderFrame();
    EXPECT_EQ(Pixel(0, 0), Palette(10)) << "the bright border";
    EXPECT_EQ(Pixel(kBorderLeft, kBorderTop), Palette(2)) << "the non-bright ink keeps entry 2";

    // 128 mode (EFF7 bit 2): no palette writes
    State().pEFF7 = PortDecoderP2666::kEff7Lock128;
    Out(0x00FF, 0x00);
    EXPECT_EQ(State().p2666.palette[10], 0x5678);
    State().pEFF7 = 0;

    // DOS with pen2 (the reset value): no palette writes
    State().flags |= CF_TRDOS;
    Out(0x00FF, 0x00);
    EXPECT_EQ(State().p2666.palette[10], 0x5678);
}

/// T-V1 (screen page): #7FFD.3 shows screen 1 (RAM page 7) in the ZX mode (hardware-reference.md §6 mode table);
/// NedoOS games (UFO 2) draw in page 7 and flip to it
TEST_F(P2666Video_Test, Port7FFDBit3ShowsPage7)
{
    using namespace P2666Geometry;
    DosOff();
    uint8_t* page5 = _context->pMemory->RAMPageAddress(5);
    uint8_t* page7 = _context->pMemory->RAMPageAddress(7);
    std::memset(page5, 0, 0x4000);
    std::memset(page7, 0, 0x4000);
    page7[0] = 0xFF;
    page7[0x1800] = 0x02;  // ink red, paper black
    SelectMode(3, false);

    Out(0x7FFD, 0x08);
    RenderFrame();
    EXPECT_EQ(Pixel(kBorderLeft, kBorderTop), Palette(2)) << "page 7 on the screen";

    Out(0x7FFD, 0x00);
    RenderFrame();
    EXPECT_EQ(Pixel(kBorderLeft, kBorderTop), Palette(0)) << "back to page 5";
}

/// T-V2 (text): the font is the board's otmfnt ROM, not the ATM Turbo 2+ one; kept across a CPU reset
TEST_F(P2666Video_Test, TextModeUsesTheBoardFont)
{
    EXPECT_EQ(std::memcmp(State().atm.fontRam, P2666Font::kFont, sizeof(P2666Font::kFont)), 0);
    // 'A' row 1 (otmfnt.hex): ..####..
    EXPECT_EQ(P2666Font::kFont[0x41 * 8 + 1], 0x3C);
    _emulator->Reset();
    EXPECT_EQ(std::memcmp(State().atm.fontRam, P2666Font::kFont, sizeof(P2666Font::kFont)), 0);
}

/// T-V3 (border): an FE write shows 3.25 T after the latch on the board (border_buf, POC 051 video.sh); the renderer
/// keeps the old colour through the write T + 2, the new one from T + 3 (2 T + 6 pixels; the board 2 T + 6.5)
TEST_F(P2666Video_Test, BorderWriteShowsThreeTLater)
{
    using namespace P2666Geometry;
    Z80* z80 = _context->pCore->GetZ80();
    SelectMode(3, false);
    Out(0x00FE, 0x00);

    const uint32_t line = 100;
    const uint32_t t = 10;  // left border of the paper line 20
    _screen->ResetPrevTstate();
    z80->t = State().BaseToCpuT(line * kTStatesPerLine + t);
    _screen->UpdateScreen();
    Out(0x00FE, 0x02);
    z80->t = State().BaseToCpuT(line * kTStatesPerLine + t + 40);
    _screen->UpdateScreen();

    const int row = static_cast<int>(line) - (kVSyncLines + kVBlankLines);
    for (int x = 0; x < 2 * static_cast<int>(t + 3); x++)
        EXPECT_EQ(Pixel(x, row), Palette(0)) << "x " << x << ": the old colour through T + 2";
    for (int x = 2 * static_cast<int>(t + 3); x < 2 * static_cast<int>(t + 3) + 8; x++)
        EXPECT_EQ(Pixel(x, row), Palette(2)) << "x " << x << ": the new colour from T + 3";
}
