// TS-Conf sound (TSConf implementation-plan phase 7 SND-1...3; hardware-spec
// §7, [V] sound/sound.v): the AY decode, the board's one 8-bit sound DAC that
// #FB and the #FE beeper bit share, General Sound on the ZX-Bus.

#include <gtest/gtest.h>

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/ports/models/portdecoder_tsconf.h>
#include <emulator/sound/covox.h>
#include <emulator/sound/soundmanager.h>

#include "pch.h"
#include "stdafx.h"

class TsConfSound_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_TSConf* _decoder = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("tsconf-sound", "TSL", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0); }
    uint8_t In(uint16_t port) { return _decoder->DecodePortIn(port, 0); }

    std::array<uint8_t, 4> Dac()
    {
        uint8_t latches[4] = {};
        _context->pSoundManager->getCovox()->getDacLatches(latches);
        return {latches[0], latches[1], latches[2], latches[3]};
    }
};

/// SND-2: one register - #FB writes the byte, #FE bit 4 writes #FF / #00, the
/// last write wins; a mono DAC (all four Covox latches)
TEST_F(TsConfSound_Test, SND2_SharedDac)
{
    ASSERT_TRUE(_context->pSoundManager->hasCovox()) << "the ts-conf config fits the DAC (CovoxFB=1)";
    Out(0x00FB, 0x40);
    EXPECT_EQ(Dac(), (std::array<uint8_t, 4>{0x40, 0x40, 0x40, 0x40}));
    Out(0x00FE, 0x10);
    EXPECT_EQ(Dac()[0], 0xFF) << "the beeper bit overwrites the byte";
    Out(0x12FB, 0x40);
    EXPECT_EQ(Dac()[0], 0x40) << "any #xxFB";
    Out(0x00FE, 0x07);
    EXPECT_EQ(Dac()[0], 0x00);
    EXPECT_EQ(_context->emulatorState.border_attr, 0x07) << "the border still follows #FE";
}

/// SND-1: the AY answers on #xxFD with A15 = 1 only (BC1 = A14); A15 = 0 is
/// #7FFD. (Register read-back through #FFFD is the TurboSound slot's business
/// on every model, not the decoder's)
TEST_F(TsConfSound_Test, SND1_AyDecode)
{
    using Arm = PortDecoder_TSConf::PortArm;
    EXPECT_EQ(_decoder->ClassifyPort(0xFFFD), Arm::Ay);
    EXPECT_EQ(_decoder->ClassifyPort(0xBFFD), Arm::Ay);
    EXPECT_EQ(_decoder->ClassifyPort(0xC0FD), Arm::Ay) << "only A15 and the low byte count";
    EXPECT_EQ(_decoder->ClassifyPort(0x7FFD), Arm::Paging7FFD);
    EXPECT_EQ(_decoder->ClassifyPort(0x3FFD), Arm::Paging7FFD);
}

/// SND-3: General Sound is a ZX-Bus card - the board leaves #B3 / #BB (and
/// the NeoGS #33) to the bus, where a fitted card answers
TEST_F(TsConfSound_Test, SND3_GeneralSoundPortsReachTheZxBus)
{
    using Arm = PortDecoder_TSConf::PortArm;
    for (uint16_t port : {0x00B3, 0x00BB, 0x0033})
        EXPECT_EQ(_decoder->ClassifyPort(port), Arm::ZxBus) << std::hex << port;
}
