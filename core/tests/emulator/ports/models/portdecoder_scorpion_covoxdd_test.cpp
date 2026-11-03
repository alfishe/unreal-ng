#include "stdafx.h"
#include "pch.h"

#include <vector>

#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/sound/covox.h"

/// The Scorpion printer Covox ([SOUND] CovoxDD=1, slot built-in covox-dd): an 8-bit DAC on the printer data latch
/// #DD, decoded as the board does (yellow board and GMX: A0 = 1, A1 = 0, A2 = 1, A5 = 0; Turbo+: no A2), a fifth
/// mono latch beside the SounDrive / #FB ones. VPlayer's "DD (Scorp.)" sound port writes it with OUT (C),A
class PortDecoder_Scorpion_CovoxDd_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Scorpion256* _portDecoder = nullptr;

    void Make(MEM_MODEL model, bool covoxDd, bool sd = false, bool covoxFb = false)
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.mem_model = model;
        _context->config.sound.covoxDD = covoxDd ? 1 : 0;
        _context->config.sound.sd = sd ? 1 : 0;
        _context->config.sound.covoxFB = covoxFb ? 1 : 0;
        _portDecoder = new PortDecoder_Scorpion256(_context);
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
        delete _context;
        _context = nullptr;
    }
};

TEST_F(PortDecoder_Scorpion_CovoxDd_Test, PrinterDecodePerModel)
{
    const PortMaskMatch yellow = Covox::PrinterDecodeOf(MM_SCORP);
    EXPECT_EQ(yellow.mask, 0x0027);
    EXPECT_EQ(yellow.match, 0x0005);
    EXPECT_EQ((0x00DD & yellow.mask), yellow.match);
    const PortMaskMatch gmx = Covox::PrinterDecodeOf(MM_GMX);
    EXPECT_EQ(gmx.mask, yellow.mask);
    EXPECT_EQ(gmx.match, yellow.match);
    const PortMaskMatch turbo = Covox::PrinterDecodeOf(MM_PROFSCORP);
    EXPECT_EQ(turbo.mask, 0x0023);
    EXPECT_EQ(turbo.match, 0x0001);
    EXPECT_EQ(Covox::PrinterDecodeOf(MM_PENTAGON).mask, 0) << "no printer Covox off the Scorpion family";

    // None of the board's other ports falls into the printer decode
    for (uint16_t port : { 0x7FFD, 0x1FFD, 0xFFFD, 0xBFFD, 0x00FE, 0x001F, 0x003F, 0x005F, 0x007F, 0x00FF, 0x00B3,
                           0x00BB, 0x0033, 0x00FB, 0xFFBA, 0xFFBE, 0x0057, 0x0077, 0xFADF })
    {
        EXPECT_NE((port & yellow.mask), yellow.match) << std::hex << port;
        EXPECT_NE((port & turbo.mask), turbo.match) << std::hex << port;
    }
}

TEST_F(PortDecoder_Scorpion_CovoxDd_Test, OutDdDrivesThePrinterDacAheadOfTheAyMasks)
{
    Make(MM_SCORP, true);
    Covox covox(_context);
    ASSERT_TRUE(covox.hasPrinterDac());
    EXPECT_EQ(covox.fitment(), Covox::Fitment::None);
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));
    const uint8_t ayRegister = _context->emulatorState.pFFFD;

    // OUT (C),A with B = 0 (VPlayer), then OUT (#DD),A with a sample of #C3 on A15-A8: the AY #FFFD mask
    // (A15 = A14 = 1, A1 = 0) would take it, the printer latch is decoded first
    _portDecoder->DecodePortOut(0x00DD, 0x40, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x40);
    _portDecoder->DecodePortOut(0xC3DD, 0xC3, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0xC3);
    EXPECT_EQ(_context->emulatorState.pFFFD, ayRegister) << "the AY register select must not see #DD";

    // Mono, centred: the same level on both sides
    EXPECT_EQ(covox.lastLeftAmplitude(), (0xC3 - 128) * 128);
    EXPECT_EQ(covox.lastRightAmplitude(), (0xC3 - 128) * 128);

    // A mirror of the partial decode (A2 = 1, A0 = 1, A1 = A5 = 0) reaches it; A2 = 0 does not on the yellow board
    _portDecoder->DecodePortOut(0x00C5, 0x11, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x11);
    _portDecoder->DecodePortOut(0x00D9, 0x22, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x11) << "#D9 (A2 = 0) is outside the yellow board's decode";

    // No SounDrive / #FB card: #FB is not the DAC's, and the printer latch is write-only
    _portDecoder->DecodePortOut(0x00FB, 0x77, 0x0000);
    uint8_t latches[4];
    covox.getDacLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::RightB)], 0x80);
    uint8_t in = 0;
    EXPECT_FALSE(covox.tryClaimIn(0x00DD, in));

    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}

