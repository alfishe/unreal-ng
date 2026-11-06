// The ZX Next's histories beside its journals (nexthistory.h): the copper's instructions, the DMA's blocks, the UARTs'
// bytes on the device history pattern; off, nothing

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/statenodejson.h"

class NextHistory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void On(const char* device)
    {
        ASSERT_TRUE(_context->histories.With(device, [](history::IDeviceHistory& h) {
            std::string error;
            EXPECT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 64 }, error)) << error;
        })) << device;
    }
    std::string Json(const char* device)
    {
        std::string json;
        _context->histories.With(device, [&](history::IDeviceHistory& h) { json = StateNodeToJsonText(h.Records(0)); });
        return json;
    }
    uint64_t Written(const char* device)
    {
        uint64_t written = 0;
        _context->histories.With(device, [&](history::IDeviceHistory& h) { written = h.GetStats().written; });
        return written;
    }
    void Dma(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _ports->DecodePortOut(0x6B, b, 0);
    }
    void RunDma()
    {
        for (int i = 0; i < 64 && _ports->Dma().Active(); i++)
            _ports->Dma().Run(256, 0);
    }
};

/// A memory copy A #8000 -> B #A000 of 16 bytes: one block; off, none
TEST_F(NextHistory_Test, DmaBlocks)
{
    const std::initializer_list<uint8_t> copy = { 0x7D, 0x00, 0x80, 0x10, 0x00, 0x14, 0x10, 0xAD, 0x00, 0xA0, 0xCF, 0x87 };
    Dma(copy);
    RunDma();
    EXPECT_EQ(Written("dma"), 0u) << "off: nothing";

    On("dma");
    Dma(copy);
    RunDma();
    ASSERT_EQ(Written("dma"), 1u);
    const std::string json = Json("dma");
    EXPECT_NE(json.find("\"source\":32768"), std::string::npos) << json;
    EXPECT_NE(json.find("\"destination\":40960"), std::string::npos) << json;
    EXPECT_NE(json.find("\"length\":16"), std::string::npos) << json;
    EXPECT_NE(json.find("\"source_space\":\"memory\""), std::string::npos) << json;
}

/// A NOP, then the halting WAIT: the NOP is recorded where the beam was
TEST_F(NextHistory_Test, CopperInstructions)
{
    NextCopper& copper = _ports->Board().Copper();
    uint64_t beam = 0;
    copper.SetNow([&beam]() { return beam; });
    copper.SetBeforeWrite(nullptr);
    copper.Reset();
    copper.WriteAddressLow(0);
    copper.WriteControl(0);
    for (uint8_t b : { 0x00, 0x00, 0xFF, 0xFF })  // MOVE 0,0 (the NOP); WAIT for a line that never comes
        copper.WriteData(b);
    On("copper");
    copper.WriteControl(1 << 6);  // mode 1: run from instruction 0
    beam = copper.ClockOf(0, 8);
    _ports->Board().AfterInstruction();
    ASSERT_GE(Written("copper"), 1u);
    const std::string json = Json("copper");
    EXPECT_NE(json.find("\"pc\":0"), std::string::npos) << json;
    EXPECT_NE(json.find("\"kind\":\"nop\""), std::string::npos) << json;
}

/// A byte sent on the ESP UART with nothing on the line still went out of the shifter
TEST_F(NextHistory_Test, UartBytes)
{
    On("uart");
    NextUart& uart = _ports->Uart();
    uart.Write(0x133B, 0x41, 0);
    uart.Advance(10'000'000);
    ASSERT_EQ(Written("uart"), 1u);
    const std::string json = Json("uart");
    EXPECT_NE(json.find("\"dir\":\"tx\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"value\":65"), std::string::npos) << json;
    EXPECT_NE(json.find("\"channel\":\"esp\""), std::string::npos) << json;
}
