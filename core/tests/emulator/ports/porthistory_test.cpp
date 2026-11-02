// The port histories (core/src/emulator/ports/porthistory.h): one per registry device (`<device>.ports`) and
// `board.ports` for the ports no device claims; each apart, so one device's traffic never floods another's

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/portowners.h"
#include "emulator/state/statenodejson.h"

namespace
{

class PortHistory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pPortDecoder, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    PortHistories& Histories() const { return _context->pPortDecoder->GetPortHistories(); }
    std::string Owner(uint16_t port) const { return slots::PortOwners::Of(_context).Find(port, true, false); }
    std::string HistoryOf(uint16_t port) const
    {
        const std::string owner = Owner(port);
        return (owner.empty() ? std::string("board") : owner) + ".ports";
    }

    bool Configure(const std::string& name, history::Mode mode)
    {
        return _context->histories.With(name, [mode](history::IDeviceHistory& h) {
            std::string error;
            EXPECT_TRUE(h.Configure({ .mode = mode, .limit = 64 }, error)) << error;
        });
    }
    uint64_t Written(const std::string& name) const
    {
        uint64_t written = 0;
        _context->histories.With(name, [&](history::IDeviceHistory& h) { written = h.GetStats().written; });
        return written;
    }
    std::string Json(const std::string& name) const
    {
        std::string json;
        _context->histories.With(name, [&](history::IDeviceHistory& h) { json = StateNodeToJsonText(h.Records(0)); });
        return json;
    }
    void Out(uint16_t port, uint8_t value) { _context->pPortDecoder->DecodePortOut(port, value, 0x1234); }
};

}  // namespace

/// Every registry device has its history, the board one besides; all off at the start
TEST_F(PortHistory_Test, OnePerRegistryDevice)
{
    const auto devices = Histories().Devices();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices.front(), "board");
    for (const std::string& id : devices)
        EXPECT_TRUE(_context->histories.With(id + ".ports", [](history::IDeviceHistory&) {})) << id;
    ASSERT_FALSE(Owner(0xFFFD).empty()) << "the Pentagon's AY is a registry device";
    EXPECT_FALSE(Histories().AnyOn());
}

/// One device's history on: its accesses only, with the time, PC, direction and value; the others stay empty
TEST_F(PortHistory_Test, SeparateByOwner)
{
    const std::string ay = HistoryOf(0xFFFD);
    const std::string border = HistoryOf(0x00FE);
    ASSERT_NE(ay, border);

    Out(0xFFFD, 0x07);
    EXPECT_EQ(Written(ay), 0u) << "off: nothing";

    ASSERT_TRUE(Configure(ay, history::Mode::Ring));
    EXPECT_TRUE(Histories().AnyOn());
    Out(0xFFFD, 0x07);
    Out(0x00FE, 0x02);
    EXPECT_EQ(Written(ay), 1u);
    EXPECT_EQ(Written(border), 0u) << "the border port's owner is off";
    const std::string json = Json(ay);
    EXPECT_NE(json.find("\"port\":65533"), std::string::npos) << json;
    EXPECT_NE(json.find("\"dir\":\"out\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"pc\":4660"), std::string::npos) << json;
    EXPECT_NE(json.find("\"value\":7"), std::string::npos) << json;

    ASSERT_TRUE(Configure(border, history::Mode::Ring));
    Out(0x00FE, 0x02);
    EXPECT_EQ(Written(border), 1u);
    EXPECT_EQ(Written(ay), 1u) << "the AY's history does not see the border";

    ASSERT_TRUE(Configure(ay, history::Mode::Off));
    ASSERT_TRUE(Configure(border, history::Mode::Off));
    EXPECT_FALSE(Histories().AnyOn()) << "all off again: back to one load an access";
}

/// Nothing while a TTD replay drives the machine
TEST_F(PortHistory_Test, NothingDuringReplay)
{
    const std::string ay = HistoryOf(0xFFFD);
    ASSERT_TRUE(Configure(ay, history::Mode::Ring));
    _context->ttdReplayActive = true;
    Out(0xFFFD, 0x07);
    _context->ttdReplayActive = false;
    EXPECT_EQ(Written(ay), 0u);
}

/// The Next's own devices are the registry's: their ports go to their histories, not the board's
TEST(PortHistoryNext_Test, NextDevicesHaveTheirOwn)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (const char* device : { "nextreg.ports", "dma.ports", "uart.ports", "sprites.ports", "spi.ports", "board.ports" })
        EXPECT_TRUE(context->histories.With(device, [](history::IDeviceHistory&) {})) << device;
    ASSERT_TRUE(context->histories.With("nextreg.ports", [](history::IDeviceHistory& h) {
        std::string error;
        EXPECT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 16 }, error)) << error;
    }));
    context->pPortDecoder->DecodePortOut(0x243B, 0x07, 0x8000);
    context->pPortDecoder->DecodePortOut(0x6B, 0xC3, 0x8002);
    uint64_t written = 0;
    context->histories.With("nextreg.ports", [&](history::IDeviceHistory& h) { written = h.GetStats().written; });
    EXPECT_EQ(written, 1u) << "the select, not the DMA's write";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
