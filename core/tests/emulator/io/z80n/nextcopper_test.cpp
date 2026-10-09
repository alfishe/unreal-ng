// NextCopper (core/src/emulator/io/z80n/nextcopper.h): the instruction memory through NR #60-#63, WAIT / MOVE timing at
// 28 MHz in the paper-relative raster, the modes of NR #62 and the vertical offset NR #64.
// Source: research-fpga-vhdl.md section 11 (device/copper.vhd).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "emulator/io/z80n/nextcopper.h"

class NextCopper_Test : public ::testing::Test
{
protected:
    NextCopper _copper;
    std::vector<std::pair<uint8_t, uint8_t>> _writes;
    std::vector<uint64_t> _times;
    uint64_t _clock = 0;

    void SetUp() override
    {
        _copper.Reset();
        _copper.SetGeometry(448, 312);
        _copper.SetWriter([this](uint8_t reg, uint8_t value) {
            _writes.push_back({reg, value});
            _times.push_back(_clock);
        });
    }
    static uint16_t Wait(unsigned line, unsigned h) { return static_cast<uint16_t>(0x8000 | (h << 9) | line); }
    static uint16_t Move(unsigned reg, unsigned value) { return static_cast<uint16_t>((reg << 8) | value); }
    /// Program through NR #63 (two writes per instruction), then start in `mode`
    void Load(const std::vector<uint16_t>& code, uint8_t mode)
    {
        _copper.WriteAddressLow(0);
        _copper.WriteControl(0);
        for (uint16_t word : code)
        {
            _copper.WriteWord(word >> 8);
            _copper.WriteWord(word & 0xFF);
        }
        _copper.WriteControl(static_cast<uint8_t>(mode << 6));
    }
    static uint64_t At(unsigned line, unsigned hc) { return (static_cast<uint64_t>(line) * 448 + hc) * 4; }
};

TEST_F(NextCopper_Test, ProgramMemoryReadsBackThroughTheAddress)
{
    _copper.WriteControl(0x05);  // address bits 10:8 = 5
    _copper.WriteAddressLow(0x34);
    EXPECT_EQ(_copper.ReadAddressLow(), 0x34);
    EXPECT_EQ(_copper.ReadControl(), 0x05);
    _copper.WriteData(0xAB);
    _copper.WriteData(0xCD);
    EXPECT_EQ(_copper.Instruction((0x534) / 2), 0xABCD);
    EXPECT_EQ(_copper.ReadAddressLow(), 0x36) << "the address advances per byte";
}

TEST_F(NextCopper_Test, WaitHoldsTheMoveUntilTheBeamIsThere)
{
    Load({Wait(5, 0), Move(0x40, 7), Move(0x41, 9)}, 1);
    _copper.RunTo(At(5, 11));
    EXPECT_TRUE(_writes.empty()) << "horizontal 0 waits for hc >= 12";
    _copper.RunTo(At(5, 12) + 2);
    ASSERT_EQ(_writes.size(), 1u);
    EXPECT_EQ(_writes[0], std::make_pair(uint8_t(0x40), uint8_t(7)));
    _copper.RunTo(At(5, 20));
    ASSERT_EQ(_writes.size(), 2u) << "a MOVE takes two clocks, the next follows";
    EXPECT_EQ(_writes[1], std::make_pair(uint8_t(0x41), uint8_t(9)));
}

TEST_F(NextCopper_Test, MoveToRegisterZeroIsANopAndStoppedModeDoesNothing)
{
    Load({Move(0, 0), Move(0x42, 1)}, 0);
    _copper.RunTo(At(2, 0));
    EXPECT_TRUE(_writes.empty()) << "mode 00: stopped";
    _copper.WriteControl(0x40);  // run from the start
    _copper.RunTo(At(2, 100));
    ASSERT_EQ(_writes.size(), 1u);
    EXPECT_EQ(_writes[0].first, 0x42) << "MOVE 0,0 produces no write";
}

TEST_F(NextCopper_Test, Mode3RestartsAtTheFrameStartAndMode2ContinuesWhereItStopped)
{
    Load({Wait(1, 0), Move(0x40, 1), 0xFFFF}, 3);  // the usual halt: WAIT line 511
    _copper.RunTo(At(2, 0));
    ASSERT_EQ(_writes.size(), 1u);
    _copper.RunTo(At(3, 0));
    EXPECT_EQ(_writes.size(), 1u) << "halted on WAIT 511";
    _copper.RunTo(100);  // the frame wrapped: mode 11 restarts
    _copper.RunTo(At(2, 0));
    EXPECT_EQ(_writes.size(), 2u) << "restarted at the frame start";
}

TEST_F(NextCopper_Test, VerticalOffsetShiftsTheLineTheWaitSees)
{
    _copper.WriteOffset(3);  // the copper's line count runs 3 ahead
    Load({Wait(10, 0), Move(0x40, 5)}, 1);
    _copper.RunTo(At(6, 400));
    EXPECT_TRUE(_writes.empty());
    _copper.RunTo(At(7, 50));
    EXPECT_EQ(_writes.size(), 1u) << "wait line 10 is raw line 7 with an offset of 3";
}
