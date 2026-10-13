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
    EXPECT_GT(render(), 300) << "the third AY is audible";
    _ports->DecodePortOut(0xFFFD, 0x9D, 0);  // chip 2: both sides off (bits 6:5 = 00)
    render();  // the high-pass settles after the DC step of the pan change
    render();
    EXPECT_LT(render(), 60) << "panned out";
    // the DAC alone: A = #FF, others #80 -> left level
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);
    _ports->DecodePortOut(0x001F, 0xFF, 0);
    EXPECT_GT(render(), 1500) << "DAC A at #FF: (255 - 128) x 16 = 2032 on the left";
}

TEST_F(NextAudio_Test, BeeperRunsAtTheNextLevelsAndLeavesTheMixWhenTheSpeakerIsExcluded)
{
    Beeper& beeper = _context->pSoundManager->getBeeper();
    EXPECT_EQ(beeper.levelFor(0x10) - beeper.levelFor(0x00), 2048) << "EAR";
    EXPECT_EQ(beeper.levelFor(0x08) - beeper.levelFor(0x00), 512) << "MIC";
    _ports->Board().Write(0x06, 0x40);  // NR #06 bit 6 with NR #08 bit 4: the beeper only goes to the speaker
    _ports->Board().Write(0x08, 0x1A);
    EXPECT_EQ(beeper.levelFor(0x10) - beeper.levelFor(0x00), 0);
}

// zxnext.vhd 6379-6392: aymode_i is NR #06 bit 0 (modes 00 and 10 are the YM, 01 and 11 the AY); mode 11 holds all three chips in
// reset (audio_ay_reset): the selected chip, the pans and the registers are the reset values and nothing a program writes stays
TEST_F(NextAudio_Test, NrSixModeThreeHoldsTheChipsInResetAndBitZeroPicksTheChipType)
{
    NextAudio& audio = _ports->Audio();
    NextBoard& board = _ports->Board();
    board.Write(0x06, 0xA0);
    EXPECT_TRUE(audio.YmMode());
    board.Write(0x06, 0xA1);
    EXPECT_FALSE(audio.YmMode());
    board.Write(0x06, 0xA2);
    EXPECT_TRUE(audio.YmMode()) << "mode 10: bit 0 is 0";

    board.Write(0x06, 0xA0);
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);  // chip 2
    AyWrite(8, 0x0F);
    ASSERT_EQ(AyRead(8), 0x0F);

    board.Write(0x06, 0xA3);
    EXPECT_TRUE(audio.Held());
    EXPECT_FALSE(audio.YmMode()) << "bit 0 is 1";
    EXPECT_EQ(audio.SelectedChip(), 0u);
    AyWrite(8, 0x05);
    EXPECT_EQ(audio.Chip(0)->readRegister(8), 0x00) << "a write in reset is lost";

    board.Write(0x06, 0xA0);
    EXPECT_FALSE(audio.Held());
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);
    EXPECT_EQ(AyRead(8), 0x00) << "chip 2 was reset too";
    EXPECT_EQ(AyRead(7), 0xFF) << "ym2149.vhd 109: reg(7) <= x\"ff\"";
}

// turbosound.vhd 128-136 `reset_i`, soundrive.vhd: a soft or a hard reset puts chip 0 up, both pan bits on, the registers and DACs at
// their reset values
TEST_F(NextAudio_Test, AResetRestoresTheChipSelectThePansTheRegistersAndTheDacs)
{
    NextAudio& audio = _ports->Audio();
    _ports->DecodePortOut(0xFFFD, 0x9F, 0);  // chip 0, no pan bits
    EXPECT_EQ(audio.PanMask(0), 0);
    _ports->DecodePortOut(0xFFFD, 0xFE, 0);  // chip 1
    AyWrite(8, 0x0A);
    _ports->DecodePortOut(0x001F, 0x10, 0);
    ASSERT_EQ(audio.Dac(0), 0x10);

    _ports->PerformReset(false);
    EXPECT_EQ(audio.PanMask(0), 3);
    EXPECT_EQ(audio.SelectedChip(), 0u);
    EXPECT_EQ(audio.Chip(1)->readRegister(8), 0x00);
    EXPECT_EQ(audio.Dac(0), 0x80);
}

