// NextAudio (core/src/emulator/sound/nextaudio.h): three AY chips with chip select and pan through #FFFD, per-chip
// registers, the DAC ports and their NextREG mirrors, and a rendered frame. Sources: research-fpga-vhdl.md section 18,
// zxnext.vhd 2429-2435 / 2658-2664 (DAC ports), ZX Spectrum Next wiki (Turbosound Next).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextAudio_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    PortDecoder_Next* _ports = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _ports->Board().Write(0x08, 0x02 | 0x08);  // turbosound and the DAC on
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }
    void AyWrite(uint8_t reg, uint8_t value)
    {
        _ports->DecodePortOut(0xFFFD, reg, 0);
        _ports->DecodePortOut(0xBFFD, value, 0);
    }
    uint8_t AyRead(uint8_t reg)
    {
        _ports->DecodePortOut(0xFFFD, reg, 0);
        return _ports->DecodePortIn(0xFFFD, 0);
    }
};

TEST_F(NextAudio_Test, ChipSelectKeepsThreeIndependentRegisterFiles)
{
    _ports->DecodePortOut(0xFFFD, 0xFF, 0);  // chip 0 (11)
    AyWrite(0, 0x11);
    _ports->DecodePortOut(0xFFFD, 0xFE, 0);  // chip 1 (10)
    AyWrite(0, 0x22);
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);  // chip 2 (01)
    AyWrite(0, 0x33);
    EXPECT_EQ(_ports->Audio().SelectedChip(), 2u);
    EXPECT_EQ(AyRead(0), 0x33);
    _ports->DecodePortOut(0xFFFD, 0xFE, 0);
    EXPECT_EQ(AyRead(0), 0x22);
    _ports->DecodePortOut(0xFFFD, 0xFF, 0);
    EXPECT_EQ(AyRead(0), 0x11);
}

TEST_F(NextAudio_Test, SelectWithoutTurbosoundLeavesChipZero)
{
    _ports->Board().Write(0x08, 0x08);  // turbosound off
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);
    EXPECT_EQ(_ports->Audio().SelectedChip(), 0u);
}

TEST_F(NextAudio_Test, PanBitsOfTheSelectByteAreKeptPerChip)
{
    _ports->DecodePortOut(0xFFFD, 0xBF, 0);  // chip 0: bit 6 = 0 (left off), bit 5 = 1 (right on)
    EXPECT_EQ(_ports->Audio().PanMask(0), 1);
    _ports->DecodePortOut(0xFFFD, 0xDE, 0);  // chip 1: left on, right off
    EXPECT_EQ(_ports->Audio().PanMask(1), 2);
}

TEST_F(NextAudio_Test, DacPortsOfTheTwoSoundDriveSetsAndTheMonoPorts)
{
    NextAudio& audio = _ports->Audio();
    _ports->DecodePortOut(0x001F, 0x10, 0);  // mode 1: A
    _ports->DecodePortOut(0x000F, 0x20, 0);  // B
    _ports->DecodePortOut(0x004F, 0x30, 0);  // C
    _ports->DecodePortOut(0x005F, 0x40, 0);  // D
    EXPECT_EQ(audio.Dac(0), 0x10);
    EXPECT_EQ(audio.Dac(1), 0x20);
    EXPECT_EQ(audio.Dac(2), 0x30);
    EXPECT_EQ(audio.Dac(3), 0x40);
    _ports->DecodePortOut(0x00F1, 0x51, 0);  // mode 2: A B C D = F1 F3 F9 FB
    _ports->DecodePortOut(0x00F3, 0x52, 0);
    _ports->DecodePortOut(0x00F9, 0x53, 0);
    _ports->DecodePortOut(0x00FB, 0x54, 0);
    EXPECT_EQ(audio.Dac(0), 0x51);
    EXPECT_EQ(audio.Dac(1), 0x52);
    EXPECT_EQ(audio.Dac(2), 0x53);
    EXPECT_EQ(audio.Dac(3), 0x54);
    _ports->DecodePortOut(0x00DF, 0x66, 0);  // the SpecDrum mono port: A and D
    EXPECT_EQ(audio.Dac(0), 0x66);
    EXPECT_EQ(audio.Dac(3), 0x66);
    _ports->DecodePortOut(0x00B3, 0x77, 0);  // GS Covox mono: B and C
    EXPECT_EQ(audio.Dac(1), 0x77);
    EXPECT_EQ(audio.Dac(2), 0x77);
}

TEST_F(NextAudio_Test, NextRegMirrorsOfTheDacAndTheEnableRegister)
{
    NextAudio& audio = _ports->Audio();
    _ports->Board().Write(0x2C, 0x91);  // B
    _ports->Board().Write(0x2D, 0x92);  // A and D
    _ports->Board().Write(0x2E, 0x93);  // C
    EXPECT_EQ(audio.Dac(1), 0x91);
    EXPECT_EQ(audio.Dac(0), 0x92);
    EXPECT_EQ(audio.Dac(3), 0x92);
    EXPECT_EQ(audio.Dac(2), 0x93);
    _ports->Board().Write(0x84, 0x00);  // every DAC port decode off
    _ports->DecodePortOut(0x001F, 0x01, 0);
    EXPECT_EQ(audio.Dac(0), 0x92) << "the port is not decoded any more";
}

TEST_F(NextAudio_Test, AFrameRendersTheThirdChipAndTheDacAndHonoursThePan)
{
    NextAudio& audio = _ports->Audio();
    double now = 0;
    audio.SetTimeSource([&]() { return now; });
    // chip 2: tone on channel A at full volume
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);
    AyWrite(0, 0x20);  // period 0x20
    AyWrite(7, 0x3E);  // tone A on
    AyWrite(8, 0x0F);
    auto render = [&]() {
        audio.AudioFrameStart(false);
        now += _context->config.frame;
        audio.AudioFrameEnd(882);
        int peak = 0;
        const int16_t* buffer = audio.AudioBuffer();
        for (int i = 0; i < 882 * 2; i++)
            peak = std::max(peak, std::abs(static_cast<int>(buffer[i])));
        return peak;
    };
    render();  // the first frame anchors the time
    EXPECT_GT(render(), 1000) << "the third AY is audible";
    _ports->DecodePortOut(0xFFFD, 0x9D, 0);  // chip 2: both sides off (bits 6:5 = 00)
    EXPECT_EQ(render(), 0) << "panned out";
    // the DAC alone: A = #FF, others #80 -> left level
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);
    _ports->DecodePortOut(0x001F, 0xFF, 0);
    EXPECT_GT(render(), 1000);
}
