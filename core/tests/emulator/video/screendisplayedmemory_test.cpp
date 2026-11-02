/// @file screendisplayedmemory_test.cpp
/// @brief Screen::DescribeDisplayedMemory per video family: the byte ranges each renderer reads, the source of the TTD
/// timeline's `screen` tag (docs/inprogress/2026-10-31-ttd-timeline-summary, design §5). Most checks compare the 4 KB
/// pieces the ranges touch (machine RAM piece = page * 4 + offset / 4 KB, or a device region's piece); the ZX one
/// compares bytes.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testtier.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/platform.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/video/screen.h"

namespace
{
using Piece = std::pair<std::string, uint32_t>;   // region ("ram" for machine RAM), piece
using Pieces = std::set<Piece>;

Pieces Ram(std::initializer_list<std::pair<uint32_t, uint32_t>> runs)   // {first piece, count}
{
    Pieces out;
    for (const auto& [first, count] : runs)
        for (uint32_t k = 0; k < count; ++k)
            out.insert({"ram", first + k});
    return out;
}
}  // namespace

class ScreenDisplayedMemory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool Make(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        _context = _emulator ? _emulator->GetContext() : nullptr;
        return _context && _context->pScreen;
    }

    Pieces Describe() const
    {
        std::vector<ScreenMemorySpan> spans;
        _context->pScreen->DescribeDisplayedMemory(spans);
        Pieces out;
        for (const ScreenMemorySpan& s : spans)
            if (s.bytes)
                for (uint32_t k = s.offset / 0x1000; k <= (s.offset + s.bytes - 1) / 0x1000; ++k)
                    out.insert({s.region ? s.region : "ram", k});
        return out;
    }

    /// The mode the latches select (as a frame start re-detects it)
    void Latch(uint8_t p7FFD, uint8_t pEFF7 = 0, uint8_t pFF77 = FF77_ZX, uint8_t pDFFD = 0)
    {
        EmulatorState& state = _context->emulatorState;
        state.p7FFD = p7FFD;
        state.pEFF7 = pEFF7;
        state.pFF77 = pFF77;
        state.pDFFD = pDFFD;
        _context->pScreen->InitRaster();
    }
};

TEST_F(ScreenDisplayedMemory_Test, ZxReadsTheSixKilobytesOfItsVideoPage)
{
    ASSERT_TRUE(Make("48K"));
    Latch(0x08);   // no shadow screen on the 48K
    EXPECT_EQ(Describe(), Ram({{20, 2}}));
    std::vector<ScreenMemorySpan> spans;
    _context->pScreen->DescribeDisplayedMemory(spans);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].region, nullptr);
    EXPECT_EQ(spans[0].offset, 5u * 0x4000u);
    EXPECT_EQ(spans[0].bytes, 6912u) << "pixels and attributes, not the system variables after them";
}

TEST_F(ScreenDisplayedMemory_Test, PentagonFollowsTheShadowScreenAndTheAlcoModes)
{
    ASSERT_TRUE(Make("PENTAGON"));
    Latch(0x00);
    EXPECT_EQ(Describe(), Ram({{20, 2}}));
    Latch(0x08);
    EXPECT_EQ(Describe(), Ram({{28, 2}}));
    Latch(0x00, EFF7_4BPP);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_P16);
    EXPECT_EQ(Describe(), Ram({{16, 8}})) << "pages 4 and 5 whole";
    Latch(0x08, EFF7_4BPP);
    EXPECT_EQ(Describe(), Ram({{24, 8}})) << "pages 6 and 7";
    Latch(0x00, EFF7_HWMC);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_PMC);
    EXPECT_EQ(Describe(), Ram({{20, 4}})) << "pixels and the 8 x 1 attributes at #2000";
}

TEST_F(ScreenDisplayedMemory_Test, AtmReadsBothPagesOfItsPlanes)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1)";
    ASSERT_TRUE(Make("ATM710"));
    Latch(0x00, 0, FF77_16);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_ATM16);
    EXPECT_EQ(Describe(), Ram({{20, 4}, {4, 4}}));
    Latch(0x08, 0, FF77_MC);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_ATMHR);
    EXPECT_EQ(Describe(), Ram({{28, 4}, {12, 4}}));
    Latch(0x00, 0, FF77_TX);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_ATMTX);
    Pieces text = Ram({{20, 1}, {22, 1}, {4, 1}, {6, 1}});
    text.insert({"device.EvoFontRam", 0});
    EXPECT_EQ(Describe(), text);
}

