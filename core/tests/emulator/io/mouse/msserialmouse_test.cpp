#include "stdafx.h"
#include "pch.h"

#include <utility>
#include <vector>

#include "emulator/io/mouse/msserialmouse.h"

namespace
{
std::pair<uint8_t, uint64_t> At(uint8_t value, uint64_t t)
{
    return {value, t};
}
}  // namespace

/// MsSerialMouse: Microsoft 3-byte packets at 1 200 baud from Kempston-style
/// counters (X + right, Y + up, buttons active low)
class MsSerialMouse_Test : public ::testing::Test
{
protected:
    MsSerialMouse _mouse;
    uint8_t _x = 31, _y = 85, _buttons = 0xFF;
    std::vector<std::pair<uint8_t, uint64_t>> _got;

    void SetUp() override
    {
        _mouse.SetBaseClock(3500000);
        _mouse.SetSampler([this](uint8_t& x, uint8_t& y, uint8_t& buttons) {
            x = _x;
            y = _y;
            buttons = _buttons;
        });
        _mouse.SetByteSink([this](uint8_t value, uint64_t at) { _got.emplace_back(value, at); });
        _mouse.Advance(0);  // the first sample is the reference
    }
};

TEST_F(MsSerialMouse_Test, PacketLayout)
{
    uint8_t p[3];
    MsSerialMouse::BuildPacket(0, 0, false, false, p);
    EXPECT_EQ(p[0], 0x40);
    MsSerialMouse::BuildPacket(-1, 1, true, true, p);
    EXPECT_EQ(p[0], 0x40 | 0x20 | 0x10 | 0x00 | 0x03);  // X = #FF: bits 7-6 = 11
    EXPECT_EQ(p[1], 0x3F);
    EXPECT_EQ(p[2], 0x01);
}

// The header's worked example: 5 right, 3 up, left button, first seen at T = 10 000
TEST_F(MsSerialMouse_Test, MoveBecomesOnePacket)
{
    EXPECT_EQ(_mouse.CharacterTStates(), 26250u) << "9 bits at 1 200 baud, 3.5 MHz";

    _mouse.Advance(5000);
    EXPECT_TRUE(_got.empty()) << "nothing moved";

    _x = 36;
    _y = 88;
    _buttons = 0xFE;
    _mouse.Advance(10000);
    EXPECT_TRUE(_got.empty()) << "the first character is still on the line";
    _mouse.Advance(36250);
    ASSERT_EQ(_got.size(), 1u);
    _mouse.Advance(100000);
    ASSERT_EQ(_got.size(), 3u);
    EXPECT_EQ(_got[0], At(0x6C, 36250));
    EXPECT_EQ(_got[1], At(0x05, 62500));
    EXPECT_EQ(_got[2], At(0x3D, 88750));

    _mouse.Advance(200000);
    EXPECT_EQ(_got.size(), 3u) << "no change, no packet";
}

// The counters are bytes: a sample sees at most 128 either way; -128 goes out as -127, then -1
TEST_F(MsSerialMouse_Test, MoveOf128IsSplit)
{
    _x = static_cast<uint8_t>(_x + 100);
    _mouse.Advance(0);
    _mouse.Advance(1000000);
    ASSERT_EQ(_got.size(), 3u);
    EXPECT_EQ(_got[1].first, 100 & 0x3F);

    _got.clear();
    _x = static_cast<uint8_t>(_x - 128);
    for (uint64_t t = 1000000; t < 2000000; t += 10000)
        _mouse.Advance(t);
    ASSERT_EQ(_got.size(), 6u);
    EXPECT_EQ(_got[0].first & 0x03, 0x02) << "-127 = #81: X7-6 = 10";
    EXPECT_EQ(_got[1].first, 0x01);
    EXPECT_EQ(_got[3].first & 0x03, 0x03) << "-1 = #FF";
    EXPECT_EQ(_got[4].first, 0x3F);
}

TEST_F(MsSerialMouse_Test, ButtonChangeAloneSendsPacket)
{
    _buttons = 0xFD;  // right down
    _mouse.Advance(1000);
    _mouse.Advance(1000000);
    ASSERT_EQ(_got.size(), 3u);
    EXPECT_EQ(_got[0].first, 0x50);
    _buttons = 0xFF;
    _mouse.Advance(1000000);
    _mouse.Advance(2000000);
    ASSERT_EQ(_got.size(), 6u);
    EXPECT_EQ(_got[3].first, 0x40);
}
