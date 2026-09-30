// TS-Conf line engine (TSConf implementation-plan phase 3 ENG-2...4, VID-7;
// hardware-spec §3.2, §4.2): line-start latching, the graphics row counter,
// independence from rendering.

#include "tsconffixture.h"

#include <cstring>

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include "emulator/video/tsconf/screentsconf.h"

class TsConfEngine_Test : public TsConfFixture
{
protected:
    TsConfEngine& Engine() { return _decoder->GetEngine(); }

    /// Frame T-state of raster line `line`, tact `tact` (3.5 MHz)
    static uint32_t T(uint32_t line, uint32_t tact) { return line * TsConfEngine::kLineTacts + tact; }

    /// Start a new frame and bring the engine and the CPU clock to `t`
    void RunTo(uint32_t t)
    {
        Engine().CatchUp(t);
        _z80->t = t;
    }

    void NewFrame()
    {
        Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        _z80->t = 0;
    }
};

/// ENG-2: a latched register written mid-line takes effect on the next line
TEST_F(TsConfEngine_Test, ENG2_LatchedRegisterActsFromTheNextLine)
{
    NewFrame();
    RunTo(T(100, 120));
    Reg(TsConfReg::VPage, 0x20);
    Reg(TsConfReg::PalSel, 0x03);
    Reg(TsConfReg::GXOffsL, 0x11);
    RunTo(T(319, 0));
    EXPECT_EQ(Engine().Line(100).vPage, 0x05);
    EXPECT_EQ(Engine().Line(100).palSel, 0x0F);
    EXPECT_EQ(Engine().Line(101).vPage, 0x20);
    EXPECT_EQ(Engine().Line(101).palSel, 0x03);
    EXPECT_EQ(Engine().Line(101).gxOffs, 0x11);
    EXPECT_EQ(Engine().Line(200).vPage, 0x20);
}

/// ENG-2: BORDER acts at the dot - the rest of the same line changes
TEST_F(TsConfEngine_Test, ENG2_BorderChangesWithinTheLine)
{
    auto* screen = dynamic_cast<ScreenTSConf*>(_context->pScreen);
    ASSERT_NE(screen, nullptr);
    TsConfState& ts = _decoder->GetState();
    ts.cram[0x10] = 0x7C00;
    ts.cram[0x20] = 0x001F;
    Reg(TsConfReg::Border, 0x10);

    NewFrame();
    screen->InitRaster();
    screen->Reset();
    RunTo(T(50, 100));
    screen->UpdateScreen();
    Reg(TsConfReg::Border, 0x20);
    RunTo(T(319, 223));
    screen->UpdateScreen();

    uint32_t* fb = nullptr;
    size_t size = 0;
    screen->GetFramebufferData(&fb, &size);
    const uint32_t y = 50 - 32;
    EXPECT_EQ(fb[y * 720 + (99 - 44) * 4], ScreenTSConf::CramToRgba(0x7C00)) << "before the write";
    EXPECT_EQ(fb[y * 720 + (101 - 44) * 4], ScreenTSConf::CramToRgba(0x001F)) << "after the write, same line";
}

/// ENG-3: the row counter - reload with G_Y_OFFS after line 31, +1 per window
/// line, and reload with the written value (not value + elapsed lines)
TEST_F(TsConfEngine_Test, ENG3_RowCounterReloadsWithTheWrittenValue)
{
    NewFrame();
    RunTo(T(100, 50));
    EXPECT_EQ(Engine().Line(79).cntRow, 0) << "above the rres 0 window: no step";
    EXPECT_EQ(Engine().Line(80).cntRow, 0);
    EXPECT_EQ(Engine().Line(100).cntRow, 20);

    Reg(TsConfReg::GYOffsL, 50);
    RunTo(T(103, 0));
    EXPECT_EQ(Engine().Line(100).cntRow, 20) << "the current line keeps its row";
    EXPECT_EQ(Engine().Line(101).cntRow, 50);
    EXPECT_EQ(Engine().Line(102).cntRow, 51);

    NewFrame();
    RunTo(T(81, 0));
    EXPECT_EQ(Engine().Line(80).cntRow, 50) << "next frame: the reload after line 31";
    EXPECT_EQ(Engine().Line(81).cntRow, 51);
}

