// The SD card history (core/src/emulator/io/sdcard/sdcardspi.h): every SdCardSpi host gives its cards one, named by
// the registry id of the host (TS-Conf / ATM3 `sd-zc`, the NeoGS `<slot>.sd`, the Next `sd0` / `sd1`), off until
// configured; commands with their response, blocks read and written

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/statenodejson.h"

namespace
{

class SdHistory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::string _image;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        if (!_image.empty())
        {
            std::error_code ec;
            std::filesystem::remove(_image, ec);
        }
    }

    void Create(const std::string& model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    /// A blank 16-block image
    const std::string& Image()
    {
        _image = TestPathHelper::GetUniqueTestScratchPath("sdhistory.img");
        std::ofstream out(_image, std::ios::binary);
        const std::vector<char> blank(SdCardSpi::BLOCK * 16, 0);
        out.write(blank.data(), static_cast<std::streamsize>(blank.size()));
        return _image;
    }

    /// CMD0 with its CRC; the R1
    static uint8_t Cmd0(SdCardSpi& card)
    {
        for (const uint8_t b : { uint8_t(0x40), uint8_t(0), uint8_t(0), uint8_t(0), uint8_t(0), uint8_t(0x95) })
            card.exchange(b);
        for (int i = 0; i < 16; i++)
            if (const uint8_t r = card.exchange(0xFF); r != 0xFF)
                return r;
        return 0xFF;
    }
};

}  // namespace

/// TS-Conf: the Z-Controller card's history is `sd-zc`; a command with its response, nothing while off
TEST_F(SdHistory_Test, TsConfCardRecordsCommands)
{
    ASSERT_NO_FATAL_FAILURE(Create("TSL"));
    auto* ports = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    SdCardSpi& card = ports->GetSdCard();
    ASSERT_NE(card.History(), nullptr);
    EXPECT_EQ(card.History()->Device(), "sd-zc");
    EXPECT_TRUE(_context->histories.With("sd-zc", [](history::IDeviceHistory&) {}));

    ASSERT_TRUE(card.open(Image()));
    card.select(true);
    EXPECT_EQ(Cmd0(card), 0x01);
    EXPECT_EQ(card.History()->GetStats().written, 0u) << "off: nothing";

    std::string error;
    ASSERT_TRUE(card.History()->Configure({ .mode = history::Mode::Ring, .limit = 16 }, error)) << error;
    EXPECT_EQ(Cmd0(card), 0x01);
    const std::vector<SdHistoryRecord> records = card.History()->Snapshot();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].event.kind, SdTraceEvent::Kind::Command);
    EXPECT_EQ(records[0].event.index, 0);
    EXPECT_EQ(records[0].event.r1, 0x01);
    const std::string json = StateNodeToJsonText(card.History()->Records(0));
    EXPECT_NE(json.find("\"cmd\":\"CMD0\""), std::string::npos) << json;

    _context->ttdReplayActive = true;
    Cmd0(card);
    _context->ttdReplayActive = false;
    EXPECT_EQ(card.History()->GetStats().written, 1u) << "a silent replay records nothing";
}

/// The NeoGS card's SD: `<slot>.sd`, beside the card's GS history; its MP3 decoder's SCI writes `<slot>.mp3`
TEST_F(SdHistory_Test, NeoGsCardNamedBySlot)
{
    GeneralSoundFitScope fit{ GSTypeKind::NGS };
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON"));
    const std::string slot = GSHistoryDevice(_context);
    EXPECT_TRUE(_context->histories.With(slot + ".sd", [](history::IDeviceHistory&) {})) << slot;
    auto* neogs = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(neogs, nullptr);
    EXPECT_EQ(_context->histories.With(slot + ".mp3", [](history::IDeviceHistory&) {}), neogs->mp3Decoder() != nullptr);
}

/// The Next's two cards: `sd0`, `sd1`
TEST_F(SdHistory_Test, NextCardsListed)
{
    ASSERT_NO_FATAL_FAILURE(Create("NEXT"));
    EXPECT_TRUE(_context->histories.With("sd0", [](history::IDeviceHistory&) {}));
    EXPECT_TRUE(_context->histories.With("sd1", [](history::IDeviceHistory&) {}));
}
