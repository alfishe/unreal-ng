// The sound chip write histories (core/src/emulator/sound/chips/chipwritehistory.h): one per device outside the AY
// path, named by its registry id, listed while the device lives; the ZX-MultiSound's YM writes into the machine's AY
// history (it takes the AY ports), its SAA / SounDrive and its GS into histories of its own (`<slot>.chips`,
// `<slot>.gs`)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/slots/cards/multisound/multisoundstagedmachine.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/portowners.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/ayhistory.h"
#include "emulator/sound/chips/chipwritehistory.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/statenodejson.h"

namespace
{

/// Turns a listed history to a ring; false when the machine has none by that name
bool Ring(EmulatorContext* context, const std::string& device)
{
    return context->histories.With(device, [](history::IDeviceHistory& h) {
        std::string error;
        EXPECT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 64 }, error)) << error;
    });
}

uint64_t Written(EmulatorContext* context, const std::string& device)
{
    uint64_t written = 0;
    context->histories.With(device, [&](history::IDeviceHistory& h) { written = h.GetStats().written; });
    return written;
}

std::string RecordsJson(EmulatorContext* context, const std::string& device)
{
    std::string json;
    context->histories.With(device, [&](history::IDeviceHistory& h) { json = StateNodeToJsonText(h.Records(0)); });
    return json;
}

class ChipWriteHistory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::string& model, const std::vector<std::pair<std::string, std::string>>& slots = {})
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        if (!slots.empty())
            ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    void Out(uint16_t port, uint8_t value) { _context->pPortDecoder->WriteCycle(port, value, 0x8000); }
};

}  // namespace

/// The Fuller Box's AY: its history is the slot's, off until configured, the data writes with their register
TEST_F(ChipWriteHistory_Test, FullerBoxRecordsItsAyWrites)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K", { { "edge.1", "fuller-box" } }));
    Out(0x003F, 0x07);
    Out(0x005F, 0x38);
    EXPECT_EQ(Written(_context, "edge.1"), 0u) << "off: nothing";
    ASSERT_TRUE(Ring(_context, "edge.1")) << "listed under the slot";

    Out(0x003F, 0x07);
    Out(0x005F, 0x38);
    EXPECT_EQ(Written(_context, "edge.1"), 1u) << "a data write; the select is part of it";
    const std::string json = RecordsJson(_context, "edge.1");
    EXPECT_NE(json.find("\"chip\":\"ay\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"reg\":7"), std::string::npos) << json;
    EXPECT_NE(json.find("\"value\":56"), std::string::npos) << json;
}

/// The MoonSound's OPL4: named by the owner of its FM ports, the bank in the register's high byte
TEST_F(ChipWriteHistory_Test, MoonSoundRecordsOpl4Writes)
{
    SoundCardScope sound{ TestSound::MoonSound };
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON"));
    if (!_context->pSoundManager->hasMoonSound())
        GTEST_SKIP() << "built without the OPL4";
    const std::string device = ChipHistoryDevice(_context, 0x00C4, "moonsound");
    ASSERT_TRUE(Ring(_context, device)) << device;

    Out(0x00C6, 0x05);  // bank 1, register 5 (NEW / NEW2)
    Out(0x00C7, 0x03);
    EXPECT_EQ(Written(_context, device), 1u);
    const std::string json = RecordsJson(_context, device);
    EXPECT_NE(json.find("\"chip\":\"opl4\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"reg\":261"), std::string::npos) << "bank 1 << 8 | 5: " << json;
}

/// The ZX-MultiSound: the YM into the machine's AY history (named by the card, which takes the AY ports), the SAA
/// into `<slot>.chips`; its GS history listed as `<slot>.gs`
TEST(ChipWriteHistory_MultiSound, CardHistoriesByModule)
{
    multisoundtest::StagedMachine m("pentagon128k", "zxbus.1 = multisound");
    ASSERT_TRUE(m.Ok());
    EmulatorContext* context = m.Context();
    ASSERT_TRUE(context->histories.With("zxbus.1.gs", [](history::IDeviceHistory&) {}));
    ASSERT_TRUE(Ring(context, "zxbus.1.chips"));
    AYHistory* ay = context->pSoundManager->ayHistory();
    ASSERT_NE(ay, nullptr);
    EXPECT_EQ(ay->Device(), "zxbus.1");
    ASSERT_TRUE(Ring(context, "zxbus.1"));

    multisoundtest::Out(m, 0xFFFD, 0x07);
    multisoundtest::Out(m, 0xBFFD, 0x38);
    multisoundtest::Out(m, 0x01FF, 0x1C);
    multisoundtest::Out(m, 0x00FF, 0x01);

    const std::vector<AYLogRecord> ym = ay->Snapshot();
    ASSERT_EQ(ym.size(), 2u);
    EXPECT_EQ(ym[1].port, 0xBFFD);
    EXPECT_EQ(ym[1].reg, 7);
    EXPECT_EQ(ym[1].value, 0x38);
    EXPECT_EQ(Written(context, "zxbus.1.chips"), 1u);
    const std::string json = RecordsJson(context, "zxbus.1.chips");
    EXPECT_NE(json.find("\"chip\":\"saa\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"reg\":28"), std::string::npos) << json;
}

/// The Sprinter's Covox Blaster: the board's built-in, listed under its registry id
TEST_F(ChipWriteHistory_Test, SprinterCovoxBlasterListed)
{
    ASSERT_NO_FATAL_FAILURE(Create("SPRINTER"));
    const std::string device = ChipHistoryDevice(_context, 0x004E, "covox-blaster");
    EXPECT_EQ(device, "covox-blaster");
    EXPECT_TRUE(_context->histories.With(device, [](history::IDeviceHistory&) {}));
}

/// A silent TTD replay records nothing
TEST_F(ChipWriteHistory_Test, SilentReplayRecordsNothing)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K", { { "edge.1", "fuller-box" } }));
    ASSERT_TRUE(Ring(_context, "edge.1"));
    _context->ttdReplayActive = true;
    Out(0x005F, 0x38);
    _context->ttdReplayActive = false;
    EXPECT_EQ(Written(_context, "edge.1"), 0u);
}
