// The shared MC146818 / DS12887 clock chip (io/rtc/ds12887.h): registers,
// counting, flags, the hybrid time base, NVRAM image and TTD state.
//
// Most tests run the chip on a test-controlled emulated clock: EnterEmulatedTime()
// anchors at host time, and the guest-style set that follows (divider reset,
// SET, write the time registers, release) makes every later read independent
// of the host clock and its time zone. After the release the first update
// comes half a second later (datasheet), so a set clock reads its value until
// now + 0.5 s and ticks on every whole second after that.

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <new>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/testpathhelper.h"
#include "emulator/io/rtc/ds12887.h"

namespace
{
    constexpr uint64_t kSecond = 1000000;

    /// A chip on a test-driven emulated clock, set to a known instant
    class Rtc
    {
    public:
        explicit Rtc(size_t cells = 128) : chip(cells)
        {
            chip.SetEmulatedClock([this]() { return now; });
            chip.EnterEmulatedTime();
        }

        uint8_t Read(uint8_t index)
        {
            chip.WriteAddress(index);
            return chip.ReadData();
        }

        void Write(uint8_t index, uint8_t value)
        {
            chip.WriteAddress(index);
            chip.WriteData(value);
        }

        /// Set the clock the way a guest does: divider reset, SET, write BCD
        /// registers, release SET, start the divider
        void Set(uint8_t year, uint8_t month, uint8_t day, uint8_t hours, uint8_t minutes, uint8_t seconds)
        {
            Write(Ds12887::kRegA, 0x76);
            Write(Ds12887::kRegB, static_cast<uint8_t>(Read(Ds12887::kRegB) | Ds12887::kBSet));
            Write(Ds12887::kYear, year);
            Write(Ds12887::kMonth, month);
            Write(Ds12887::kDay, day);
            Write(Ds12887::kHours, hours);
            Write(Ds12887::kMinutes, minutes);
            Write(Ds12887::kSeconds, seconds);
            Write(Ds12887::kRegB, static_cast<uint8_t>(Read(Ds12887::kRegB) & ~Ds12887::kBSet));
            Write(Ds12887::kRegA, 0x26);
        }

        Ds12887 chip;
        uint64_t now = 1000 * kSecond;
    };
}  // namespace

/// Regression for the ATM3 (ZX-Evo) non-determinism root cause: the old CMOS
/// class left its cells uninitialized, the BaseConf ROM probes them during
/// boot, and the boot path depended on process memory layout. Construct the
/// chip on top of poisoned storage: every RAM cell must read 0
TEST(Ds12887_Test, ConstructionOverwritesWhateverStorageHeld)
{
    alignas(Ds12887) uint8_t storage[sizeof(Ds12887)];
    std::memset(storage, 0xFF, sizeof(storage));

    Ds12887* chip = new (storage) Ds12887(256);
    for (unsigned index = Ds12887::kFirstRamCell; index < 256; ++index)
    {
        chip->WriteAddress(static_cast<uint8_t>(index));
        ASSERT_EQ(chip->ReadData(), 0) << "cell " << index;
    }
    chip->~Ds12887();
}

TEST(Ds12887_Test, PowerOnRegisters)
{
    Rtc rtc;
    EXPECT_EQ(rtc.Read(Ds12887::kRegA), 0x26) << "divider running, 1024 Hz";
    EXPECT_EQ(rtc.Read(Ds12887::kRegB), 0x02) << "BCD, 24-hour";
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x00);
    EXPECT_EQ(rtc.Read(Ds12887::kRegD), 0x80) << "VRT: battery good";
    rtc.Write(Ds12887::kRegD, 0x00);
    EXPECT_EQ(rtc.Read(Ds12887::kRegD), 0x80) << "D is read-only";
}

/// Fixed time reads the host-local wall time of the instant, frozen
TEST(Ds12887_Test, FixedTimeServesTheLocalInstant)
{
    constexpr time_t kFrozen = 1767268830;  // 2026-01-01 12:00:30 UTC
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &kFrozen);
#else
    localtime_r(&kFrozen, &local);
