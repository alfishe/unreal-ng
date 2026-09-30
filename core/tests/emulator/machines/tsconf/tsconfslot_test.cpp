// SLOT-1 (TSConf implementation-plan phase 6): TS-Conf registers the ZX-Evo
// SD slot "sd.zc" with the media manager, and a card inserted through it
// reaches the machine's SPI.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/media/mediamanager.h>
#include <emulator/ports/models/portdecoder_tsconf.h>
#include <gtest/gtest.h>

#include "_helpers/zcsdtesthelper.h"
#include "pch.h"
#include "stdafx.h"

/// Runtime: a whole emulator (the media manager lives there), ~50 ms
TEST(TsConfSlot_Test, SLOT1_SdSlotRegisteredAndWorking)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("tsconf-slot", "TSL", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pMediaManager, nullptr);
    EXPECT_TRUE(context->pMediaManager->HasSlot("sd.zc"));

    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    ASSERT_TRUE(decoder->InsertSdCard(zcsdtest::PatternDisk(16), SdCardSpi::WriteMode::Session));
    EXPECT_TRUE(decoder->GetSdCard().present());
    decoder->DecodePortOut(0x0077, 0x00, 0);
    EXPECT_EQ(zcsdtest::SdCommand(decoder, 0x0057, 0, 0, 0x95), 0x01) << "CMD0 through the media manager's card";
    decoder->EjectSdCard();
    EXPECT_FALSE(decoder->GetSdCard().present());
    manager->RemoveEmulator(emulator->GetUUID());
}
