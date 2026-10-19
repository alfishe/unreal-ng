#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/ttdrecordedstate.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdds12887.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

/// @file ttdds12887_test.cpp
/// @brief TTD capture of the shared clock chip (PeripheralId::Ds12887) on every
/// machine that wires one: the blob is registered, recording switches the
/// clock runs on emulated time (the tests' fixed start, EmulatorTestHelper;
/// the baseline holds it), the emulated clock follows the frames, a reset
/// keeps the time, and stopping keeps the tests' time base.

namespace
{
    Ds12887* RtcOf(EmulatorContext* context)
    {
        if (auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder))
            return &atm3->GetRtc();
        if (auto* profi = dynamic_cast<PortDecoder_Profi*>(context->pPortDecoder))
            return &profi->GetRtc();
        if (auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(context->pPortDecoder))
            return &scorpion->GetRtc();
        return nullptr;
    }

    uint8_t Read(Ds12887& rtc, uint8_t index)
    {
        rtc.WriteAddress(index);
        return rtc.ReadData();
    }

    void Write(Ds12887& rtc, uint8_t index, uint8_t value)
    {
        rtc.WriteAddress(index);
        rtc.WriteData(value);
    }

    /// 10:00:00 with the divider started now: the first update comes 0.5 s
    /// later. Register A through the chip core: on the ZX-Evo the AVR serves
    /// A as its EEPROM page, so the guest cannot reset the divider there
    void SetTenOClock(Ds12887& rtc)
    {
        rtc.Ds12887::WriteRegister(Ds12887::kRegA, 0x76);
        Write(rtc, Ds12887::kHours, 0x10);
        Write(rtc, Ds12887::kMinutes, 0x00);
        Write(rtc, Ds12887::kSeconds, 0x00);
        rtc.Ds12887::WriteRegister(Ds12887::kRegA, 0x26);
    }
}  // namespace

class TTDDs12887_Test : public ::testing::TestWithParam<std::string>
{
};

/// Recording: the chip is registered, runs on emulated time (the tests' fixed
/// start: the baseline holds it) and counts with the frames; stopping keeps it there
TEST_P(TTDDs12887_Test, RecordingRunsTheClockOnEmulatedTime)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr) << GetParam();
    EmulatorContext* context = emulator->GetContext();
    Ds12887* rtc = RtcOf(context);
    ASSERT_NE(rtc, nullptr);
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);

    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Emulated) << "the tests' fixed start";
    ASSERT_TRUE(ttd->StartRecording());
    EXPECT_TRUE(ttd->GetPeripheralRegistry().IsRegistered(ttd::TTDDeviceType::Ds12887));
    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Emulated) << "still at the baseline";

    const ttd::TTDCheckpoint* baseline = ttd->GetCheckpoint(0);
    ASSERT_NE(baseline, nullptr);
    const auto state = ttdtest::RecordedDeviceState(*ttd, 0, ttd::PeripheralId::Ds12887);
    ASSERT_EQ(state.size(), Ds12887::kStateSize);
    EXPECT_EQ(state[6], static_cast<uint8_t>(Ds12887::TimeMode::Emulated)) << "the baseline holds the emulated time base";

    // Emulated time, however fast the host runs it. The chip follows the
    // video frames: on a turbo machine RunNFrames(n) runs fewer of them.
    // Slower than 50 ms on the ATM3 (~95 ms): at 2x turbo it needs 60 CPU
    // frames of the booting ROM to pass one update, and only a real run shows
    // the decoder's emulated clock following the frame counter
    SetTenOClock(*rtc);
    const uint64_t before = context->pPortDecoder->EmulatedMicroseconds();
    emulator->RunNFrames(60);
    const uint64_t elapsed = context->pPortDecoder->EmulatedMicroseconds() - before;
    ASSERT_GT(elapsed, 500000u) << "at least one update";
    EXPECT_EQ(Read(*rtc, Ds12887::kSeconds), (500000 + elapsed) / 1000000) << "whole seconds of emulated time";

    // The blob replays the same reads
    ttd::TTDSerializable* device = ttd->GetPeripheralRegistry().FindType(ttd::TTDDeviceType::Ds12887);
    ASSERT_NE(device, nullptr);
    std::vector<uint8_t> blob(device->TTDStateSize());
    device->TTDSaveState(blob.data());
    const uint64_t hash = device->TTDHashState();
    const uint8_t seconds = Read(*rtc, Ds12887::kSeconds);
    Write(*rtc, Ds12887::kHours, 0x03);
    device->TTDLoadState(blob.data());
    EXPECT_EQ(device->TTDHashState(), hash);
    EXPECT_EQ(Read(*rtc, Ds12887::kSeconds), seconds);
    EXPECT_EQ(Read(*rtc, Ds12887::kHours), 0x10);

    ttd->StopRecording();
    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Emulated) << "the tests' time base outlives the session";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The tests run the clock on emulated time from a fixed start, so a test never reads the host's speed or its clock;
/// a reset restarts the emulated time but not the battery clock. Boot-bound (~40 frames for half a second of
/// emulated time)
TEST_P(TTDDs12887_Test, TestClockRunsOnEmulatedTimeAndKeepsItOverAReset)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr) << GetParam();
    EmulatorContext* context = emulator->GetContext();
    Ds12887* rtc = RtcOf(context);
    ASSERT_NE(rtc, nullptr);
    EXPECT_EQ(rtc, context->pPortDecoder->RtcChip());
    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Emulated);

    SetTenOClock(*rtc);
    emulator->RunNFrames(40);
    const uint8_t seconds = Read(*rtc, Ds12887::kSeconds);
    const uint8_t minutes = Read(*rtc, Ds12887::kMinutes);
    const uint8_t hours = Read(*rtc, Ds12887::kHours);
    ASSERT_EQ(hours, 0x10);
    emulator->Reset();
    EXPECT_EQ(Read(*rtc, Ds12887::kSeconds), seconds);
    EXPECT_EQ(Read(*rtc, Ds12887::kMinutes), minutes);
    EXPECT_EQ(Read(*rtc, Ds12887::kHours), hours);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

INSTANTIATE_TEST_SUITE_P(Machines, TTDDs12887_Test, ::testing::Values("ATM3", "PROFI", "SCORPION"));
