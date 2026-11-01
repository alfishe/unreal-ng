// The General Sound history (core/src/emulator/sound/chips/gs/gsporttrace.h): the machine's one, owned by
// SoundManager and named by the card's slot in the registry; the GS port trace verbs are its face

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/portowners.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/soundmanager.h"

namespace
{

class GSHistory_Test : public ::testing::Test
{
protected:
    GeneralSoundFitScope _fit{ GSTypeKind::LW };  // the lightweight card: no firmware, the same host ports
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    GeneralSoundCard* _gs = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pSoundManager, nullptr);
        _gs = _context->pSoundManager->getGeneralSound();
        ASSERT_NE(_gs, nullptr);
        ASSERT_NE(_context->pSoundManager->gsHistory(), nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    GSHistory& History() const { return *_context->pSoundManager->gsHistory(); }
    void Command(uint8_t value) { _context->pPortDecoder->DecodePortOut(0x00BB, value, 0); }
};

}  // namespace

/// Named by the card's slot: the registry owner of the GS command port; listed under that name
TEST_F(GSHistory_Test, NamedByTheCardSlot)
{
    const std::string slot = slots::PortOwners::Of(_context).Find(0x00BB, true, false);
    ASSERT_FALSE(slot.empty()) << "the registry has the GS card";
    EXPECT_EQ(History().Device(), slot);
    EXPECT_TRUE(_context->histories.With(slot, [](history::IDeviceHistory&) {}));
    EXPECT_FALSE(History().On()) << "off until a trace starts: DAC fetches are frequent";
}

/// The port trace session records into the machine's history: start, pause (armed, not recording), stop (the events
/// stay), the history planes see the same events
TEST_F(GSHistory_Test, PortTraceSessionDrivesTheHistory)
{
    _gs->startPortTrace();
    EXPECT_TRUE(History().On());
    Command(0x2A);
    const size_t afterCommand = _gs->getPortTraceEventCount();
    ASSERT_GE(afterCommand, 1u);
    EXPECT_EQ(History().GetStats().kept, afterCommand);

    _gs->pausePortTrace();
    EXPECT_FALSE(_gs->isPortTraceCapturing());
    EXPECT_TRUE(_gs->isPortTraceArmed());
    Command(0x2A);
    EXPECT_EQ(_gs->getPortTraceEventCount(), afterCommand) << "paused: nothing recorded";

    _gs->resumePortTrace();
    Command(0x2A);
    EXPECT_GT(_gs->getPortTraceEventCount(), afterCommand);

    _gs->stopPortTrace();
    EXPECT_FALSE(_gs->isPortTraceArmed());
    EXPECT_GT(_gs->getPortTraceEventCount(), 0u) << "stopped: the events stay readable";
    _gs->resumePortTrace();
    EXPECT_FALSE(_gs->isPortTraceCapturing()) << "a stopped session does not resume";

    const std::vector<GSTraceEvent> events = History().Snapshot();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front().side, GSTraceSide::Host);
    EXPECT_EQ(events.front().port, 0x00BB);
    EXPECT_TRUE(events.front().isOut());
}

/// A silent TTD replay re-executes what the history already holds
TEST_F(GSHistory_Test, SilentReplayRecordsNothing)
{
    _gs->startPortTrace();
    _context->ttdReplayActive = true;
    Command(0x2A);
    _context->ttdReplayActive = false;
    EXPECT_EQ(History().GetStats().written, 0u);
}
