// The Profi boards' CPU wait states (profiwaitoverlay.h) and the TURBO switch. Expected durations are the worked
// examples of docs/inprogress/2026-10-01-profi-v3-v5/research-profi-v5-wait.md (v5, 3.5 MHz) and
// research-profi-v3-turbo-floatbus.md A5 (v3 turbo). Z80::t counts the current CPU clock: 3.5 MHz T, or 7 MHz
// clocks in turbo.

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/ports/models/portdecoder_profi.h"

namespace
{
class ProfiWaitOverlay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Profi* _decoder = nullptr;

    /// Paper line 10's fetch window starts here (frame T at 3.5 MHz): q = 0
    static constexpr uint32_t kPaper = kProfiPaperStartT + 10 * 224;
    /// The same line's right border: q = 150
    static constexpr uint32_t kBorder = kPaper + 150;

    void Create(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _core = _context->pCore;
        _z80 = _core->GetZ80();
        _decoder = dynamic_cast<PortDecoder_Profi*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _z80->iff1 = 0;
        // Out of the SYS session, RAM page 2 at #8000, page 0 at #C000
        _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);
        _context->pMemory->UpdateZ80Banks();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Poke(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _z80->DirectWrite(addr++, b);
    }

    /// Clocks of `count` instructions from `pc`, one after another from clock `start`
    std::vector<uint32_t> Run(uint16_t pc, uint32_t start, size_t count)
    {
        std::vector<uint32_t> clocks;
        _z80->pc = pc;
        _z80->t = start;
        for (size_t i = 0; i < count; i++)
        {
            const uint32_t before = _z80->t;
            _z80->Z80Step();
            clocks.push_back(_z80->t - before);
        }
        return clocks;
    }

    /// Rebuild the overlay after a [PROFI] setting changed in the test
    void Reinstall()
    {
        const uint8_t pentagon = _context->config.profi_wait_pentagon;
        _context->config.profi_wait_pentagon = 1;
        _decoder->SyncWaits();
        _context->config.profi_wait_pentagon = pentagon;
        _decoder->SyncWaits();
    }
};
}  // namespace

/// region <v5: the video WAIT at 3.5 MHz>

TEST_F(ProfiWaitOverlay_Test, V5InstallsTheVideoWaitAtPowerOnUnlessSb8IsInPentagon)
{
    Create("PROFI");
    EXPECT_TRUE(_decoder->AreWaitsInstalled());
    _context->config.profi_wait_pentagon = 1;
    _decoder->SyncWaits();
    EXPECT_FALSE(_decoder->AreWaitsInstalled()) << "SB8 PENTAGON: no waits at 3.5 MHz";
    EXPECT_EQ(_core->GetBusOverlayCount(), 0u);
}

/// Phase 0: one wait on the even T of the fetch window; a NOP stream then settles on the odd T and never waits again
TEST_F(ProfiWaitOverlay_Test, V5NopStreamWaitsOnceInThePaperAndNeverInTheBorder)
{
    Create("PROFI");
    Poke(0x8000, {0x00, 0x00, 0x00, 0x00});
    EXPECT_EQ(Run(0x8000, kPaper, 4), (std::vector<uint32_t>{5, 4, 4, 4}));
    EXPECT_EQ(Run(0x8000, kBorder, 4), (std::vector<uint32_t>{4, 4, 4, 4}));
    EXPECT_EQ(Run(0x8000, kProfiPaperStartT - 224 * 10, 4), (std::vector<uint32_t>{4, 4, 4, 4})) << "top border";
}

