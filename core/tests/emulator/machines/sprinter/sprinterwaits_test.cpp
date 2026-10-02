// SprinterWaits: the turbo wait rule and where it applies (Sprinter test-plan
// §2.2 T-MEM-10; technical-design §4).

#include "sprinterfixture.h"

#include "emulator/memory/sprinter/sprinterwaits.h"

class SprinterWaits_Test : public SprinterFixture
{
};

// T-MEM-10: extra clocks for t mod 6 = 0..5, memory (3 taken) and port (4 taken)
TEST_F(SprinterWaits_Test, Rule_PhaseTable)
{
    const uint32_t memory[6] = {3, 8, 7, 6, 5, 4};
    const uint32_t port[6] = {2, 7, 6, 5, 4, 3};
    for (uint32_t phase = 0; phase < 6; phase++)
    {
        EXPECT_EQ(SprinterWaits::Rule(600 + phase, SprinterWaits::kMemoryTaken), memory[phase]) << phase;
        EXPECT_EQ(SprinterWaits::Rule(600 + phase, SprinterWaits::kPortTaken), port[phase]) << phase;
    }
    // The worked example: t = 100 costs 5, t = 102 costs 3
    EXPECT_EQ(SprinterWaits::Rule(100, 3), 5u);
    EXPECT_EQ(SprinterWaits::Rule(102, 3), 3u);
}

// At 3.5 MHz nothing waits; at 21 MHz RAM waits, ROM and fast RAM do not
TEST_F(SprinterWaits_Test, Turbo_RamSlowerThanFastRam)
{
    OpenDcp();
    SetCode(0x007C, false, 0xC6);

    // LD A,(#8000) x 4 + RET-free straight code in window 2 (RAM)
    const std::vector<uint8_t> code = {0x3A, 0x00, 0x80, 0x3A, 0x00, 0x80, 0x3A, 0x00, 0x80};
    auto clocks = [&](uint16_t origin) {
        const uint32_t start = _z80->t;
        RunCode(code, origin);
        return _z80->t - start;
    };

    const uint32_t slow = clocks(0x8100);
    EXPECT_EQ(slow, 3u * 13) << "3.5 MHz: no waits";

    Out(0x007C, 0x03);  // turbo on
    ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 6);
    const uint32_t ramTurbo = clocks(0x8100);
    EXPECT_GT(ramTurbo, 3u * 13) << "21 MHz from RAM: every access waits";

    // The same code from fast RAM, reading fast RAM: no waits
    In(0x00FB);
    ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_CACHE);
    const std::vector<uint8_t> fastCode = {0x3A, 0x00, 0x30, 0x3A, 0x00, 0x30, 0x3A, 0x00, 0x30};
    for (size_t i = 0; i < fastCode.size(); i++)
        _sprinterMemory->FastRam()[0x0100 + i] = fastCode[i];
    _z80->pc = 0x0100;
    const uint32_t start = _z80->t;
    for (int i = 0; i < 3; i++)
        Step();
    EXPECT_EQ(_z80->t - start, 3u * 13) << "fast RAM: no waits at 21 MHz";
}

/// region <Original waits (ZX mode, ALL_MODE bit 2 = 0 at 3.5 MHz; tdd-zx-mode.md §3.3, T-ZX-9)>

// The rule: T2 = T1 + 1 sampled against the 4-T CT5 period (low for two T, high for two): 2, 1, 0, 0 by T2 mod 4
TEST_F(SprinterWaits_Test, OrigWaits_PhaseTable)
{
    static_assert(SprinterOrigWaits::kPhase == 0, "the table below is for the placeholder phase");
    const uint32_t expected[4] = {1, 0, 0, 2};  // by T1 mod 4: T2 mod 4 = 1, 2, 3, 0
    for (uint32_t start = 1000; start < 1008; start++)
        EXPECT_EQ(SprinterOrigWaits::Rule(start), expected[start % 4]) << start;
    // The worked example of the header: T1 = 1 000 waits 1 T, T1 = 1 003 waits 2 T; 0.75 T on average
    EXPECT_EQ(SprinterOrigWaits::Rule(1000), 1u);
    EXPECT_EQ(SprinterOrigWaits::Rule(1003), 2u);
    // 224 T per line and 71 680 / 69 888 T per frame are multiples of the period: the same phase on every line
    EXPECT_EQ(224u % SprinterOrigWaits::kPeriod, 0u);
    EXPECT_EQ(71680u % SprinterOrigWaits::kPeriod, 0u);
    EXPECT_EQ(69888u % SprinterOrigWaits::kPeriod, 0u);
}

