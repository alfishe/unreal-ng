#include "stdafx.h"
#include "pch.h"

#include <utility>
#include <vector>

#include "emulator/io/keyboard/ps2keyboardstream.h"

namespace
{
std::pair<uint8_t, uint64_t> At(uint8_t value, uint64_t t)
{
    return {value, t};
}
}  // namespace

/// Ps2KeyboardStream: a PS/2 keyboard's set-2 bytes on the wire, one 11-bit
/// frame each, in emulated time (3.5 MHz base clock: a byte = 3 210 T)
class Ps2KeyboardStream_Test : public ::testing::Test
{
protected:
    Ps2KeyboardStream _kbd;
    uint64_t _now = 0;
    std::vector<std::pair<uint8_t, uint64_t>> _got;

    void SetUp() override
    {
        _kbd.SetBaseClock(3500000);
        _kbd.SetClock([this]() { return _now; });
        _kbd.SetByteSink([this](uint8_t value, uint64_t at) { _got.emplace_back(value, at); });
    }

    std::vector<uint8_t> Bytes() const
    {
        std::vector<uint8_t> out;
        for (const auto& b : _got)
            out.push_back(b.first);
        return out;
    }
};

TEST_F(Ps2KeyboardStream_Test, TimingConstants)
{
    EXPECT_EQ(_kbd.ByteTStates(), 3210u) << "917 us at 3.5 MHz";
    EXPECT_EQ(_kbd.TypematicDelayTStates(), 1750000u) << "500 ms";
    EXPECT_EQ(_kbd.TypematicPeriodTStates(), 321101u) << "10.9 per second";
}

// The header's worked example: Up = E0 75, its break E0 F0 75, each byte at the end of its frame
TEST_F(Ps2KeyboardStream_Test, BytesArriveOneFrameApart)
{
    _now = 1000;
    _kbd.OnPcKey(PcKey::Up, true);
    EXPECT_TRUE(_kbd.Busy());
    EXPECT_EQ(_kbd.NextEventAt(), 4210u);

    _kbd.Advance(4209);
    EXPECT_TRUE(_got.empty()) << "nothing before the first frame ends";
    _kbd.Advance(4210);
    ASSERT_EQ(_got.size(), 1u);
    EXPECT_EQ(_got[0], At(0xE0, 4210));
    _kbd.Advance(7420);
    ASSERT_EQ(_got.size(), 2u);
    EXPECT_EQ(_got[1], At(0x75, 7420));

    _now = 100000;
    _kbd.OnPcKey(PcKey::Up, false);
    _kbd.Advance(200000);
    ASSERT_EQ(_got.size(), 5u);
    EXPECT_EQ(Bytes(), (std::vector<uint8_t>{0xE0, 0x75, 0xE0, 0xF0, 0x75}));
    EXPECT_EQ(_got[2].second, 103210u);
    EXPECT_EQ(_got[3].second, 106420u);
    EXPECT_EQ(_got[4].second, 109630u);
    EXPECT_FALSE(_kbd.Busy()) << "released: no repeat, nothing queued";
}

// A key pressed while the wire still sends waits for it: no two frames overlap
TEST_F(Ps2KeyboardStream_Test, KeysQueueBehindEachOther)
{
    _now = 0;
    _kbd.OnPcKey(PcKey::A, true);   // 1C at 3 210
    _now = 100;
    _kbd.OnPcKey(PcKey::A, false);  // F0 1C at 6 420, 9 630
    _kbd.Advance(1000000);
    EXPECT_EQ(Bytes(), (std::vector<uint8_t>{0x1C, 0xF0, 0x1C}));
    EXPECT_EQ(_got[1].second, 6420u);
    EXPECT_EQ(_got[2].second, 9630u);
}

// A second press of a held key and a release of a key not held send nothing
TEST_F(Ps2KeyboardStream_Test, NoChangeSendsNothing)
{
    _kbd.OnPcKey(PcKey::B, false);
    EXPECT_FALSE(_kbd.Busy());
    _kbd.OnPcKey(PcKey::B, true);
    _kbd.OnPcKey(PcKey::B, true);
    _now = 100000;
    _kbd.OnPcKey(PcKey::B, false);
    _kbd.Advance(200000);
    EXPECT_EQ(Bytes(), (std::vector<uint8_t>{0x32, 0xF0, 0x32}));
}