/// ENG-3: offsets wrap at 512 rows; the window geometry decides where rows step
TEST_F(TsConfEngine_Test, ENG3_RowCounterWrapsAndFollowsTheGeometry)
{
    Reg(TsConfReg::GYOffsL, 0xFF);
    Reg(TsConfReg::GYOffsH, 0x01);  // 511
    Reg(TsConfReg::VConfig, 0xC0);  // rres 3: the window starts at line 32
    NewFrame();
    RunTo(T(40, 0));
    EXPECT_EQ(Engine().Line(32).cntRow, 511);
    EXPECT_EQ(Engine().Line(33).cntRow, 0) << "9-bit wrap";
    EXPECT_EQ(Engine().Line(40).cntRow, 7);
}

/// VID-7 / P7F-7: #7FFD switches V_PAGE on the current line
TEST_F(TsConfEngine_Test, VID7_ScreenBitActsOnTheCurrentLine)
{
    NewFrame();
    RunTo(T(120, 30));
    Out(0x7FFD, 0x08);
    EXPECT_EQ(Engine().Line(120).vPage, 0x07);
    RunTo(T(122, 0));
    EXPECT_EQ(Engine().Line(121).vPage, 0x07);
}

/// ENG-4: the engine's results do not depend on whether frames are rendered:
/// the same boot with and without turbo render decimation ends in the same
/// TS-Conf state (TTD blob) and CPU position. Runtime: two 60-frame boots of
/// the real ROM (~120 ms), which is the point of the test
TEST(TsConfEngineRender_Test, ENG4_SameStateRenderedOrDecimated)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_NE(manager, nullptr);

    auto run = [&](bool turbo, TsConfState& out, uint16_t& pc) {
        auto emulator = manager->CreateEmulatorWithModelAndRAM(turbo ? "tsconf-eng4-t" : "tsconf-eng4-r", "TSL", 4096,
                                                               LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* context = emulator->GetContext();
        auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
        ASSERT_NE(decoder, nullptr);
        decoder->GetRtc().SetFixedTime(1767268830);
        if (turbo)
            emulator->EnableTurboMode();
        emulator->RunNFrames(60, true);
        out = decoder->GetState();
        pc = context->pCore->GetZ80()->pc;
        manager->RemoveEmulator(emulator->GetUUID());
    };

    TsConfState rendered{}, decimated{};
    uint16_t pcRendered = 0, pcDecimated = 0;
    run(false, rendered, pcRendered);
    run(true, decimated, pcDecimated);
    EXPECT_EQ(pcRendered, pcDecimated);
    EXPECT_EQ(std::memcmp(&rendered, &decimated, sizeof(TsConfState)), 0);
}

/// ENG-1: the per-line DRAM budget - 448 accesses; video takes its share of
/// the window (ZX 1/8, 16C 1/4, 256C and TXT 1/2 of the window dots), none on
/// NOGFX or border lines; the CPU's reads come off the rest and the DMA gets
/// what is left: a RAM copy (2 accesses per word) moves (448 - video - CPU) / 2
/// words over a whole line
TEST_F(TsConfEngine_Test, ENG1_LineBudget)
{
    struct Case
    {
        uint8_t vConfig;
        uint16_t video;
    };
    for (const Case& c : {Case{0x00, 256 / 8}, Case{0x41, 320 / 4}, Case{0x42, 320 / 2}, Case{0x83, 320 / 2}, Case{0x22, 0}})
    {
        SCOPED_TRACE(int(c.vConfig));
        Reg(TsConfReg::VConfig, c.vConfig);
        NewFrame();
        RunTo(T(100, 0));
        EXPECT_EQ(Engine().Line(100).videoCost, c.video);
        EXPECT_EQ(Engine().Line(10).videoCost, 0) << "a border line fetches nothing";

        // A long RAM copy across line 100, the CPU reading 40 times on it
        TsConfState& ts = _decoder->GetState();
        Reg(TsConfReg::DmaSAl, 0);
        Reg(TsConfReg::DmaSAh, 0);
        Reg(TsConfReg::DmaSAx, 0x10);
        Reg(TsConfReg::DmaDAl, 0);
        Reg(TsConfReg::DmaDAh, 0);
        Reg(TsConfReg::DmaDAx, 0x20);
        Reg(TsConfReg::DmaLen, 0xFF);
        Reg(TsConfReg::DmaNum, 0xFF);
        Reg(TsConfReg::DmaCtrl, 0x01);
        const uint32_t before = ts.dmaDst;
        ts.cpuAccesses += 40;
        RunTo(T(101, 0));
        EXPECT_EQ(ts.dmaDst - before, (448u - c.video - 40u) / 2u) << "words moved on the line";
        _decoder->GetDma().Reset();
    }
}