namespace
{
    class MockCardPort final : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t) override { return 0xFF; }
        void portDeviceOutMethod(uint16_t port, uint8_t value) override { writes.emplace_back(port, value); }
        std::vector<std::pair<uint16_t, uint8_t>> writes;
    };
}

// The ZXM-MoonSound in a slot (the Scorpion INIs fit one) decodes #C5 fully: the card wins the cycle over the
// printer latch's partial decode, which also covers #C5 on the yellow board. The card itself is served from the
// Z80 I/O funnel tap; the model decode stands down
TEST_F(PortDecoder_Scorpion_CovoxDd_Test, FullDecodeCardWinsOverThePartialDecode)
{
    Make(MM_SCORP, true);
    Covox covox(_context);
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));
    MockCardPort moonSound;
    ASSERT_TRUE(_portDecoder->RegisterFullDecodeLowBytePort(0xC5, &moonSound));

    _portDecoder->DecodePortOut(0x00C5, 0x33, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x80) << "the card's port is not the printer latch's";
    _portDecoder->DecodePortOut(0x00DD, 0x44, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x44);

    _portDecoder->UnregisterFullDecodeLowBytePort(0xC5, &moonSound);
    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}

TEST_F(PortDecoder_Scorpion_CovoxDd_Test, TurboPlusDecodeIgnoresA2)
{
    Make(MM_PROFSCORP, true);
    Covox covox(_context);
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));
    _portDecoder->DecodePortOut(0x00D9, 0x5A, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x5A);
    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}

TEST_F(PortDecoder_Scorpion_CovoxDd_Test, OffByDefault)
{
    Make(MM_SCORP, false, true);
    Covox covox(_context);
    EXPECT_FALSE(covox.hasPrinterDac());
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));
    _portDecoder->DecodePortOut(0x00DD, 0x40, 0x0000);
    EXPECT_EQ(covox.printerLatch(), 0x80);
    EXPECT_EQ(covox.lastLeftAmplitude(), 0);
    for (const PortMaskMatch& claim : covox.selfDecodingClaims())
        EXPECT_NE((0x00DD & claim.mask), claim.match) << "no claim covers #DD without CovoxDD";
    _portDecoder->UnregisterSelfDecodingDevice(&covox);

    TearDown();
    Make(MM_PENTAGON, true);
    Covox pentagon(_context);
    EXPECT_FALSE(pentagon.hasPrinterDac()) << "CovoxDD fits nothing off the Scorpion family";
}

TEST_F(PortDecoder_Scorpion_CovoxDd_Test, SumsWithTheSoundriveAndRoundTripsTtd)
{
    Make(MM_GMX, true, false, true);
    Covox covox(_context);
    EXPECT_EQ(covox.fitment(), Covox::Fitment::Mono);
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));

    _portDecoder->DecodePortOut(0x00FB, 0xC0, 0x0000);
    _portDecoder->DecodePortOut(0x00DD, 0xA0, 0x0000);
    uint8_t latches[4];
    covox.getDacLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::RightB)], 0xC0) << "two separate DACs";
    EXPECT_EQ(covox.printerLatch(), 0xA0);
    EXPECT_EQ(covox.lastLeftAmplitude(), (0xC0 - 128) * 128 + (0xA0 - 128) * 128);
    EXPECT_EQ(covox.lastRightAmplitude(), covox.lastLeftAmplitude());

    // The printer channel mutes on its own
    covox.setChannelMute(Covox::Channel::Printer, true);
    _portDecoder->DecodePortOut(0x00DD, 0xA1, 0x0000);
    EXPECT_EQ(covox.lastLeftAmplitude(), (0xC0 - 128) * 128);
    covox.setChannelMute(Covox::Channel::Printer, false);

    // TTD: 11 bytes with the printer DAC (9 without), the printer latch restored
    ASSERT_EQ(covox.TTDStateSize(), 11u);
    std::vector<uint8_t> blob(covox.TTDStateSize());
    covox.TTDSaveState(blob.data());
    Covox restored(_context);
    restored.TTDLoadState(blob.data());
    EXPECT_EQ(restored.printerLatch(), 0xA1);
    std::vector<uint8_t> again(restored.TTDStateSize());
    restored.TTDSaveState(again.data());
    EXPECT_EQ(again, blob);

    _context->config.sound.covoxDD = 0;
    Covox plain(_context);
    EXPECT_EQ(plain.TTDStateSize(), 9u) << "the state of a Covox without the printer DAC is unchanged";

    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}