#endif
    auto bcd = [](int v) { return static_cast<uint8_t>((v % 10) | ((v / 10) << 4)); };

    Ds12887 chip;
    chip.SetFixedTime(kFrozen);
    auto read = [&](uint8_t index) {
        chip.WriteAddress(index);
        return chip.ReadData();
    };
    EXPECT_EQ(read(Ds12887::kSeconds), bcd(local.tm_sec));
    EXPECT_EQ(read(Ds12887::kMinutes), bcd(local.tm_min));
    EXPECT_EQ(read(Ds12887::kHours), bcd(local.tm_hour));
    EXPECT_EQ(read(Ds12887::kDay), bcd(local.tm_mday));
    EXPECT_EQ(read(Ds12887::kMonth), bcd(local.tm_mon + 1)) << "months count from 1 (the old SMUC read 0)";
    EXPECT_EQ(read(Ds12887::kYear), bcd(local.tm_year % 100));
    EXPECT_EQ(read(Ds12887::kDayOfWeek), local.tm_wday + 1) << "1 = Sunday (the old SMUC read wday + 2)";
    EXPECT_EQ(read(Ds12887::kRegC), 0x00) << "a frozen clock never raises UF";
    EXPECT_EQ(read(Ds12887::kRegA) & 0x80, 0x00) << "no UIP while frozen";
}

/// Data mode (B bit 2) and hour format (B bit 1) only change how the same
/// time is presented
TEST(Ds12887_Test, BinaryAnd12HourModes)
{
    Rtc rtc;
    rtc.Set(0x26, 0x03, 0x15, 0x13, 0x05, 0x09);

    EXPECT_EQ(rtc.Read(Ds12887::kHours), 0x13);
    EXPECT_EQ(rtc.Read(Ds12887::kMinutes), 0x05);
    EXPECT_EQ(rtc.Read(Ds12887::kDay), 0x15);

    rtc.Write(Ds12887::kRegB, 0x06);  // binary, 24-hour
    EXPECT_EQ(rtc.Read(Ds12887::kHours), 13);
    EXPECT_EQ(rtc.Read(Ds12887::kDay), 15);
    EXPECT_EQ(rtc.Read(Ds12887::kYear), 26);

    rtc.Write(Ds12887::kRegB, 0x00);  // BCD, 12-hour
    EXPECT_EQ(rtc.Read(Ds12887::kHours), 0x81) << "1 PM";

    rtc.Write(Ds12887::kHours, 0x12);  // 12 AM = midnight
    rtc.Write(Ds12887::kRegB, 0x02);
    EXPECT_EQ(rtc.Read(Ds12887::kHours), 0x00);
    rtc.Write(Ds12887::kRegB, 0x00);
    rtc.Write(Ds12887::kHours, 0x92);  // 12 PM = noon
    rtc.Write(Ds12887::kRegB, 0x02);
    EXPECT_EQ(rtc.Read(Ds12887::kHours), 0x12);
}

/// The clock counts with the time base, across a year boundary
TEST(Ds12887_Test, EmulatedTimeCountsAcrossNewYear)
{
    Rtc rtc;
    rtc.Set(0x99, 0x12, 0x31, 0x23, 0x59, 0x59);
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x59);

    rtc.now += kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x00);
    EXPECT_EQ(rtc.Read(Ds12887::kMinutes), 0x00);
    EXPECT_EQ(rtc.Read(Ds12887::kHours), 0x00);
    EXPECT_EQ(rtc.Read(Ds12887::kDay), 0x01);
    EXPECT_EQ(rtc.Read(Ds12887::kMonth), 0x01);
    EXPECT_EQ(rtc.Read(Ds12887::kYear), 0x00);
}

/// A divider reset (A = 11x) holds the clock at a whole second
TEST(Ds12887_Test, DividerResetHoldsTheClock)
{
    Rtc rtc;
    rtc.Set(0x26, 0x01, 0x01, 0x10, 0x00, 0x00);
    rtc.Write(Ds12887::kRegA, 0x66);
    rtc.now += 10 * kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x00);
    rtc.Write(Ds12887::kRegA, 0x26);
    rtc.now += kSecond / 2;
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x01);
}

/// SET (B bit 7) holds the clock; counting resumes from the written time
TEST(Ds12887_Test, SetBitHoldsTheClock)
{
    Rtc rtc;
    rtc.Set(0x26, 0x01, 0x01, 0x10, 0x00, 0x00);
    rtc.Write(Ds12887::kRegB, 0x92);  // SET + UIE
    EXPECT_EQ(rtc.Read(Ds12887::kRegB) & 0x10, 0x00) << "SET clears UIE";

    rtc.now += 5 * kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x00) << "held";
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x00) << "no update while held";

    rtc.Write(Ds12887::kRegB, 0x02);
    rtc.now += 2 * kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x02);
}

/// UF rises once per update and the read of C clears it; IRQF follows the enables
TEST(Ds12887_Test, UpdateEndedFlagOncePerSecond)
{
    Rtc rtc;
    rtc.Set(0x26, 0x01, 0x01, 0x10, 0x00, 0x00);
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x00);

    rtc.now += kSecond / 2 - 1;
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x00) << "the first update comes half a second after the divider starts";

    rtc.now += 1;
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x10);
    EXPECT_EQ(rtc.Read(Ds12887::kSeconds), 0x01);
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x00) << "cleared by the read";

    rtc.Write(Ds12887::kRegB, 0x12);  // UIE
    rtc.now += kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x90) << "IRQF with UIE";
}

