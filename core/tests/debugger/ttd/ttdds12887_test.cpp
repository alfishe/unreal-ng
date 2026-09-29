#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
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
/// clock to emulated time before the baseline, the emulated clock follows the
/// frames, and stopping returns it to the host clock with the guest's offset.

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

/// Recording: the chip is registered, runs on emulated time from the
/// baseline on and counts with the frames; stopping hands it back to the host
TEST_P(TTDDs12887_Test, RecordingRunsTheClockOnEmulatedTime)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr) << GetParam();
    EmulatorContext* context = emulator->GetContext();
    Ds12887* rtc = RtcOf(context);
    ASSERT_NE(rtc, nullptr);
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);

    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Host);
    ASSERT_TRUE(ttd->StartRecording());
    EXPECT_TRUE(ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::Ds12887));
    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Emulated) << "switched before the baseline";

    const ttd::TTDCheckpoint* baseline = ttd->GetCheckpoint(0);
    ASSERT_NE(baseline, nullptr);
    ASSERT_EQ(baseline->peripheralBlobs.count(static_cast<uint8_t>(ttd::PeripheralId::Ds12887)), 1u);
    const auto state = ttd::TTDPeripheralRegistry::DecodeBlob(static_cast<uint8_t>(ttd::PeripheralId::Ds12887),
                                                             baseline->peripheralBlobs.at(static_cast<uint8_t>(ttd::PeripheralId::Ds12887)));
    ASSERT_EQ(state.size(), Ds12887::kStateSize);
    EXPECT_EQ(state[6], static_cast<uint8_t>(Ds12887::TimeMode::Emulated)) << "the baseline blob holds the emulated time base";

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
    ttd::TTDSerializable* device = ttd->GetPeripheralRegistry().GetDevice(ttd::PeripheralId::Ds12887);
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
    EXPECT_EQ(rtc->GetTimeMode(), Ds12887::TimeMode::Host) << "back to the host clock";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

INSTANTIATE_TEST_SUITE_P(Machines, TTDDs12887_Test, ::testing::Values("ATM3", "PROFI", "SCORPION"));
