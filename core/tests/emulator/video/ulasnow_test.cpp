#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"

/// ULA snow (docs/inprogress/2026-09-29-ula-snow/tdd.md): a refresh cycle while I points into slow memory,
/// whose T3 falls on the ULA's pixel byte 1 fetch, puts R (before its increment) on the low 7 address bits of
/// that cell's pixel and attribute byte; on pixel byte 2's fetch the second cell repeats the first. The tick is
/// the floating bus's (validated by Butler's hardware tests 36 / 37) and was fixed on Snow Hold's photos of
/// three real 48K machines (UlaSnowRender_Test)
class UlaSnow_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    UlaContention* _ula = nullptr;

    void Create(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _ula = _context->pUlaContention;
        _context->config.floatbus = 1;  // the tests find the fetch ticks through the floating bus
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// Screen line 0: pixel byte #10+n and attribute #40+n in cell n, so the floating bus names the cell
    void FillLineZero()
    {
        for (uint16_t n = 0; n < 32; n++)
        {
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x4000 + n), static_cast<uint8_t>(0x10 + n));
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x5800 + n), static_cast<uint8_t>(0x40 + n));
        }
    }

    /// The frame T-state at which the floating bus reads cell `cell`'s pixel byte (the fetch tick), 0 if none.
    /// The whole frame is searched: frame T-state 0 is not the interrupt on every model
    uint32_t PixelFetchT(uint32_t cell)
    {
        const uint32_t frame = _ula->GetRaster().configFrameDuration;
        for (uint32_t t = 0; t < frame; t++)
        {
            _z80->t = t;
            if (_ula->GetFloatingBus() == 0x10 + cell)
                return t;
        }
        return 0;
    }

    /// Runs one LD A,0 at #8000 (uncontended) whose M1 starts at `t`, with I and R as given
    void StepLdAt(uint32_t t, uint8_t i, uint8_t r)
    {
        _memory->DirectWriteToZ80Memory(0x8000, 0x3E);
        _memory->DirectWriteToZ80Memory(0x8001, 0x00);
        _z80->pc = 0x8000;
        _z80->iff1 = 0;
        _z80->i = i;
        _z80->r_low = r;
        _z80->t = t;
        _z80->Z80Step();
    }

    bool Marked(uint32_t y, uint32_t cell, uint16_t& pixelOffset, uint16_t& attrOffset) const
    {
        pixelOffset = static_cast<uint16_t>(cell);
        attrOffset = static_cast<uint16_t>(0x1800 + cell);
        (void)y;
        return _ula->SnowOffsets(y, cell, pixelOffset, attrOffset);
    }
};

/// The tick: T3 of the refresh on the fetch of pixel byte 1 snows that cell, with R before the M1's increment
TEST_F(UlaSnow_Test, RefreshOnPixelFetchSnowsTheCell)
{
    Create("48K");
    FillLineZero();
    const uint32_t tPixel = PixelFetchT(4);  // cell 4: the first cell of the third 16-pixel group
    ASSERT_NE(tPixel, 0u) << "the floating bus never reads cell 4";

    StepLdAt(tPixel - 2, 0x40, 0x25);  // T1 T2 T3: T3 two ticks after the start

    uint16_t pixel, attr;
    ASSERT_TRUE(Marked(0, 4, pixel, attr)) << "no snow on cell 4";
    EXPECT_EQ(pixel, 0x0025) << "bits 6..0 from R before the increment (#25)";
    EXPECT_EQ(attr, 0x1825);
    EXPECT_FALSE(Marked(0, 5, pixel, attr));
    EXPECT_FALSE(Marked(0, 3, pixel, attr));
}

/// T3 on pixel byte 2's fetch: the second cell shows the first cell's bytes (double)
TEST_F(UlaSnow_Test, RefreshOnSecondPixelFetchDoublesTheFirstCell)
{
    Create("48K");
    FillLineZero();
    const uint32_t tPixel = PixelFetchT(5);
    ASSERT_NE(tPixel, 0u);

    StepLdAt(tPixel - 2, 0x40, 0x25);

    uint16_t pixel, attr;
    ASSERT_TRUE(Marked(0, 5, pixel, attr)) << "no double on cell 5";
    EXPECT_EQ(pixel, 0x0004) << "cell 5 shows cell 4's pixel byte";
    EXPECT_EQ(attr, 0x1804);
    EXPECT_FALSE(Marked(0, 4, pixel, attr));
}

/// Every other tick of the group leaves the screen alone
TEST_F(UlaSnow_Test, OtherTicksDoNothing)
{
    Create("48K");
    FillLineZero();
    const uint32_t tPixel = PixelFetchT(4);
    ASSERT_NE(tPixel, 0u);
    for (uint32_t d : { 1u, 3u, 5u, 6u, 7u })
    {
        StepLdAt(tPixel - 2 + d, 0x40, 0x25);
        uint16_t pixel, attr;
        for (uint32_t cell = 0; cell < 32; cell++)
            EXPECT_FALSE(Marked(0, cell, pixel, attr)) << "tick +" << d << ", cell " << cell;
    }
}