/// research-profi-v5-wait.md, worked examples: LD A,(HL) in the paper takes 8, LD (HL),A 8, in the border 7
TEST_F(ProfiWaitOverlay_Test, V5RamReadsAndWritesTakeOneMoreInThePaper)
{
    Create("PROFI");
    Poke(0x8000, {0x7E, 0x7E, 0x7E});
    Poke(0x9000, {0x77, 0x77, 0x77});
    _z80->hl = 0xC000;
    EXPECT_EQ(Run(0x8000, kPaper, 3), (std::vector<uint32_t>{8, 8, 8})) << "LD A,(HL)";
    EXPECT_EQ(Run(0x9000, kPaper, 3), (std::vector<uint32_t>{8, 8, 8})) << "LD (HL),A";
    EXPECT_EQ(Run(0x8000, kBorder, 3), (std::vector<uint32_t>{7, 7, 7})) << "border";
}

TEST_F(ProfiWaitOverlay_Test, V5RomReadsWaitOnlyWithRomWait)
{
    Create("PROFI");
    Poke(0x8000, {0x7E});
    _z80->hl = 0x0100;
    EXPECT_EQ(Run(0x8000, kPaper + 1, 1), (std::vector<uint32_t>{7})) << "M1 at an odd T, the ROM read: no wait";
    _context->config.profi_rom_wait = 1;
    Reinstall();
    EXPECT_EQ(Run(0x8000, kPaper + 1, 1), (std::vector<uint32_t>{8})) << "RomWait=1: the one-shot adds 1";
}

TEST_F(ProfiWaitOverlay_Test, V5PhaseOneHasNoWaitsAndPhaseTwoWaitsOnTheOddT)
{
    Create("PROFI");
    Poke(0x8000, {0x00, 0x00});
    _context->config.profi_wait_phase = 1;
    Reinstall();
    EXPECT_EQ(Run(0x8000, kPaper, 2), (std::vector<uint32_t>{4, 4}));
    _context->config.profi_wait_phase = 2;
    Reinstall();
    EXPECT_EQ(Run(0x8000, kPaper, 1), (std::vector<uint32_t>{4})) << "even T: free";
    EXPECT_EQ(Run(0x8000, kPaper + 1, 1), (std::vector<uint32_t>{5})) << "odd T: waits";
    EXPECT_EQ(Run(0x8000, kPaper - 1, 1), (std::vector<uint32_t>{5})) << "one T before the window";
}

TEST_F(ProfiWaitOverlay_Test, V5HiResAndTheContentionSwitchTurnTheWaitOff)
{
    Create("PROFI");
    Poke(0x8000, {0x00});
    _context->emulatorState.pDFFD |= 0x80;
    EXPECT_EQ(Run(0x8000, kPaper, 1), (std::vector<uint32_t>{4})) << "DS80: not modeled, no waits";
    _context->emulatorState.pDFFD &= ~0x80;
    _core->SetContentionSwitch(false);
    EXPECT_EQ(Run(0x8000, kPaper, 1), (std::vector<uint32_t>{4})) << "the contention feature off";
    _core->SetContentionSwitch(true);
}

/// endregion </v5: the video WAIT at 3.5 MHz>

/// region <v3: the TURBO switch and its waits>

TEST_F(ProfiWaitOverlay_Test, V3HasNoWaitsAt35MHz)
{
    Create("PROFI3");
    EXPECT_FALSE(_decoder->AreWaitsInstalled());
    Poke(0x8000, {0x00, 0x00});
    EXPECT_EQ(Run(0x8000, kPaper, 2), (std::vector<uint32_t>{4, 4}));
}

