#include "stdafx.h"
#include "pch.h"

#include "portdecoder_atm710_test.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"

/// ATM Turbo 2+ v7.10 clock select.
///
/// Contract shared with every hardware turbo (see ScorpionTurbo_Test):
///     current_z80_frequency_multiplier = next_z80_frequency_multiplier << hw_turbo_shift
/// next_ is the HOST speed control; hw_turbo_shift is the guest's hardware
/// clock. A model decoder owns hw_turbo_shift ONLY - writing next_ as well both
/// double-counts the clock and discards the user's speed setting.
///
/// The decoder half is tested without a CPU on purpose: on a running machine
/// the ATM BIOS owns #FF77 and reprograms it every frame, so port writes issued
/// from a test are overwritten before they can be observed.
class AtmTurboDecoder_Test : public PortDecoder_ATM710_Test
{
protected:
    /// The xx77 port address IS the aFF77 latch (Port_FF77_Out: aFF77 = port)
    /// and bit 9 is CPM. A bare 0x0077 keeps CPM clear, which is the SYSEN
    /// "ports continuously accessible" state the write gate needs
    void WriteFF77(uint8_t value) { _portDecoder->DecodePortOut(0x0077, value, 0x0000); }
    void WriteEFF7(uint8_t value) { _portDecoder->DecodePortOut(0xEFF7, value, 0x0000); }

    static constexpr uint8_t FF77_TURBO = 0x08;
    static constexpr uint8_t EFF7_PENTEVO_3_5 = 0x10;
};

/// @brief pFF77 bit 3 alone selects the clock, two states only
///        (Xpeccy atm2.c atm2Out77: compSetHwTurbo(comp, (val & 0x08) ? 2 : 1))
TEST_F(AtmTurboDecoder_Test, Ff77Bit3SelectsTheHardwareClock)
{
    EmulatorState& state = _context->emulatorState;

    WriteFF77(0x00);
    EXPECT_EQ(state.hw_turbo_shift, 0) << "bit 3 clear is 3.5 MHz";

    WriteFF77(FF77_TURBO);
    EXPECT_EQ(state.hw_turbo_shift, 1) << "bit 3 set is 7 MHz";

    WriteFF77(0x00);
    EXPECT_EQ(state.hw_turbo_shift, 0) << "turbo off must reach the clock";
}

/// @brief The decoder must not touch the host speed control. This is what made
///        the BIOS turbo toggle look inert: writing both fields composed
///        next << shift, so 7 MHz became 4x and the host setting was lost
TEST_F(AtmTurboDecoder_Test, DecoderLeavesHostSpeedMultiplierAlone)
{
    EmulatorState& state = _context->emulatorState;
    state.next_z80_frequency_multiplier = 3;   // whatever the host asked for

    WriteFF77(FF77_TURBO);
    EXPECT_EQ(state.next_z80_frequency_multiplier, 3) << "host intent must survive turbo on";

    WriteFF77(0x00);
    EXPECT_EQ(state.next_z80_frequency_multiplier, 3) << "host intent must survive turbo off";
}

/// @brief Regression guard: the three-way 14 / 7 / 3.5 select keyed on pEFF7
///        bit 4 is ZX Evo baseconf / Pentevo behaviour (Xpeccy pentevo.c
///        evoOut77d). ATM 7.10 has no 14 MHz mode and no clock bit in #EFF7 -
///        it used to inherit both
TEST_F(AtmTurboDecoder_Test, Eff7CarriesNoClockBit)
{
    EmulatorState& state = _context->emulatorState;

    WriteFF77(FF77_TURBO);
    ASSERT_EQ(state.hw_turbo_shift, 1);

    WriteEFF7(EFF7_PENTEVO_3_5);
    EXPECT_EQ(state.hw_turbo_shift, 1) << "#EFF7 must not drop the ATM 7.10 clock";

    WriteEFF7(0x00);
    EXPECT_EQ(state.hw_turbo_shift, 1) << "#EFF7 must not raise it either";
}

/// @brief There is no 14 MHz on ATM 7.10: no combination of the two latches may
///        produce a shift above 1
TEST_F(AtmTurboDecoder_Test, NoCombinationReachesFourteenMegahertz)
{
    EmulatorState& state = _context->emulatorState;

    for (uint8_t eff7 : {uint8_t(0x00), EFF7_PENTEVO_3_5})
    {
        WriteEFF7(eff7);
        for (uint8_t ff77 : {uint8_t(0x00), FF77_TURBO})
        {
            WriteFF77(ff77);
            EXPECT_LE(state.hw_turbo_shift, 1)
                << "pFF77=0x" << std::hex << int(ff77) << " pEFF7=0x" << int(eff7);
        }
    }
}

/// Composition is an Emulator-layer concern: the queued multiplier is applied by
/// Z80FrameCycle's prologue. Driven exactly like ScorpionTurbo_Test - state is
/// set directly rather than through ports, because the running BIOS owns #FF77
class AtmTurboClock_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        _emulator = _manager->CreateEmulatorWithModel("", "ATM710", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator) << "ATM710 instance could not be created";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);

        _emulator->EnableTurboMode();   // host-side only: mutes audio, skips rendering
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _manager->RemoveEmulator(_emulator->GetId());
            _emulator.reset();
        }
        _context = nullptr;
    }

    void CrossFrameBoundary() { _context->pCore->CPUFrameCycle(); }
};

TEST_F(AtmTurboClock_Test, HardwareShiftReachesTheCpuAndTheReportedFrequency)
{
    EmulatorState& state = _context->emulatorState;
    state.next_z80_frequency_multiplier = 1;   // host at 1x

    state.hw_turbo_shift = 1;                  // what the decoder sets for 7 MHz
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 2);
    EXPECT_EQ(state.current_z80_frequency, state.base_z80_frequency * 2) << "7 MHz reporting";

    state.hw_turbo_shift = 0;                  // BIOS turbo off
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1);
    EXPECT_EQ(state.current_z80_frequency, state.base_z80_frequency) << "3.5 MHz reporting";
}

TEST_F(AtmTurboClock_Test, HardwareClockComposesWithHostSpeedMultiplier)
{
    EmulatorState& state = _context->emulatorState;
    state.next_z80_frequency_multiplier = 4;   // host asks for 4x

    state.hw_turbo_shift = 1;
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 8) << "host 4x x hardware 2x";

    state.hw_turbo_shift = 0;
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 4) << "turbo off returns to the host setting";
    EXPECT_EQ(state.next_z80_frequency_multiplier, 4) << "host intent preserved across toggles";
}