/// UIP (A bit 7) is high for the last 244 us before each update
TEST(Ds12887_Test, UpdateInProgressWindow)
{
    Rtc rtc;
    rtc.Set(0x26, 0x01, 0x01, 0x10, 0x00, 0x00);

    rtc.now += kSecond / 2 - 245;
    EXPECT_EQ(rtc.Read(Ds12887::kRegA), 0x26);
    rtc.now += 2;
    EXPECT_EQ(rtc.Read(Ds12887::kRegA), 0xA6);
    rtc.now += 243;
    EXPECT_EQ(rtc.Read(Ds12887::kRegA), 0x26) << "the update has happened";
    rtc.Write(Ds12887::kRegA, 0xAF);
    EXPECT_EQ(rtc.Read(Ds12887::kRegA) & 0x7F, 0x2F) << "UIP is read-only, bits 6-0 stored";
}

/// AF rises when seconds / minutes / hours match; 11xxxxxx is don't-care
TEST(Ds12887_Test, AlarmFlag)
{
    Rtc rtc;
    rtc.Set(0x26, 0x01, 0x01, 0x10, 0x00, 0x00);
    rtc.Write(Ds12887::kSecondsAlarm, 0x02);
    rtc.Write(Ds12887::kMinutesAlarm, 0xC0);
    rtc.Write(Ds12887::kHoursAlarm, 0xFF);

    rtc.now += kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x10) << "update only";
    rtc.now += kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0x30) << "update + alarm at :02";

    rtc.Write(Ds12887::kRegB, 0x22);  // AIE
    rtc.now += 60 * kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kRegC), 0xB0) << "every minute at :02, IRQF with AIE";
}

/// The day-of-week register is a counter of its own: a date set leaves it,
/// midnight advances it, 7 wraps to 1
TEST(Ds12887_Test, DayOfWeekIsAnIndependentCounter)
{
    Rtc rtc;
    rtc.Set(0x26, 0x01, 0x01, 0x23, 0x59, 0x59);
    rtc.Write(Ds12887::kDayOfWeek, 5);  // the guest sets it: 2026-01-01 is a Thursday
    EXPECT_EQ(rtc.Read(Ds12887::kDayOfWeek), 5);

    rtc.Write(Ds12887::kDayOfWeek, 7);
    rtc.Write(Ds12887::kDay, 0x02);
    rtc.Write(Ds12887::kSeconds, 0x59);
    EXPECT_EQ(rtc.Read(Ds12887::kDayOfWeek), 7) << "a date set does not touch the counter";

    rtc.now += kSecond;
    EXPECT_EQ(rtc.Read(Ds12887::kDay), 0x03);
    EXPECT_EQ(rtc.Read(Ds12887::kDayOfWeek), 1) << "7 wraps to 1 at midnight";
}

/// Century register (DS12887A / DS12C887, used by the Sprinter at 0x32)
TEST(Ds12887_Test, CenturyRegister)
{
    Rtc rtc;
    rtc.chip.SetCenturyRegister(0x32);
    rtc.Set(0x99, 0x12, 0x31, 0x23, 0x59, 0x59);
    rtc.Write(0x32, 0x19);
    EXPECT_EQ(rtc.Read(0x32), 0x19);
    EXPECT_EQ(rtc.Read(Ds12887::kYear), 0x99);

    rtc.now += kSecond;
    EXPECT_EQ(rtc.Read(0x32), 0x20) << "1999 -> 2000";
    EXPECT_EQ(rtc.Read(Ds12887::kYear), 0x00);
}

/// The address decode follows the part size: 64 cells mirror every 0x40
TEST(Ds12887_Test, CellCountMasksTheAddress)
{
    Rtc rtc(64);
    rtc.Write(0x0E, 0x5A);
    EXPECT_EQ(rtc.Read(0x4E), 0x5A);
    EXPECT_EQ(rtc.Read(0x8E), 0x5A);

    Rtc large(256);
    large.Write(0x8E, 0xA5);
    EXPECT_EQ(large.Read(0x0E), 0x00);
    EXPECT_EQ(large.Read(0x8E), 0xA5);
}