// The windows of the PLD equation: #4000-#7FFF always, #C000-#FFFF while #7FFD bit 2 is set (Spectrum pages 4-7)
TEST_F(SprinterWaits_Test, OrigWaits_Windows)
{
    for (uint8_t pn : {0x00, 0x03, 0x04, 0x05, 0x07, 0x10, 0x14})
    {
        EXPECT_FALSE(SprinterOrigWaits::WindowWaits(0, pn)) << int(pn);
        EXPECT_TRUE(SprinterOrigWaits::WindowWaits(1, pn)) << int(pn);
        EXPECT_FALSE(SprinterOrigWaits::WindowWaits(2, pn)) << int(pn);
        EXPECT_EQ(SprinterOrigWaits::WindowWaits(3, pn), (pn & 0x04) != 0) << int(pn);
    }
}

namespace
{
constexpr uint16_t kAllModePort = 0x12C3;
constexpr uint16_t k7ffdPort = 0x12C1;
constexpr uint16_t kSysPort = 0x007C;
}  // namespace

// The overlay follows ALL_MODE bit 2 and the clock: on with bit 2 = 0 at 3.5 MHz, off with bit 2 = 1 or at
// 21 MHz; window 3 follows #7FFD bit 2
TEST_F(SprinterWaits_Test, OrigWaits_FollowAllModeTurboAnd7ffd)
{
    OpenDcp();
    SetCode(kAllModePort, false, 0xC3);
    SetCode(k7ffdPort, false, 0xC1);
    SetCode(kSysPort, false, 0xC6);

    Out(kAllModePort, 0xFF);
    EXPECT_FALSE(_decoder->OrigWaitsActive()) << "bit 2 = 1: no waits";
    Out(kAllModePort, 0xFA);  // ORIGIN.ZX
    EXPECT_TRUE(_decoder->OrigWaitsActive());
    ASSERT_NE(_decoder->GetOrigWaits(), nullptr);
    EXPECT_FALSE(_decoder->GetOrigWaits()->SlotWaits(0));
    EXPECT_TRUE(_decoder->GetOrigWaits()->SlotWaits(1));
    EXPECT_FALSE(_decoder->GetOrigWaits()->SlotWaits(2));

    Out(k7ffdPort, 0x05);
    EXPECT_TRUE(_decoder->GetOrigWaits()->SlotWaits(3)) << "#7FFD bit 2: page 5 in window 3";
    Out(k7ffdPort, 0x03);
    EXPECT_FALSE(_decoder->GetOrigWaits()->SlotWaits(3));

    Out(kSysPort, 0x03);  // turbo on
    ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 6);
    EXPECT_FALSE(_decoder->OrigWaitsActive()) << "TURBO: no waits";
    Out(kSysPort, 0x02);  // turbo off
    EXPECT_TRUE(_decoder->OrigWaitsActive());
    Out(kAllModePort, 0xFE);  // the default ZX mode (SP.ZX, P128.ZX)
    EXPECT_FALSE(_decoder->OrigWaitsActive());
}

// LD A,(nn) from window 2: a read of #4000 waits by the rule, a read of #8000 never; with ALL_MODE bit 2 = 1 or in
// turbo the same code takes its plain 13 T
TEST_F(SprinterWaits_Test, OrigWaits_ScreenReadsWait)
{
    OpenDcp();
    SetCode(kAllModePort, false, 0xC3);
    SetCode(kSysPort, false, 0xC6);

    // LD A,(addr) at #8100: M1 4 T, two operand reads 3 T each, the data read starts 10 T after the instruction
    auto run = [&](uint16_t addr, uint32_t& expectedWaits) {
        const std::vector<uint8_t> code = {0x3A, static_cast<uint8_t>(addr), static_cast<uint8_t>(addr >> 8)};
        for (size_t i = 0; i < code.size(); i++)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), code[i]);
        uint32_t total = 0;
        expectedWaits = 0;
        for (int i = 0; i < 8; i++)
        {
            _z80->pc = 0x8100;
            const uint32_t start = _z80->t;
            if (_decoder->OrigWaitsActive() && addr >= 0x4000 && addr < 0x8000)
                expectedWaits += SprinterOrigWaits::Rule(start + 10);
            Step();
            total += _z80->t - start;
        }
        return total;
    };

    uint32_t waits = 0;
    Out(kAllModePort, 0xFA);
    ASSERT_TRUE(_decoder->OrigWaitsActive());
    const uint32_t screen = run(0x4000, waits);
    EXPECT_EQ(screen, 8u * 13 + waits);
    EXPECT_GT(waits, 0u) << "eight reads at successive phases hit a CT5-low T";
    EXPECT_EQ(run(0x8000, waits), 8u * 13) << "window 2: no waits";

    Out(kAllModePort, 0xFE);
    EXPECT_EQ(run(0x4000, waits), 8u * 13) << "ALL_MODE bit 2 = 1: no waits";

    Out(kAllModePort, 0xFA);
    Out(kSysPort, 0x03);  // 21 MHz: the turbo waits apply instead, the original ones do not
    ASSERT_FALSE(_decoder->OrigWaitsActive());
}

/// endregion </Original waits>
