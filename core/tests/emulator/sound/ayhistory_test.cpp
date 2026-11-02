// The AY history (core/src/emulator/sound/chips/ayhistory.h): one per machine, named by the registry device on the AY
// ports, written by whatever device takes them (the TSFM, the TurboSound pair, the Next's three AYs); the AY log
// analyzer is its face

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/aylog/ayloganalyzer.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/slots/portowners.h"
#include "emulator/sound/chips/ayhistory.h"
#include "emulator/sound/soundmanager.h"

namespace
{

class AYHistory_Test : public ::testing::Test
{
protected:
    SoundCardScope _sound{ TestSound::TurboSound };  // the runner leaves the AY socket empty otherwise
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(Emulator* emulator)
    {
        _emulator = emulator;
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pSoundManager, nullptr);
        ASSERT_NE(_context->pSoundManager->ayHistory(), nullptr);
    }

    AYHistory& History() const { return *_context->pSoundManager->ayHistory(); }

    void Ring()
    {
        std::string error;
        ASSERT_TRUE(History().Configure({ .mode = history::Mode::Ring, .limit = 64 }, error)) << error;
    }

    void Out(uint16_t port, uint8_t value) { _context->pPortDecoder->DecodePortOut(port, value, 0); }
};

}  // namespace

/// Named by the registry: the card in the AY socket shadows the built-in AY; the history is listed under that name
TEST_F(AYHistory_Test, NamedByTheRegistryDeviceOnTheAyPorts)
{
    ASSERT_NO_FATAL_FAILURE(Create(EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM)));
    EXPECT_EQ(History().Device(), "ay-socket");
    EXPECT_EQ(History().Device(), slots::PortOwners::Of(_context).Find(0xFFFD, true, false));
    EXPECT_FALSE(History().On()) << "off until configured: AY writes are frequent";
    EXPECT_TRUE(_context->histories.With("ay-socket", [](history::IDeviceHistory&) {}));
}

/// The TSFM records selects and data writes after applying them: the register a data write went to, the chip
TEST_F(AYHistory_Test, TsfmRecordsSelectsAndWrites)
{
    ASSERT_NO_FATAL_FAILURE(Create(EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM)));
    Out(0xFFFD, 0x07);
    EXPECT_EQ(History().GetStats().written, 0u) << "off: nothing";

    Ring();
    Out(0xFFFD, 0xFF);  // the control word: chip 1 (bit 0), FM muted
    Out(0xFFFD, 0x07);
    Out(0xBFFD, 0x38);
    Out(0xFFFD, 0xFE);  // chip 0
    Out(0xFFFD, 0x08);
    Out(0xBFFD, 0x0F);
    const std::vector<AYLogRecord> records = History().Snapshot();
    ASSERT_EQ(records.size(), 6u);
    EXPECT_EQ(records[2].port, 0xBFFD);
    EXPECT_EQ(records[2].reg, 7);
    EXPECT_EQ(records[2].value, 0x38);
    EXPECT_EQ(records[2].chip, 1);
    EXPECT_EQ(records[5].reg, 8);
    EXPECT_EQ(records[5].chip, 0) << "the control word switched the chip";
}

/// The Next's three AYs (NextAudio) write into the machine's one AY history, named by the registry
TEST_F(AYHistory_Test, NextThreeChipsWriteIntoTheOneHistory)
{
    ASSERT_NO_FATAL_FAILURE(Create(EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero)));
    auto* ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    ports->Board().Write(0x08, 0x02);  // turbosound on
    // The registry still has the bare personality's AY socket (the 128K board): the history takes its owner's name
    EXPECT_EQ(History().Device(), slots::PortOwners::Of(_context).Find(0xFFFD, true, false));

    Ring();
    for (const uint8_t select : { uint8_t(0xFF), uint8_t(0xFE), uint8_t(0xFD) })  // chips 0, 1, 2
    {
        Out(0xFFFD, select);
        Out(0xFFFD, 0x00);
        Out(0xBFFD, select);
    }
    const std::vector<AYLogRecord> records = History().Snapshot();
    ASSERT_EQ(records.size(), 9u);
    for (unsigned chip = 0; chip < 3; chip++)
    {
        const AYLogRecord& write = records[chip * 3 + 2];
        EXPECT_EQ(write.port, 0xBFFD);
        EXPECT_EQ(write.chip, chip);
        EXPECT_EQ(write.reg, 0);
    }
}

/// The AY log analyzer is the history's face: activation turns it to a ring of the analyzer's capacity
TEST_F(AYHistory_Test, AyLogAnalyzerDrivesTheHistory)
{
    ASSERT_NO_FATAL_FAILURE(Create(EmulatorTestHelper::CreateDebugEmulator({ "analyzers" }, "PENTAGON")));
    ASSERT_NE(_context->pDebugManager, nullptr);
    AnalyzerManager* analyzers = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(analyzers, nullptr);
    AYLogAnalyzer* log = analyzers->getAnalyzer<AYLogAnalyzer>("aylog");
    ASSERT_NE(log, nullptr);

    log->setCapacity(16);
    analyzers->activate("aylog");
    EXPECT_TRUE(History().On());
    EXPECT_TRUE(log->isRecording());
    for (int i = 0; i < 20; i++)
        Out(0xFFFD, uint8_t(i & 0x0F));
    EXPECT_EQ(log->getEntryCount(), 16u);
    EXPECT_EQ(log->getDroppedCount(), 4u);
    EXPECT_EQ(log->getEntries(15, 0).size(), 1u);
    EXPECT_EQ(History().GetStats().kept, 16u) << "the history planes see the same records";

    analyzers->deactivate("aylog");
    EXPECT_FALSE(History().On());
}

/// A silent TTD replay re-executes what the history already holds: nothing is recorded twice
TEST_F(AYHistory_Test, SilentReplayRecordsNothing)
{
    ASSERT_NO_FATAL_FAILURE(Create(EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM)));
    Ring();
    _context->ttdReplayActive = true;
    Out(0xFFFD, 0x07);
    _context->ttdReplayActive = false;
    Out(0xFFFD, 0x07);
    EXPECT_EQ(History().GetStats().written, 1u);
}