/// Leaving emulated time (recording stops) returns to the host clock with the
/// guest's offset kept: a guest that set 1999 still reads 1999
TEST(Ds12887_Test, LeavingEmulatedTimeKeepsTheGuestOffset)
{
    Rtc rtc;
    rtc.Set(0x99, 0x06, 0x15, 0x12, 0x00, 0x00);
    rtc.chip.LeaveEmulatedTime();
    EXPECT_EQ(rtc.chip.GetTimeMode(), Ds12887::TimeMode::Host);
    EXPECT_EQ(rtc.Read(Ds12887::kYear), 0x99);
    EXPECT_EQ(rtc.Read(Ds12887::kMonth), 0x06);

    rtc.chip.SetFixedTime(1767268830);
    rtc.chip.EnterEmulatedTime();
    EXPECT_EQ(rtc.chip.GetTimeMode(), Ds12887::TimeMode::Fixed) << "a fixed clock stays fixed while recording";
}

/// TTD: a restored state replays the same reads, whatever the host clock does
TEST(Ds12887_Test, StateRoundTripReplaysIdentically)
{
    Rtc rtc(256);
    rtc.Set(0x26, 0x02, 0x28, 0x23, 0x59, 0x58);
    rtc.Write(0x40, 0x77);
    rtc.Write(Ds12887::kDayOfWeek, 3);
    rtc.now += kSecond;
    rtc.chip.WriteAddress(0x2E);

    std::vector<uint8_t> state(Ds12887::kStateSize);
    rtc.chip.SaveState(state.data());
    const uint64_t savedNow = rtc.now;

    auto sample = [&]() {
        std::vector<uint8_t> reads;
        for (int step = 0; step < 4; ++step)
        {
            rtc.now += kSecond / 2 + 1;
            for (uint8_t index : {Ds12887::kSeconds, Ds12887::kMinutes, Ds12887::kHours, Ds12887::kDay,
                                  Ds12887::kMonth, Ds12887::kDayOfWeek, Ds12887::kRegC})
                reads.push_back(rtc.Read(index));
        }
        return reads;
    };
    const std::vector<uint8_t> first = sample();

    // Scramble everything, then restore
    rtc.Set(0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
    rtc.Write(0x40, 0x00);
    rtc.Write(Ds12887::kRegB, 0x06);
    rtc.chip.LoadState(state.data());
    rtc.now = savedNow;

    EXPECT_EQ(rtc.chip.GetAddress(), 0x2E) << "address latch";
    EXPECT_EQ(sample(), first);
    EXPECT_EQ(rtc.Read(0x40), 0x77);
    EXPECT_EQ(rtc.chip.GetTimeMode(), Ds12887::TimeMode::Emulated);
}

/// A blob from a different part size is refused rather than scrambling the map
TEST(Ds12887_Test, StateFromAnotherPartSizeIsRefused)
{
    Rtc small(64);
    small.Write(0x0E, 0x11);
    std::vector<uint8_t> state(Ds12887::kStateSize);
    small.chip.SaveState(state.data());

    Rtc large(256);
    large.Write(0x0E, 0x22);
    large.chip.LoadState(state.data());
    EXPECT_EQ(large.Read(0x0E), 0x22);
}

/// NVRAM image: A, B, the alarms and the RAM cells survive; the time does not
/// (it comes from the time base), a short file leaves the power-on contents
TEST(Ds12887_Test, NvramFileRoundTrip)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("ds12887-nvram.bin");

    {
        Rtc rtc;
        rtc.Write(Ds12887::kRegA, 0x2F);
        rtc.Write(Ds12887::kRegB, 0x06);
        rtc.Write(Ds12887::kSecondsAlarm, 0x30);
        rtc.Write(0x0E, 0x5A);
        rtc.Write(0x7F, 0xA5);
        ASSERT_TRUE(rtc.chip.SaveNvram(path));
    }

    Rtc restored;
    ASSERT_TRUE(restored.chip.LoadNvram(path));
    EXPECT_EQ(restored.Read(Ds12887::kRegA) & 0x7F, 0x2F);
    EXPECT_EQ(restored.Read(Ds12887::kRegB), 0x06);
    EXPECT_EQ(restored.Read(Ds12887::kSecondsAlarm), 0x30);
    EXPECT_EQ(restored.Read(0x0E), 0x5A);
    EXPECT_EQ(restored.Read(0x7F), 0xA5);

    {
        std::ofstream truncated(path, std::ios::binary | std::ios::trunc);
        truncated << "short";
    }
    Rtc rejected;
    EXPECT_FALSE(rejected.chip.LoadNvram(path));
    EXPECT_EQ(rejected.Read(0x0E), 0x00);
    EXPECT_FALSE(rejected.chip.LoadNvram(path + ".missing"));

    std::remove(path.c_str());
}