/// I in fast memory, the gate array, the clones and contention switched off: no snow. The first case is the
/// positive control: the same sweep on a 48K with I = #40 snows
TEST_F(UlaSnow_Test, OnlyTheFerrantiUlaWithIInSlowMemory)
{
    struct Case
    {
        const char* model;
        uint8_t i;
        bool contention;
        bool snows;
    };
    for (const Case& c : { Case{ "48K", 0x40, true, true }, Case{ "48K", 0x80, true, false },
                           Case{ "48K", 0x3F, true, false }, Case{ "48K", 0x40, false, false },
                           Case{ "PLUS3", 0x40, true, false }, Case{ "PENTAGON", 0x40, true, false } })
    {
        Create(c.model);
        _context->pFeatureManager->setFeature(Features::kContention, c.contention);
        FillLineZero();
        // Every tick of the first two 16-pixel groups of screen line 0 (the model's own raster)
        const ContentionRaster& r = _ula->GetRaster();
        const uint32_t lineZero = r.screenAreaStart + r.screenLineAreaStart;
        for (uint32_t t = lineZero - 16; t < lineZero + 16; t++)
            StepLdAt(t, c.i, 0x25);
        uint32_t marks = 0;
        uint16_t pixel, attr;
        for (uint32_t cell = 0; cell < 32; cell++)
            marks += Marked(0, cell, pixel, attr) ? 1u : 0u;
        if (c.snows)
            EXPECT_GT(marks, 0u) << c.model << ": the positive control did not snow";
        else
            EXPECT_EQ(marks, 0u) << c.model << " I=#" << std::hex << int(c.i) << " contention " << c.contention;
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

/// The 128K: I at #C0-#FF snows only while an odd (slow) page is at #C000
TEST_F(UlaSnow_Test, Upper128KSlotOnlyWithAnOddPage)
{
    Create("128K");
    FillLineZero();
    const uint32_t tPixel = PixelFetchT(4);
    ASSERT_NE(tPixel, 0u);
    uint16_t pixel, attr;

    _context->pCore->GetZ80()->pc = 0;
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x00, 0);  // page 0 (fast) at #C000
    StepLdAt(tPixel - 2, 0xC0, 0x25);
    EXPECT_FALSE(Marked(0, 4, pixel, attr)) << "page 0 is fast";

    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x01, 0);  // page 1 (slow)
    StepLdAt(tPixel - 2, 0xC0, 0x25);
    EXPECT_TRUE(Marked(0, 4, pixel, attr)) << "page 1 is slow";
}

/// Snow Hold's beta (Mark Woodmass, testdata/contention/snow-hold) on the 48K against its photos from three
/// real machines: two ladders under each of the three pattern bands, at character columns 2 and 16, one lit
/// line every 4 screen lines, and nothing else in the empty lines. Loads a tape and renders pixels (about a
/// second): the one test that pins the model's tick and R to hardware
class UlaSnowRender_Test : public RomEditorFixture
{
};

TEST_F(UlaSnowRender_Test, SnowHoldMatchesTheHardwarePhotos)
{
    BootEditor("48K");
    ASSERT_FALSE(HasFatalFailure());
    _context->pFeatureManager->setFeature(Features::kFastTape, true);
    const std::string tap =
        (TestPathHelper::FindProjectRoot() / "testdata" / "contention" / "snow-hold" / "snowhold-beta.tap").string();
    ASSERT_TRUE(_emulator->LoadTape(tap));
    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
    typer->Request("LOAD \"\"", CommandTyper::Options{});
    ASSERT_TRUE(RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000));
    ASSERT_TRUE(RunUntil([&] { return _context->pCore->GetZ80()->i == 0x40; }, 3000)) << "Snow Hold never set I";
    RunFrames(10);
    _emulator->DisableTurboMode();
    _context->pFeatureManager->setFeature(Features::kScreenHQ, true);  // every tick rendered
    RunFrames(2);

    const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
    const RasterDescriptor& rd = _context->pScreen->GetTimingDescriptor(_context->pScreen->GetVideoMode());
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    auto lit = [&](uint32_t y, uint32_t cell) {
        const uint32_t* row = pixels + (rd.screenOffsetTop + y) * fb.width + rd.screenOffsetLeft + cell * 8;
        for (int x = 0; x < 8; x++)
            if ((row[x] & 0x00FFFFFFu) != 0)
                return true;
        return false;
    };

    for (uint32_t band = 0; band < 3; band++)
    {
        const uint32_t top = band * 64;  // the beta draws one band at the top of each screen third
        uint32_t ladderLines[2] = { 0, 0 };
        for (uint32_t y = top + 8; y < top + 64; y++)
        {
            for (uint32_t cell = 0; cell < 32; cell++)
            {
                const bool ladder = cell == 2 || cell == 16;
                if (!ladder)
                {
                    EXPECT_FALSE(lit(y, cell)) << "band " << band << ": snow at line " << y << ", column " << cell;
                    continue;
                }
                if (lit(y, cell))
                {
                    EXPECT_EQ((y - top) % 4, 0u) << "band " << band << ", column " << cell << ": lit line " << y;
                    ladderLines[cell == 16]++;
                }
            }
        }
        EXPECT_GE(ladderLines[0], 4u) << "band " << band << ": no ladder at column 2";
        EXPECT_GE(ladderLines[1], 4u) << "band " << band << ": no ladder at column 16";
    }
}
