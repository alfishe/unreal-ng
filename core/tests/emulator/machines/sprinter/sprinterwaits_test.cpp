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
        _z80->Z80Step();
    EXPECT_EQ(_z80->t - start, 3u * 13) << "fast RAM: no waits at 21 MHz";
}
