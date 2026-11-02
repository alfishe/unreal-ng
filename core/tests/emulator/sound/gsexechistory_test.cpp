// The GS card CPU's execution history (`<slot>.cpu`, exechistory.h): the card's Z80 instructions on the device
// history pattern; off, the card runs its fast loop

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/state/statenodejson.h"

namespace
{

class GSExecHistory_Test : public ::testing::Test
{
protected:
    GeneralSoundFitScope _fit{ GSTypeKind::NGS };  // a card with its own Z80
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::string _name;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _name = GSHistoryDevice(_context) + ".cpu";
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool Configure(history::Mode mode)
    {
        return _context->histories.With(_name, [mode](history::IDeviceHistory& h) {
            std::string error;
            EXPECT_TRUE(h.Configure({ .mode = mode, .limit = 256 }, error)) << error;
        });
    }
    uint64_t Written() const
    {
        uint64_t written = 0;
        _context->histories.With(_name, [&](history::IDeviceHistory& h) { written = h.GetStats().written; });
        return written;
    }
};

}  // namespace

/// Listed under the card's slot; nothing while off; on, the card's instructions with their pages; off again, the
/// count stands still
TEST_F(GSExecHistory_Test, CardInstructionsOnThePattern)
{
    ASSERT_TRUE(_context->histories.With(_name, [](history::IDeviceHistory&) {})) << _name;
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_EQ(Written(), 0u) << "off: nothing";

    ASSERT_TRUE(Configure(history::Mode::Ring));
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    const uint64_t written = Written();
    EXPECT_GT(written, 1000u) << "a frame of the card's Z80";
    std::string json;
    _context->histories.With(_name, [&](history::IDeviceHistory& h) { json = StateNodeToJsonText(h.Records(1)); });
    EXPECT_NE(json.find("\"pc\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"kind\""), std::string::npos) << json;

    ASSERT_TRUE(Configure(history::Mode::Off));
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_EQ(Written(), 0u) << "off: reset and nothing more";
}
