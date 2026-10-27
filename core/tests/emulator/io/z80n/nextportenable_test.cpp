// The internal port enable word (NR #82-#85, and NR #86-#89 with the expansion bus on): a port whose bit is off is not decoded by the
// board. Source: zxnext.vhd internal_port_enable (2392) and the port_*_io_en gates
// (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, "ports.enforced")

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint8_t kRegExpansionBus = 0x80, kRegEnable0 = 0x82, kRegEnable1 = 0x83, kRegEnable2 = 0x84, kRegBusEnable0 = 0x86;
constexpr uint8_t kBit7ffd = 0x02;          // NR #82 bit 1 (word bit 1)
constexpr uint8_t kBitSprites = 0x40;       // NR #83 bit 6 (word bit 14)
constexpr uint8_t kBitLayer2 = 0x80;        // NR #83 bit 7 (word bit 15)
constexpr uint8_t kBitAy = 0x01;            // NR #84 bit 0 (word bit 16)
constexpr uint8_t kIdleBus = 0xFF, kExpansionBusOn = 0x80;
constexpr uint16_t kPort7ffd = 0x7FFD, kPortLayer2 = 0x123B, kPortSprites = 0x303B, kPortAySelect = 0xFFFD, kPortAyData = 0xBFFD;
constexpr uint16_t kPc = 0x8000;
constexpr uint8_t kAyRegister = 0x00, kAyValue = 0x5A, kLayer2On = 0x02, kPageSix = 0x06;
}  // namespace

class NextPortEnable_Test : public ::testing::Test
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
    void Nr(uint8_t reg, uint8_t value) { _ports->Board().WriteNextReg(reg, value); }
};

TEST_F(NextPortEnable_Test, AWriteToAPortWithItsBitOffIsIgnored)
{
    _ports->DecodePortOut(kPortLayer2, kLayer2On, kPc);
    EXPECT_EQ(_ports->Board().Video().Port123b(), kLayer2On) << "on: the control byte is kept";
    _ports->DecodePortOut(kPortLayer2, 0x00, kPc);

    Nr(kRegEnable1, static_cast<uint8_t>(0xFF & ~kBitLayer2));
    _ports->DecodePortOut(kPortLayer2, kLayer2On, kPc);
    EXPECT_EQ(_ports->Board().Video().Port123b(), 0x00) << "off: #123B is not decoded";
    EXPECT_EQ(_ports->DecodePortIn(kPortLayer2, kPc), kIdleBus) << "a read is the idle bus";

    const uint8_t pagingBefore = _context->emulatorState.p7FFD;
    Nr(kRegEnable0, static_cast<uint8_t>(0xFF & ~kBit7ffd));
    _ports->DecodePortOut(kPort7ffd, kPageSix, kPc);
    EXPECT_EQ(_context->emulatorState.p7FFD, pagingBefore) << "#7FFD off: no paging";
    Nr(kRegEnable0, 0xFF);
    _ports->DecodePortOut(kPort7ffd, kPageSix, kPc);
    EXPECT_EQ(_context->emulatorState.p7FFD & 7, kPageSix) << "on again";
}

TEST_F(NextPortEnable_Test, TheAyAndTheSpritesAnswerOnlyWithTheirBits)
{
    _ports->DecodePortOut(kPortAySelect, kAyRegister, kPc);
    _ports->DecodePortOut(kPortAyData, kAyValue, kPc);
    EXPECT_EQ(_ports->DecodePortIn(kPortAySelect, kPc), kAyValue);

    Nr(kRegEnable2, static_cast<uint8_t>(0xFF & ~kBitAy));
    _ports->DecodePortOut(kPortAyData, 0x11, kPc);
    EXPECT_EQ(_ports->DecodePortIn(kPortAySelect, kPc), kIdleBus) << "off: the read is the idle bus";
    Nr(kRegEnable2, 0xFF);
    EXPECT_EQ(_ports->DecodePortIn(kPortAySelect, kPc), kAyValue) << "the write while off did not reach the chip";

    const uint8_t status = _ports->DecodePortIn(kPortSprites, kPc);
    EXPECT_NE(status, kIdleBus) << "the sprite status has bits 7:2 clear";
    Nr(kRegEnable1, static_cast<uint8_t>(0xFF & ~kBitSprites));
    EXPECT_EQ(_ports->DecodePortIn(kPortSprites, kPc), kIdleBus);
}

TEST_F(NextPortEnable_Test, TheExpansionBusWordIsAndedInOnlyWhileTheBusIsOn)
{
    Nr(kRegBusEnable0, static_cast<uint8_t>(0xFF & ~kBit7ffd));
    _ports->DecodePortOut(kPort7ffd, kPageSix, kPc);
    EXPECT_EQ(_context->emulatorState.p7FFD & 7, kPageSix) << "the bus is off: NR #86 does not count";

    Nr(kRegExpansionBus, kExpansionBusOn);
    _ports->DecodePortOut(kPort7ffd, 0x01, kPc);
    EXPECT_EQ(_context->emulatorState.p7FFD & 7, kPageSix) << "the bus is on: NR #86 bit 1 is clear, #7FFD is off";
}