TEST_F(ProfiWaitOverlay_Test, TurboSwitchRunsTheV3At7MHzWithItsSlotWaits)
{
    Create("PROFI3");
    EXPECT_EQ(_emulator->GetFrontPanelSwitch(FrontPanelSwitch::Turbo), 0);
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    EXPECT_EQ(_emulator->GetFrontPanelSwitch(FrontPanelSwitch::Turbo), 1);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 2);
    EXPECT_TRUE(_decoder->AreWaitsInstalled());

    // A RAM NOP: 4 clocks + 2 waits from an even clock, + 3 from an odd one; paper and border alike
    Poke(0x8000, {0x00, 0x00, 0x00});
    EXPECT_EQ(Run(0x8000, 2 * kPaper, 3), (std::vector<uint32_t>{6, 6, 6}));
    EXPECT_EQ(Run(0x8000, 2 * kBorder + 1, 3), (std::vector<uint32_t>{7, 6, 6}));

    // LD A,(HL) from RAM: M1 from an odd clock 4 + 3, the read from an even one 3 + 2 (A5: 12 in steady state)
    Poke(0x9000, {0x7E, 0x7E});
    _z80->hl = 0xC000;
    EXPECT_EQ(Run(0x9000, 2 * kPaper + 1, 2), (std::vector<uint32_t>{12, 12}));
    // The same read from ROM does not wait
    _z80->hl = 0x0100;
    EXPECT_EQ(Run(0x9000, 2 * kPaper, 1), (std::vector<uint32_t>{6 + 3}));

    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, false));
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 1);
    EXPECT_FALSE(_decoder->AreWaitsInstalled());
}

/// research-profi-v3-turbo-floatbus.md A6: an INC DE : JP loop in RAM runs 26 clocks per pass in turbo against 16
/// T at 3.5 MHz - 1.23x, the ratio of the Tact Meter reading on a real v3.2 (88208 / 71680)
TEST_F(ProfiWaitOverlay_Test, V3TurboRamLoopMatchesTheMeasuredRatio)
{
    Create("PROFI3");
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    Poke(0x8000, {0x13, 0xC3, 0x00, 0x80});   // INC DE : JP #8000
    std::vector<uint32_t> clocks = Run(0x8000, 2 * kBorder, 20);
    uint32_t pass = 0;
    for (size_t i = clocks.size() - 2; i < clocks.size(); i++)
        pass += clocks[i];
    EXPECT_EQ(pass, 26u);
}

/// The VG93's HLD pin drives the v3 board's /TURBO: a loaded head holds 3.5 MHz while the switch stays pressed
TEST_F(ProfiWaitOverlay_Test, V3HeadLoadHoldsTheBaseClock)
{
    Create("PROFI3");
    ASSERT_NE(_context->pBetaDisk, nullptr);
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 2);

    // Restore with h = 1 loads the head
    _context->emulatorState.flags |= CF_TRDOS;
    _context->pMemory->UpdateZ80Banks();
    _decoder->DecodePortOut(0x001F, 0x08, 0x0000);
    for (int i = 0; i < 2000 && !_context->pBetaDisk->IsHeadLoaded(); i++)
        _z80->Z80Step();
    if (!_context->pBetaDisk->IsHeadLoaded())
        GTEST_SKIP() << "the controller did not load the head without a drive";
    _decoder->OnMachineStep(_z80->t);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1) << "HLD high: 3.5 MHz";
}

TEST_F(ProfiWaitOverlay_Test, V5TurboSwitchToo)
{
    Create("PROFI");
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 2);
    // A RAM NOP: 1 wait in the border, 2 in the paper (the approximation in profiwaitoverlay.h)
    Poke(0x8000, {0x00});
    EXPECT_EQ(Run(0x8000, 2 * kBorder, 1), (std::vector<uint32_t>{5}));
    EXPECT_EQ(Run(0x8000, 2 * kPaper, 1), (std::vector<uint32_t>{6}));
}

/// endregion </v3: the TURBO switch and its waits>

/// region <Hi-res waits (design-hires.md H3)>