// Held past 500 ms, the key repeats its make bytes 10.9 times per second
TEST_F(Ps2KeyboardStream_Test, TypematicRepeat)
{
    _now = 1000;
    _kbd.OnPcKey(PcKey::Left, true);  // E0 6B
    _kbd.Advance(1750999);
    EXPECT_EQ(_got.size(), 2u) << "no repeat before 500 ms";
    _kbd.Advance(1751000 + 321101 + 3210 * 2);
    ASSERT_EQ(_got.size(), 6u) << "two repeats";
    EXPECT_EQ(_got[2], At(0xE0, 1751000 + 3210));
    EXPECT_EQ(_got[4], At(0xE0, 1751000 + 321101 + 3210));

    _now = 2300000;  // before the third repeat (2 393 202)
    _kbd.OnPcKey(PcKey::Left, false);
    _kbd.Advance(3000000);
    EXPECT_EQ(_got.size(), 9u) << "the release stops the repeat: only E0 F0 6B more";
    EXPECT_FALSE(_kbd.Busy());
}

// Only the last key made repeats; its release stops the repeat even with another key still held
TEST_F(Ps2KeyboardStream_Test, TypematicFollowsTheLastKey)
{
    _kbd.OnPcKey(PcKey::LeftShift, true);  // 12
    _now = 10000;
    _kbd.OnPcKey(PcKey::Z, true);          // 1A
    _now = 1000000;
    _kbd.OnPcKey(PcKey::Z, false);         // F0 1A, before the repeat
    _kbd.Advance(5000000);
    EXPECT_EQ(Bytes(), (std::vector<uint8_t>{0x12, 0x1A, 0xF0, 0x1A})) << "Shift held: no repeat of an older key";
    EXPECT_TRUE(_kbd.IsHeld(PcKey::LeftShift));
}

// The keyboard's buffer is 16 bytes: an event that does not fit is dropped and #00 follows
TEST_F(Ps2KeyboardStream_Test, BufferOverflowSendsOverrunCode)
{
    // Print Screen: 4 bytes on the press, 6 on the release
    _kbd.OnPcKey(PcKey::PrintScreen, true);   // 4
    _kbd.OnPcKey(PcKey::PrintScreen, false);  // 10
    _kbd.OnPcKey(PcKey::PrintScreen, true);   // 14
    _kbd.OnPcKey(PcKey::PrintScreen, false);  // 6 more do not fit: dropped
    _kbd.Advance(1000000);
    ASSERT_EQ(_got.size(), 15u);
    EXPECT_EQ(_got.back().first, Ps2KeyboardStream::kOverflowCode);
}

// Machine reset: times restart with the clock, held keys and queued bytes stay
TEST_F(Ps2KeyboardStream_Test, RebaseKeepsStateMovesTimes)
{
    _now = 500000;
    _kbd.OnPcKey(PcKey::Enter, true);  // 5A due at 503 210
    _kbd.Rebase(0);
    EXPECT_EQ(_kbd.NextEventAt(), 3210u);
    EXPECT_TRUE(_kbd.IsHeld(PcKey::Enter));
    _kbd.Advance(3210);
    EXPECT_EQ(Bytes(), (std::vector<uint8_t>{0x5A}));

    _kbd.Clear();
    EXPECT_FALSE(_kbd.Busy());
    EXPECT_FALSE(_kbd.IsHeld(PcKey::Enter));
}

// Pause sends its 8-byte make and nothing on release; it never counts as held
TEST_F(Ps2KeyboardStream_Test, PauseHasNoBreak)
{
    _kbd.OnPcKey(PcKey::Pause, true);
    _kbd.OnPcKey(PcKey::Pause, false);
    _kbd.Advance(UINT64_MAX - 1);
    EXPECT_EQ(Bytes(), (std::vector<uint8_t>{0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77}));
    EXPECT_FALSE(_kbd.IsHeld(PcKey::Pause));
    EXPECT_FALSE(_kbd.Busy()) << "no typematic for Pause";
}
