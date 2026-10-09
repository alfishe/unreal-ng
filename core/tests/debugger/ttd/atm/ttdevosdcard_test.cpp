#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/zcsdtesthelper.h"
#include "debugger/ttd/atm/ttdevosdcard.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_atm3.h"

/// @file ttdevosdcard_test.cpp
/// @brief TTD capture of the ZX-Evo Z-Controller and its SD card
/// (PeripheralId::EvoSdCard): the controller latch and the card's protocol
/// state, so a seek lands mid-transfer exactly where the recording was.

using zcsdtest::PatternDisk;
using zcsdtest::SdCommand;
using zcsdtest::SdInit;

namespace
{
    /// Outside shadow #77 is the Z-Controller config port and #57 its data port
    void LeaveShadow(EmulatorState& state)
    {
        state.atm.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
        state.flags &= ~CF_TRDOS;
        state.evo.pBF = 0x00;
    }
}  // namespace

/// The EvoSdCard blob restores the controller and the card mid-transfer:
/// the byte stream continues exactly as it would have
TEST(ZXEvoSdCardTtd_Test, BlobRoundTripMidMultiBlockRead)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_TRUE(decoder->InsertSdCard(PatternDisk(64), SdCardSpi::WriteMode::Session));
    LeaveShadow(context->emulatorState);
    decoder->DecodePortOut(0x0077, 0x00, 0);
    ASSERT_TRUE(SdInit(decoder, 0x0057));
    ASSERT_EQ(SdCommand(decoder, 0x0057, 18, 2 * 512), 0x00);
    for (int i = 0; i < 100; i++)
        decoder->DecodePortIn(0x0057, 0);

    ttd::TTDEvoSdCard serializer(decoder->GetSdCard(), decoder->GetZController());
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    const uint64_t hash = serializer.TTDHashState();
    std::vector<uint8_t> expected;
    for (int i = 0; i < 700; i++)
        expected.push_back(decoder->DecodePortIn(0x0057, 0));

    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(serializer.TTDHashState(), hash);
    for (int i = 0; i < 700; i++)
        ASSERT_EQ(decoder->DecodePortIn(0x0057, 0), expected[static_cast<size_t>(i)]) << "byte " << i;

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The hash sees the controller latch: selecting the card changes it
TEST(ZXEvoSdCardTtd_Test, HashFollowsTheControllerState)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_TRUE(decoder->InsertSdCard(PatternDisk(16), SdCardSpi::WriteMode::Session));
    LeaveShadow(context->emulatorState);
    decoder->DecodePortOut(0x0077, 0x02, 0);  // deselected

    ttd::TTDEvoSdCard serializer(decoder->GetSdCard(), decoder->GetZController());
    const uint64_t deselected = serializer.TTDHashState();
    decoder->DecodePortOut(0x0077, 0x00, 0);  // selected
    EXPECT_NE(serializer.TTDHashState(), deselected);
    decoder->DecodePortOut(0x0077, 0x02, 0);
    EXPECT_EQ(serializer.TTDHashState(), deselected);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Version 2 stores the whole Z-Controller config byte (the TS-Conf VDAC2
/// build has a second chip select on D2); version 1 blobs, which stored the
/// SD /CS bit, still load
TEST(ZXEvoSdCardTtd_Test, Version1BlobStillLoads)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_TRUE(decoder->InsertSdCard(PatternDisk(16), SdCardSpi::WriteMode::Session));
    LeaveShadow(context->emulatorState);
    decoder->DecodePortOut(0x0077, 0x00, 0);  // selected

    ttd::TTDEvoSdCard serializer(decoder->GetSdCard(), decoder->GetZController());
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    EXPECT_EQ(blob[0], 2) << "current layout";
    EXPECT_EQ(blob[1], 0x00) << "the config byte";

    blob[0] = 1;  // version 1: [1] = SD /CS, 1 = deselected
    blob[1] = 1;
    serializer.TTDLoadState(blob.data());
    EXPECT_FALSE(decoder->GetZController().IsSelected());
    blob[1] = 0;
    serializer.TTDLoadState(blob.data());
    EXPECT_TRUE(decoder->GetZController().IsSelected());

    EmulatorTestHelper::CleanupEmulator(emulator);
}