// turbosound.vhd 168-172, 270-281: turbosound_en does not move ay_select; only the chip that is selected sounds while it is off
TEST_F(NextAudio_Test, TurningTurbosoundOffKeepsTheChipThatWasChosen)
{
    _ports->DecodePortOut(0xFFFD, 0xFD, 0);
    ASSERT_EQ(_ports->Audio().SelectedChip(), 2u);
    _ports->Board().Write(0x08, 0x08);
    EXPECT_EQ(_ports->Audio().SelectedChip(), 2u);
    AyWrite(8, 0x04);
    EXPECT_EQ(_ports->Audio().Chip(2)->readRegister(8), 0x04) << "its registers are the ones written";
    EXPECT_EQ(_ports->Audio().Chip(0)->readRegister(8), 0x00);
}

// turbosound.vhd 190: psg_addr needs bits 7:5 = 000; ym2149.vhd 123 / 159: a register address above #0F is not writable and a YM
// answers #FF to it (an AY mirrors the low four bits)
TEST_F(NextAudio_Test, ARegisterAddressNeedsBitsSevenToFiveClearAndBitFourIsReadOnly)
{
    AyWrite(8, 0x0F);
    _ports->DecodePortOut(0xFFFD, 0x28, 0);  // bits 7:5 = 001: not an address, the selected register stays R8
    _ports->DecodePortOut(0xBFFD, 0x05, 0);
    EXPECT_EQ(AyRead(8), 0x05);

    _ports->DecodePortOut(0xFFFD, 0x18, 0);  // address #18: nothing is written there
    _ports->DecodePortOut(0xBFFD, 0x07, 0);
    EXPECT_EQ(_ports->DecodePortIn(0xFFFD, 0), 0xFF) << "YM";
    EXPECT_EQ(AyRead(8), 0x05);

    _ports->Board().Write(0x06, 0xA1);  // AY
    _ports->DecodePortOut(0xFFFD, 0x18, 0);
    EXPECT_EQ(_ports->DecodePortIn(0xFFFD, 0), 0x05) << "the AY answers with R8";
}

// soundrive.vhd: reset_i = `reset or not nr_08_dac_en`: the four levels sit at #80 while the DAC is off
TEST_F(NextAudio_Test, TheDacLevelsGoBackToMiddleWhenTheDacIsSwitchedOff)
{
    _ports->DecodePortOut(0x001F, 0x10, 0);
    ASSERT_EQ(_ports->Audio().Dac(0), 0x10);
    _ports->Board().Write(0x08, 0x02);
    EXPECT_EQ(_ports->Audio().Dac(0), 0x80);
}

// #BFF5 (zxnext.vhd 2649, 2771, 6395; ym2149.vhd 220): a read returns AY_ID & 0 & register address of the selected chip ("11", "10" and
// "01" for chips 0, 1, 2); #BFFD reads like #FFFD on +3 timing only; the AY ports need A2 = 1 and A1:A0 = 01 (port_fffd, port_bffd)
TEST(NextAudioPorts_Test, TheIdentityPortAndTheExactAyDecode)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    constexpr uint16_t kPc = 0x8000;
    ports->Board().WriteNextReg(0x08, 0x12);  // NR #08 bit 1: turbosound
    ports->DecodePortOut(0xFFFD, 0xFF, kPc);  // chip 0 (turbosound pattern: bits 7:5 = 111 ... select the chip)
    ports->DecodePortOut(0xFFFD, 0x07, kPc);  // register 7
    EXPECT_EQ(ports->DecodePortIn(0xBFF5, kPc), (3u << 6) | 7u) << "chip 0, register 7";
    ports->DecodePortOut(0xFFFD, 0xFE, kPc);  // chip 1
    ports->DecodePortOut(0xFFFD, 0x03, kPc);
    EXPECT_EQ(ports->DecodePortIn(0xBFF5, kPc), (2u << 6) | 3u) << "chip 1, register 3";
    ports->DecodePortOut(0xFFFD, 0xFD, kPc);  // chip 2
    ports->DecodePortOut(0xFFFD, 0x0A, kPc);
    EXPECT_EQ(ports->DecodePortIn(0xBFF5, kPc), (1u << 6) | 0x0Au) << "chip 2, register 10";
    // an address that is not xxFD with A2 = 1 does not select a register
    ports->DecodePortOut(0xFFFD, 0xFF, kPc);
    ports->DecodePortOut(0xFFFC, 0x05, kPc);  // A0 = 0: not the AY
    ports->DecodePortOut(0xFFFD, 0x01, kPc);
    EXPECT_EQ(ports->DecodePortIn(0xBFF5, kPc) & 0x1F, 0x01u);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