/// v5 at 5 MHz (ZQ3 20 MHz): a RAM access waits 1 T when its T1 starts 130 ns before to 40 ns after a video request
/// edge. The first request of paper line 0 falls at CPU clock 15387.6 (200 ns clocks): T1 at 15387 (-124 ns) waits,
/// 15386 (-324 ns) and 15388 (+76 ns) do not
TEST_F(ProfiWaitOverlay_Test, V5HiresWaitsAroundAVideoRequest)
{
    Create("PROFI");
    _decoder->DecodePortOut(0xDFFD, 0x80, 0x0000);
    ASSERT_EQ(_context->emulatorState.current_z80_frequency, 5'000'000u);
    ASSERT_TRUE(_decoder->AreWaitsInstalled());
    Poke(0x8000, {0x00});
    EXPECT_EQ(Run(0x8000, 15387, 1), (std::vector<uint32_t>{5}));
    EXPECT_EQ(Run(0x8000, 15386, 1), (std::vector<uint32_t>{4}));
    EXPECT_EQ(Run(0x8000, 15388, 1), (std::vector<uint32_t>{4}));
    EXPECT_EQ(Run(0x8000, 1000, 1), (std::vector<uint32_t>{4})) << "top border: no requests";
}

/// #7FFD bit 5 keeps the requests running all line long in hi-res: the same edge phase reaches the border
TEST_F(ProfiWaitOverlay_Test, V5HiresBcmrRunsTheRequestsEverywhere)
{
    Create("PROFI");
    _decoder->DecodePortOut(0xDFFD, 0x80, 0x0000);
    Poke(0x8000, {0x00});
    // 1000 requests before line 0's first one: 2 410 857 ns = CPU clock 12054.3; T1 at 12054 is 57 ns before it
    EXPECT_EQ(Run(0x8000, 12054, 1), (std::vector<uint32_t>{4})) << "outside the window without BCMR";
    _context->emulatorState.p7FFD |= 0x20;
    EXPECT_EQ(Run(0x8000, 12054, 1), (std::vector<uint32_t>{5}));
    _context->emulatorState.p7FFD &= ~0x20;
}

/// v5 hi-res turbo (10 MHz): every RAM access 1 T, 3 when T1 starts up to 90 ns before a request, 2 up to 90 ns after
TEST_F(ProfiWaitOverlay_Test, V5HiresTurboWaitsOneToThree)
{
    Create("PROFI");
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    _decoder->DecodePortOut(0xDFFD, 0x80, 0x0000);
    ASSERT_EQ(_context->emulatorState.current_z80_frequency, 10'000'000u);
    Poke(0x8000, {0x00});
    EXPECT_EQ(Run(0x8000, 30775, 1), (std::vector<uint32_t>{7})) << "-24 ns: 3";
    EXPECT_EQ(Run(0x8000, 30776, 1), (std::vector<uint32_t>{6})) << "+76 ns: 2";
    EXPECT_EQ(Run(0x8000, 30780, 1), (std::vector<uint32_t>{5})) << "+476 ns: 1";
}

/// v3 hi-res: no waits at 3 MHz; in turbo (6 MHz) the Spectrum-mode slot rule, 2 from an even clock, 3 from an odd
TEST_F(ProfiWaitOverlay_Test, V3HiresWaitsOnlyInTurbo)
{
    Create("PROFI3");
    _decoder->DecodePortOut(0xDFFD, 0x80, 0x0000);
    ASSERT_EQ(_context->emulatorState.current_z80_frequency, 3'000'000u);
    Poke(0x8000, {0x00});
    EXPECT_EQ(Run(0x8000, 15000, 1), (std::vector<uint32_t>{4}));
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    ASSERT_EQ(_context->emulatorState.current_z80_frequency, 6'000'000u);
    EXPECT_EQ(Run(0x8000, 30000, 1), (std::vector<uint32_t>{6}));
    EXPECT_EQ(Run(0x8000, 30001, 1), (std::vector<uint32_t>{7}));
}

/// endregion </Hi-res waits>

/// A machine without a TURBO switch answers -1 and refuses to set it
TEST(ProfiFrontPanelSwitch_Test, MachinesWithoutTheSwitchSayNo)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EXPECT_EQ(emulator->GetFrontPanelSwitch(FrontPanelSwitch::Turbo), -1);
    EXPECT_FALSE(emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    EmulatorTestHelper::CleanupEmulator(emulator);
}
