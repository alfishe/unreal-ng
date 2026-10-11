// The magic debug port (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P6; jnext --magic-port): the program's console.
// Source: jnext emulator.cpp (a handler on one exact 16-bit port; hex / dec / ascii / line output)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextmagicport.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

namespace
{
constexpr uint16_t kMagic = 0x00FF;
constexpr uint16_t kOther = 0x01FF;  // the same low byte, another port: the match is the whole address
constexpr uint16_t kPc = 0x8000;
constexpr uint8_t kByteH = 'H', kByteI = 'i', kNewline = '\n', kCarriageReturn = '\r', kEscape = 0x1B;
}  // namespace

TEST(NextMagicPort_Test, EachModeShowsTheBytesAsItsText)
{
    NextMagicPort magic;
    EXPECT_FALSE(magic.Write(kMagic, kByteH, 1, 2, kPc)) << "off: nothing is kept";
    magic.Configure(true, kMagic, NextMagicPortMode::Hex);
    EXPECT_FALSE(magic.Write(kOther, kByteH, 1, 2, kPc)) << "an exact port";
    EXPECT_TRUE(magic.Write(kMagic, 0x5A, 1, 2, kPc));
    magic.Configure(true, kMagic, NextMagicPortMode::Dec);
    EXPECT_TRUE(magic.Write(kMagic, 0x5A, 1, 3, kPc));
    magic.Configure(true, kMagic, NextMagicPortMode::Ascii);
    EXPECT_TRUE(magic.Write(kMagic, kByteH, 1, 4, kPc));
    EXPECT_TRUE(magic.Write(kMagic, kEscape, 1, 5, kPc));
    const auto items = magic.Query(0, 0);
    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0].text, "5A");
    EXPECT_EQ(items[1].text, "90");
    EXPECT_EQ(items[2].text, "H");
    EXPECT_EQ(items[3].text, "\\x1B") << "non-printable";
    EXPECT_EQ(items[3].pc, kPc);
    EXPECT_EQ(magic.Bytes(), 4u);

    // line: up to CR or LF; an empty line is dropped; the half line is visible
    magic.Clear();
    magic.Configure(true, kMagic, NextMagicPortMode::Line);
    for (uint8_t b : {kByteH, kByteI, kCarriageReturn, kNewline, kByteH})
        magic.Write(kMagic, b, 1, 6, kPc);
    ASSERT_EQ(magic.Query(0, 0).size(), 1u);
    EXPECT_EQ(magic.Query(0, 0)[0].text, "Hi");
    EXPECT_EQ(magic.Partial(), "H");
    // since / limit
    EXPECT_EQ(magic.Query(magic.LastSeq(), 0).size(), 0u);
}

class NextMagicPortMachine_Test : public ::testing::Test
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
};

TEST_F(NextMagicPortMachine_Test, AProgramsOutIsTheConsoleAndAToolsOutIsNot)
{
    const StateNode off = DeviceState::NextMagicPortReport(_context, 0, 0);
    EXPECT_EQ(off.find("enabled")->b, false) << "off by default";
    _ports->DecodePortOut(kMagic, kByteH, kPc);
    EXPECT_EQ(_ports->MagicPort().Bytes(), 0u);

    const StateNode on = DeviceState::NextMagicPortControl(_context, 1, kMagic, "line", false);
    EXPECT_EQ(on.find("port")->s, "0x00FF");
    EXPECT_EQ(on.find("mode")->s, "line");
    _ports->DecodePortOut(kMagic, kByteH, kPc);
    _ports->DecodePortOut(kMagic, kNewline, kPc);
    _context->toolAccessActive = true;  // what a tool holds around its own port write
    _ports->DecodePortOut(kMagic, kByteI, kPc);
    _context->toolAccessActive = false;
    const StateNode report = DeviceState::NextMagicPortReport(_context, 0, 0);
    const StateNode* items = report.find("items");
    ASSERT_NE(items, nullptr);
    ASSERT_EQ(items->items.size(), 1u);
    EXPECT_EQ(items->items[0].find("text")->s, "H");
    EXPECT_EQ(items->items[0].find("pc")->s, "0x8000");

    EXPECT_NE(DeviceState::NextMagicPortControl(_context, -1, -1, "binary", false).find("error"), nullptr) << "a bad mode is refused";
    const StateNode cleared = DeviceState::NextMagicPortControl(_context, 0, -1, "", true);
    EXPECT_EQ(cleared.find("bytes")->i, 0);
    EXPECT_EQ(cleared.find("enabled")->b, false);
}