/// TSU-6: the TSU draws line L during line L - 1 from ts_start ([V]
/// video_sync.v:130; 16C 320x200: dot 107 = tact 53). T_CONFIG written before
/// ts_start of line 99 acts on line 100; written after it, from line 101.
/// A tile X offset written in line 99 is latched at line 100 and acts on the
/// TSU from line 101 (the TSU works with the previous line's latches)
TEST_F(TsConfEngine_Test, TSU6_TsuDrawsDuringThePreviousLine)
{
    TsConfState& ts = _decoder->GetState();
    std::memset(ts.sfile, 0, sizeof(ts.sfile));
    for (uint16_t page = 0x20; page < 0x38; page++)
        std::memset(_memory->RAMPageAddress(page), 0, PAGE_SIZE);
    Reg(TsConfReg::VConfig, 0x41);  // 16C 320x200: lines 76..275, window dot 108
    Reg(TsConfReg::SGPage, 0x20);
    // Sprite 0: 16x64 at TS (0, 0) - lines 76..139, dots 108..123; graphics all colour 3
    ts.sfile[0] = static_cast<uint16_t>(0x2000 | (7 << 9));
    ts.sfile[1] = static_cast<uint16_t>(1 << 9);
    for (uint32_t row = 0; row < 64; row++)
        for (uint32_t b = 0; b < 8; b++)
            Ram(0x20, row * 256 + b) = 0x33;

    auto drawn = [&](uint32_t line) { return Engine().TsuPixel(line, 110) != 0; };

    // Before ts_start of line 99: line 100 already without the TSU
    Reg(TsConfReg::TConfig, 0x80);
    NewFrame();
    RunTo(T(99, 30));
    ASSERT_EQ(Engine().TsStartTact(), 53u);
    Reg(TsConfReg::TConfig, 0x00);
    RunTo(T(102, 0));
    EXPECT_TRUE(drawn(99));
    EXPECT_FALSE(drawn(100)) << "written before ts_start of line 99";

    // After ts_start of line 99: line 100 still drawn, 101 not
    Reg(TsConfReg::TConfig, 0x80);
    NewFrame();
    RunTo(T(99, 100));
    Reg(TsConfReg::TConfig, 0x00);
    RunTo(T(102, 0));
    EXPECT_TRUE(drawn(100)) << "line 100 was drawn at ts_start of line 99";
    EXPECT_FALSE(drawn(101));

    // Tile X offset: tile layer 0, one tile (map column 1) of colour 5 at TS x 8..15
    Reg(TsConfReg::TMapPage, 0x30);
    Reg(TsConfReg::T0GPage, 0x28);
    for (uint32_t row = 0; row < 64; row++)
    {
        Ram(0x30, row * 256 + 2) = 1;  // column 1 = tile 1 (bitmap x 8..15)
        for (uint32_t b = 4; b < 8; b++)
            Ram(0x28, (row & 7) * 256 + b) = 0x55;
    }
    Reg(TsConfReg::TConfig, 0x20);
    Reg(0x40, 0);
    NewFrame();
    RunTo(T(99, 30));
    Reg(0x40, 4);  // T0 X offset 4: the tile moves 4 dots left
    RunTo(T(103, 0));
    const uint32_t x0 = Engine().Line(100).tsX0;
    EXPECT_EQ(Engine().TsuPixel(100, x0 + 8) & 0x0F, 5) << "line 100: drawn with line 99's latch";
    EXPECT_EQ(Engine().TsuPixel(101, x0 + 4) & 0x0F, 5) << "line 101: the offset latched at line 100";
    EXPECT_EQ(Engine().TsuPixel(101, x0 + 12), 0);
}