TEST_F(ScreenDisplayedMemory_Test, EvoTextLinearReadsItsOwnPage)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1)";
    ASSERT_TRUE(Make("ATM3"));
    Latch(0x00, 0, FF77_TL);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_ATMTL);
    Pieces text = Ram({{32, 4}});
    text.insert({"device.EvoFontRam", 0});
    EXPECT_EQ(Describe(), text);
}

TEST_F(ScreenDisplayedMemory_Test, ProfiHiResReadsItsPixelAndAttributePages)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1)";
    ASSERT_TRUE(Make("PROFI"));
    Latch(0x00, 0, FF77_ZX, 0x80);
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_PROFIHR);
    const uint32_t attributes = (0x38u & _context->pMemory->GetRamMask()) * 4u;
    EXPECT_EQ(Describe(), Ram({{16, 4}, {attributes, 4}}));
}

TEST_F(ScreenDisplayedMemory_Test, TsConfReadsItsGraphicsTilesAndSprites)
{
    ASSERT_TRUE(Make("TSL"));
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    TsConfState& ts = decoder->GetState();
    ts.regs[TsConfReg::VConfig] = 0x00;
    ts.regs[TsConfReg::VPage] = 0x05;
    ts.regs[TsConfReg::TConfig] = 0x00;
    EXPECT_EQ(Describe(), Ram({{20, 2}})) << "ZX layout at V_PAGE";

    ts.regs[TsConfReg::VConfig] = 0x01;   // 16 colours: 128 KB from V_PAGE & #F8
    ts.regs[TsConfReg::VPage] = 0x13;
    EXPECT_EQ(Describe(), Ram({{0x10 * 4, 32}}));

    ts.regs[TsConfReg::VConfig] = 0x22;   // 256 colours, the graphics layer off
    ts.regs[TsConfReg::TConfig] = 0xA0;   // tile layer 0 and sprites
    ts.regs[TsConfReg::TMapPage] = 0x40;
    ts.regs[TsConfReg::T0GPage] = 0x48;
    ts.regs[TsConfReg::SGPage] = 0x50;
    EXPECT_EQ(Describe(), Ram({{0x40 * 4, 4}, {0x48 * 4, 32}, {0x50 * 4, 32}}));

    ts.regs[TsConfReg::VConfig] = 0x03;   // text: 16 KB at V_PAGE, the font in the first 4 KB of V_PAGE ^ 1
    ts.regs[TsConfReg::VPage] = 0x08;
    ts.regs[TsConfReg::TConfig] = 0x00;
    EXPECT_EQ(Describe(), Ram({{0x08 * 4, 4}, {0x09 * 4, 1}}));
}

TEST_F(ScreenDisplayedMemory_Test, NextReadsTheBanksOfItsLayers)
{
    ASSERT_TRUE(Make("NEXT"));
    auto* decoder = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    NextBoard& board = decoder->Board();
    EXPECT_EQ(Describe(), Ram({{20, 2}})) << "the ULA of bank 5";

    board.Video().WritePortFf(0x02);   // Timex hi-colour: attributes at #2000
    EXPECT_EQ(Describe(), Ram({{20, 4}}));
    board.Video().WritePortFf(0x00);

    board.Write(0x12, 9);              // Layer 2 at 16K bank 9, 256 x 192
    board.Write(0x70, 0x00);
    board.Video().WritePort123b(0x02);
    EXPECT_EQ(Describe(), Ram({{20, 2}, {36, 16}}));
    board.Write(0x70, 0x10);           // 320 x 256: five banks
    EXPECT_EQ(Describe(), Ram({{20, 2}, {36, 20}}));
    board.Video().WritePort123b(0x00);

    board.Write(0x68, 0x80);           // the ULA off, the tilemap on with its definitions in bank 7
    board.Write(0x6B, 0x80);
    board.Write(0x6E, 0x00);
    board.Write(0x6F, 0x80);
    EXPECT_EQ(Describe(), Ram({{20, 4}, {28, 2}}));
}

TEST_F(ScreenDisplayedMemory_Test, SprinterReadsItsWholeVideoRam)
{
    ASSERT_TRUE(Make("SPRINTER"));
    Pieces vram;
    for (uint32_t k = 0; k < 64; ++k)
        vram.insert({"sprinter.vram", k});
    EXPECT_EQ(Describe(), vram);
}
