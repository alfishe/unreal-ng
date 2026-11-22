// The Next's border colour written by the CPU (port #FE) inside a line: ScreenNext splits the line where the new colour reaches the
// output. The rule (ScreenNext::BorderEffectHalfPixel) is the one POC 028 scenario 54 measured on the simulated RTL: port_fe_border
// takes the write on the CPU clock's falling edge, the output shows it 17 clocks later, from the next 8-pixel cell in the 48K / 128K
// timings and at once in the Pentagon's.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/next/nextpicturewindow.h"
#include "emulator/video/next/nextvideorenderer.h"
#include "emulator/video/next/screennext.h"

// POC 028 scenario 54 (tools/poc/028-next-video-pipeline-alignment/measurements-2026-11-20-border.md): writes at the CPU's T-state
// starts, in 28 MHz clocks from the paper's first T-state, and the first image sub-pixel in the new colour
TEST(NextBorderEffect_Test, MatchesTheSimulatedRtl)
{
    // 48K / 128K timing: the cell grid (32 clocks = 16 sub-pixels); the right border starts at sub-pixel 576
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1120, false), 608);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1128, false), 608);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1136, false), 624);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1144, false), 624);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1152, false), 624);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1160, false), 624);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1168, false), 640);
    // the Pentagon timing: a T-state (8 clocks = 4 sub-pixels) a step
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1120, true), 604);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1128, true), 608);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(1136, true), 612);
    // a write before the line's paper: the left border, cell-aligned from the grid's edge (negative: the outer border left of it)
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(-128, false), -16);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(-96, false), 0);
    EXPECT_EQ(ScreenNext::BorderEffectHalfPixel(-64, false), 16);
}

class NextBorderMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    ScreenNext* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _screen = dynamic_cast<ScreenNext*>(_context->pScreen);
        ASSERT_NE(_screen, nullptr);
        // the test helper switches ScreenHQ off (the frame drawn at its end); a split needs the beam-following drawing
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void At(uint32_t t) { _context->pCore->GetZ80()->t = t; }
    void Border(uint8_t colour) { _ports->DecodePortOut(0x00FE, colour, 0); }
    /// The T-state of grid line `line`'s first paper pixel
    uint32_t PaperT(unsigned line) const
    {
        const RasterState& r = _screen->GetRasterState();
        return _screen->GridFirstT() + line * r.tstatesPerLine + r.screenLineAreaStart;
    }
    /// Grid line `line`, image sub-pixel `x` (0..639) of the drawn frame
    uint32_t Pixel(unsigned line, int x) const
    {
        const EmulatorState& state = _context->emulatorState;
        const NextPictureWindow::FrameOrigin origin = NextPictureWindow::Origin(state.ula_timing_class, state.ula_sixty_hz != 0);
        const FramebufferDescriptor& fb = _screen->GetNativeFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        const int row = (origin.top + static_cast<int>(line)) * NextPictureWindow::kScale;
        return pixels[static_cast<size_t>(row) * fb.width + static_cast<size_t>(origin.left * NextPictureWindow::kScale + x)];
    }
};

TEST_F(NextBorderMachine_Test, AWriteInsideTheRightBorderSplitsTheLineAtItsCell)
{
    constexpr unsigned kLine = 100;
    _screen->InitFrame();
    At(PaperT(0) - 200);
    Border(1);
    // 136 T-states after the paper's start: 8 into the right border; the new colour from the cell the rule gives
    const uint32_t writeT = PaperT(kLine) + 136;
    const int64_t split = ScreenNext::BorderEffectHalfPixel(136 * 8, _context->emulatorState.ula_timing_class == 4);
    ASSERT_GT(split, 576);
    ASSERT_LT(split, 640);
    At(writeT);
    Border(2);
    At(_screen->LineEndT(NextVideoRenderer::kHeight - 1) + 1);
    _screen->RenderOnlyMainScreen();

    const uint32_t blue = Pixel(kLine - 1, 600);
    const uint32_t red = Pixel(kLine + 1, 4);
    ASSERT_NE(blue, red);
    EXPECT_EQ(Pixel(kLine, 4), blue) << "the line's left border is before the write";
    EXPECT_EQ(Pixel(kLine, 580), blue);
    EXPECT_EQ(Pixel(kLine, static_cast<int>(split) - 1), blue) << "the last sub-pixel before the change";
    EXPECT_EQ(Pixel(kLine, static_cast<int>(split)), red) << "the change, at sub-pixel " << split;
    EXPECT_EQ(Pixel(kLine, 639), red);
    EXPECT_EQ(Pixel(kLine + 1, 600), red);
}

TEST_F(NextBorderMachine_Test, AWriteJustBeforeALinesPaperSplitsItsLeftBorder)
{
    constexpr unsigned kLine = 120;
    _screen->InitFrame();
    At(PaperT(0) - 200);
    Border(5);
    // 8 T-states before the next line's paper: its left border (32 pixels = 16 T-states of border, then the paper)
    const uint32_t writeT = PaperT(kLine + 1) - 8;
    At(writeT);
    Border(6);
    At(_screen->LineEndT(NextVideoRenderer::kHeight - 1) + 1);
    _screen->RenderOnlyMainScreen();
    const int64_t split = ScreenNext::BorderEffectHalfPixel(-8 * 8, _context->emulatorState.ula_timing_class == 4);
    ASSERT_GT(split, 0);
    ASSERT_LT(split, 64);
    const uint32_t before = Pixel(kLine, 600), after = Pixel(kLine + 2, 4);
    ASSERT_NE(before, after);
    EXPECT_EQ(Pixel(kLine, 639), before);
    EXPECT_EQ(Pixel(kLine + 1, static_cast<int>(split) - 1), before);
    EXPECT_EQ(Pixel(kLine + 1, static_cast<int>(split)), after);
}

TEST_F(NextBorderMachine_Test, ManyWritesInOneLineDrawTheirRunsInOnePass)
{
    // a top-border line (all border): a beeper-like loop's writes every 24 T-states, each colour from its own cell on
    constexpr unsigned kLine = 10;
    constexpr unsigned kWrites = 6;
    _screen->InitFrame();
    At(PaperT(0) - 200);
    Border(0);
    const bool pentagon = _context->emulatorState.ula_timing_class == 4;
    int64_t splits[kWrites];
    for (unsigned k = 0; k < kWrites; k++)
    {
        const uint32_t offset = 24 * k;
        splits[k] = ScreenNext::BorderEffectHalfPixel(static_cast<int64_t>(offset) * 8, pentagon);
        At(PaperT(kLine) + offset);
        Border(static_cast<uint8_t>(k + 1));
    }
    At(_screen->LineEndT(NextVideoRenderer::kHeight - 1) + 1);
    _screen->RenderOnlyMainScreen();

    // the line after holds the last colour everywhere, the line before the first
    EXPECT_EQ(Pixel(kLine + 1, 320), Pixel(kLine, 639));
    EXPECT_NE(Pixel(kLine - 1, 320), Pixel(kLine + 1, 320));
    EXPECT_EQ(Pixel(kLine, static_cast<int>(splits[0]) - 1), Pixel(kLine - 1, 320)) << "before the first write";
    for (unsigned k = 0; k + 1 < kWrites; k++)
    {
        const uint32_t run = Pixel(kLine, static_cast<int>(splits[k]));
        EXPECT_EQ(Pixel(kLine, static_cast<int>(splits[k + 1]) - 1), run) << "write " << k << " holds up to the next one";
        EXPECT_NE(Pixel(kLine, static_cast<int>(splits[k + 1])), run) << "write " << k + 1 << " changes the colour at " << splits[k + 1];
    }
}
