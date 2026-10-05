// TS-Conf line engine (TSConf implementation-plan phase 3 ENG-2...4, VID-7;
// hardware-spec §3.2, §4.2): line-start latching, the graphics row counter,
// independence from rendering.

#include "tsconffixture.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include "emulator/platforms/tsconf/tsconfgeometry.h"
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
        // Both boots from the same RAM: the screen pages power up random, and a
        // cache line that read them keeps the word after it is invalidated
        auto emulator = manager->CreateEmulatorWithModelAndRAM(turbo ? "tsconf-eng4-t" : "tsconf-eng4-r", "TSL", 4096,
                                                               LoggerLevel::LogError, nullptr,
                                                               [](CONFIG& config) { config.ramPowerOn = RamPowerOn::Zero; });
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

/// ENG-1: the per-line DRAM budget - 448 accesses; video takes its blocks while
/// video_go is high, w + 4 dots ([V] video_sync.v:237, video_mode.v:84-89,
/// 128-133): ceil((w + 4) / len) x need - ZX 1 of 8, 16C 1 of 4, 256C 1 of 2,
/// TXT 4 of 8 (was w >> shift, 1-4 low: TS-Conf audit, dma row 42); none on
/// NOGFX or border lines; the CPU's accesses come off the rest and the DMA gets
/// what is left: a RAM copy (2 accesses per word) moves (448 - video - CPU) / 2
/// words over a whole line
TEST_F(TsConfEngine_Test, ENG1_LineBudget)
{
    struct Case
    {
        uint8_t vConfig;
        uint16_t video;
    };
    for (const Case& c : {Case{0x00, 33}, Case{0x41, 81}, Case{0x42, 162}, Case{0x83, 164}, Case{0xC2, 182}, Case{0x22, 0}})
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

/// TSU-9: the pass that draws line L runs from ts_start of L - 1 to the next ts_start and on a busy line crosses
/// line_start of L. An object handed to the renderer after line_start takes L's latched T0/T1_G_PAGE, T0/T1_X_OFFS
/// and PAL_SEL ([V] video_ports.v:153-165, video_ts.v:162-171; TS-Conf audit tsu row 49): the engine draws the
/// pass up to the DRAM position of line_start at ts_start and the rest at line_start. The reference lines come
/// from the real Verilog (tools/machines/tsconf/rtl-sim tsulatch): the "before" registers latched for L - 1, the
/// "after" ones written at dot 420 of L - 1 (after ts_start, before line_start), one TS line captured (window line
/// 20, or a bottom line, where the tilemap prefetch has stopped). `split` is the RTL's count of TSU DRAM cycles
/// before line_start; TXT cases check it only (a TXT line mixes the TS layer in hi-res)
TEST_F(TsConfEngine_Test, TSU9_ObjectsAfterLineStartTakeTheNewLatch)
{
    // The harness's memory (rtl-sim/harness.cpp FillTsuMemory): graphics pages 80h..AFh, the tile map at 30h
    // (layer l, column c = tile c of bitmap row 1 + l, palette c & 3), V_PAGE C0h zero
    for (uint16_t page = 0x80; page < 0xB0; page++)
        for (uint32_t offset = 0; offset < PAGE_SIZE; offset++)
        {
            const uint32_t a = page * PAGE_SIZE + offset;
            Ram(page, static_cast<uint16_t>(offset)) = static_cast<uint8_t>(a * 7 + (a >> 8) * 13 + (a >> 17) * 101);
        }
    for (uint16_t page = 0xC0; page < 0xD0; page++)
        std::memset(_memory->RAMPageAddress(page), 0, PAGE_SIZE);
    for (uint32_t row = 0; row < 64; row++)
        for (uint32_t l = 0; l < 2; l++)
            for (uint32_t c = 0; c < 64; c++)
            {
                const uint16_t entry = static_cast<uint16_t>(c | ((1 + l) << 6) | ((c & 3) << 12));
                Ram(0x30, static_cast<uint16_t>(row * 256 + l * 128 + c * 2)) = static_cast<uint8_t>(entry);
                Ram(0x30, static_cast<uint16_t>(row * 256 + l * 128 + c * 2 + 1)) = static_cast<uint8_t>(entry >> 8);
            }
    TsConfState& ts = _decoder->GetState();
    auto tileRegs = [&](unsigned palSel, unsigned t0Page, unsigned t1Page, unsigned t0x, unsigned t1x) {
        Reg(TsConfReg::PalSel, static_cast<uint8_t>(palSel));
        Reg(TsConfReg::T0GPage, static_cast<uint8_t>(t0Page));
        Reg(TsConfReg::T1GPage, static_cast<uint8_t>(t1Page));
        Reg(TsConfReg::T0XOffsL, static_cast<uint8_t>(t0x));
        Reg(TsConfReg::T0XOffsL + 1, static_cast<uint8_t>(t0x >> 8));
        Reg(TsConfReg::T0XOffsL + 4, static_cast<uint8_t>(t1x));
        Reg(TsConfReg::T0XOffsL + 5, static_cast<uint8_t>(t1x >> 8));
    };

    std::ifstream file(TestPathHelper::GetTestDataPath("machines/tsconf/rtl-sim/tsu-latch.txt"));
    ASSERT_TRUE(file.good());
    std::string text, name, goes;
    unsigned vConfig = 0, tConfig = 0, tsLine = 0, s0 = 0, s1 = 0, split = 0;
    unsigned before[5] = {}, after[5] = {};
    int cases = 0, crossing = 0;
    while (std::getline(file, text))
    {
        if (text.rfind("case ", 0) == 0)
        {
            char buffer[64] = {};
            ASSERT_EQ(std::sscanf(text.c_str(), "case %63s vconf=%x tsconf=%x line=%u s0=%u s1=%u before=%x,%x,%x,%u,%u after=%x,%x,%x,%u,%u split=%u",
                                  buffer, &vConfig, &tConfig, &tsLine, &s0, &s1, &before[0], &before[1], &before[2], &before[3],
                                  &before[4], &after[0], &after[1], &after[2], &after[3], &after[4], &split),
                      17)
                << text;
            name = buffer;
            continue;
        }
        if (text.rfind("go", 0) == 0)
        {
            goes = text;
            continue;
        }
        if (text.rfind("idx", 0) != 0)
            continue;
        std::istringstream in(text.substr(3));
        std::vector<int> expected;
        for (unsigned v; in >> std::hex >> v;)
            expected.push_back(static_cast<int>(v));
        SCOPED_TRACE(name);
        cases++;

        // SFILE as the harness writes it: S0 = s0 sprites 64x8 on the TS line (LEAP on the last), S1 = s1
        // sprites, an inactive LEAP descriptor for an empty layer and one that ends S2
        std::memset(ts.sfile, 0, sizeof(ts.sfile));
        uint32_t d = 0;
        auto sprite = [&](bool leap) {
            ts.sfile[d * 3] = static_cast<uint16_t>(tsLine | 0x2000u | (leap ? 0x4000u : 0u));
            ts.sfile[d * 3 + 1] = static_cast<uint16_t>(((d * 23) & 0x1FF) | (7 << 9));
            ts.sfile[d * 3 + 2] = static_cast<uint16_t>(((d * 8) & 0x3F) | ((d & 7) << 6) | ((d & 15) << 12));
            d++;
        };
        for (unsigned i = 0; i < s0; i++)
            sprite(i == s0 - 1);
        if (s0 == 0)
            ts.sfile[d++ * 3] = 0x4000;
        for (unsigned i = 0; i < s1; i++)
            sprite(i == s1 - 1);
        if (s1 == 0)
            ts.sfile[d++ * 3] = 0x4000;
        ts.sfile[d++ * 3] = 0x4000;

        Reg(TsConfReg::VConfig, static_cast<uint8_t>(vConfig));
        Reg(TsConfReg::VPage, 0xC0);
        Reg(TsConfReg::TMapPage, 0x30);
        Reg(TsConfReg::SGPage, 0xA0);
        for (uint8_t r = 0; r < 8; r++)
            Reg(static_cast<uint8_t>(TsConfReg::T0XOffsL + r), 0);
        tileRegs(before[0], before[1], before[2], before[3], before[4]);
        Reg(TsConfReg::TConfig, static_cast<uint8_t>(tConfig));

        const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(static_cast<uint8_t>(vConfig));
        const uint32_t line = win.y0 + tsLine;
        NewFrame();
        RunTo(T(line - 1, 210));  // dot 420
        tileRegs(after[0], after[1], after[2], after[3], after[4]);
        RunTo(T(line + 1, 0));

        // The pass crossed line_start when the RTL handed an object over after it
        if (goes.find(":L") != std::string::npos)
        {
            crossing++;
            EXPECT_EQ(Engine().Line(line).tsuSplit, split) << "TSU DRAM cycles before line_start";
        }
        if ((vConfig & 3) == 3)
            continue;
        ASSERT_EQ(expected.size(), win.w);
        std::vector<int> actual;
        for (uint32_t x = 0; x < win.w; x++)
            actual.push_back(Engine().TsuPixel(line, win.x0 + x));
        size_t first = 0;
        while (first < actual.size() && actual[first] == expected[first])
            first++;
        EXPECT_EQ(first, actual.size()) << "first difference at TS x " << first << ": "
                                        << (first < actual.size() ? actual[first] : 0) << " vs the RTL's "
                                        << (first < actual.size() ? expected[first] : 0);
    }
    EXPECT_EQ(cases, 12);
    EXPECT_EQ(crossing, 10);
}
