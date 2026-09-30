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
