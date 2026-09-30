// Scorpion ZS-256 Turbo+ wait states at 7 MHz (scorpionturbooverlay.h), the SC15.1 firmware. The expected
// durations are the worked examples of docs/inprogress/2026-09-29-machine-waits/research-scorpion-turbo.md
// 4.3, simulated from the logic chip's equations. All clocks here are 7 MHz T-states (Z80::t counts the
// current clock).

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

namespace
{
class ScorpionTurboOverlay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Scorpion256* _decoder = nullptr;

    // The fetch window's first clock: 2 x (INT at 1816 + 14336) + 1, the pixel counter's phase 0
    static constexpr uint32_t kPicture = 2 * 16152 + 1;
    // The same line, 300 clocks later: the right border (the window is 256 clocks of each 448), phase 0
    static constexpr uint32_t kBorder = kPicture + 300;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("SCORPION", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _core = _context->pCore;
        _z80 = _core->GetZ80();
        _decoder = dynamic_cast<PortDecoder_Scorpion256*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _z80->iff1 = 0;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// The turbo flip-flop: an IN from the #7FFD decode sets it, from #1FFD clears it
    void Turbo(bool on) { _decoder->DecodePortIn(on ? 0x7FFD : 0x1FFD, 0x0000); }

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
};
}  // namespace

TEST_F(ScorpionTurboOverlay_Test, TurboInstallsTheOverlay)
{
    EXPECT_FALSE(_decoder->AreTurboWaitsInstalled());
    Turbo(true);
    EXPECT_TRUE(_decoder->AreTurboWaitsInstalled());
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 2);
    Turbo(false);
    EXPECT_FALSE(_decoder->AreTurboWaitsInstalled());
    EXPECT_EQ(_core->GetBusOverlayCount(), 0u);
}

/// 4.3: a RAM NOP takes 8 in the picture (one CPU slot every 4 clocks, plus the forced M1 wait), 6 in the border
TEST_F(ScorpionTurboOverlay_Test, NopStreamTakesEightInThePictureAndSixInTheBorder)
{
    Turbo(true);
    Poke(0x8000, {0x00, 0x00, 0x00, 0x00});

    EXPECT_EQ(Run(0x8000, kPicture, 4), (std::vector<uint32_t>{8, 8, 8, 8}));
    EXPECT_EQ(Run(0x8000, kBorder, 4), (std::vector<uint32_t>{6, 6, 6, 6}));
}

/// 4.3: `LD A,(HL)` and `LD (HL),A` (no write buffer) wait alike: 14 first from phase 0, then 12; 10 in the border
TEST_F(ScorpionTurboOverlay_Test, RamReadsAndWritesWaitForTheSlot)
{
    Turbo(true);
    Poke(0x8000, {0x7E, 0x7E, 0x7E});
    Poke(0x9000, {0x77, 0x77, 0x77});
    _z80->hl = 0xC000;

    EXPECT_EQ(Run(0x8000, kPicture, 3), (std::vector<uint32_t>{14, 12, 12})) << "LD A,(HL)";
    EXPECT_EQ(Run(0x9000, kPicture, 3), (std::vector<uint32_t>{14, 12, 12})) << "LD (HL),A";
    EXPECT_EQ(Run(0x8000, kBorder, 3), (std::vector<uint32_t>{10, 10, 10})) << "LD A,(HL) in the border";
}

TEST_F(ScorpionTurboOverlay_Test, RomDoesNotWait)
{
    Turbo(true);
    Poke(0x8000, {0x7E});

    _z80->hl = 0x0100;
    EXPECT_EQ(Run(0x8000, kBorder, 1), (std::vector<uint32_t>{6 + 3})) << "M1 from RAM 6, the ROM read 3";
    _z80->hl = 0xC000;
    EXPECT_EQ(Run(0x8000, kBorder, 1), (std::vector<uint32_t>{6 + 4})) << "the same read from RAM waits 1";
}

/// 4.1: every I/O cycle takes 2 more; `OUT (#FE),A` in the border is 16 (4.3: 15-16)
TEST_F(ScorpionTurboOverlay_Test, IoTakesTwoMore)
{
    Turbo(true);
    Poke(0x8000, {0xD3, 0xFE});

    EXPECT_EQ(Run(0x8000, kBorder, 1), (std::vector<uint32_t>{6 + 4 + 6}));
}

TEST_F(ScorpionTurboOverlay_Test, NoWaitsAtNormalSpeedOrWithTheFeatureOff)
{
    Poke(0x8000, {0x00, 0x00, 0x00, 0xD3, 0xFE});

    // 3.5 MHz: the normal rules (Even M1 on an even start keeps NOPs at 4)
    EXPECT_EQ(Run(0x8000, 16152, 4), (std::vector<uint32_t>{4, 4, 4, 11}));

    Turbo(true);
    _core->SetContentionSwitch(false);
    EXPECT_EQ(Run(0x8000, kPicture, 4), (std::vector<uint32_t>{4, 4, 4, 11})) << "the contention feature off";
    _core->SetContentionSwitch(true);
    EXPECT_EQ(Run(0x8000, kPicture, 3), (std::vector<uint32_t>{8, 8, 8}));
}
