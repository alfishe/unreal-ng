#include "stdafx.h"
#include "pch.h"

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/rtc/rtcaccess.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/memory/devicememory.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/state/devicestate.h"

/// @file rtcaccess_test.cpp
/// @brief The automation path to the CMOS clock (RtcAccess, DeviceState::Rtc):
/// every machine that wires a clock answers, the rest say why not; reads
/// peek, writes act like the guest and never move the address latch.

namespace
{
    struct Machine
    {
        explicit Machine(const std::string& model)
        {
            emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        }
        ~Machine()
        {
            if (emulator)
                EmulatorTestHelper::CleanupEmulator(emulator);
        }
        EmulatorContext* Context() const { return emulator->GetContext(); }
        Emulator* emulator = nullptr;
    };
}  // namespace

class RtcAccess_Test : public ::testing::TestWithParam<std::string>
{
};

/// RAM cells round-trip, the time registers set the clock, the latch stays put
TEST_P(RtcAccess_Test, ReadWriteOnEveryClockMachine)
{
    Machine machine(GetParam());
    ASSERT_NE(machine.emulator, nullptr) << GetParam();
    EmulatorContext* context = machine.Context();
    if (auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(context->pPortDecoder))
        scorpion->SetSmucEnabled(true);  // the clock lives on the SMUC

    std::string error;
    Ds12887* chip = RtcAccess::Find(context, &error);
    ASSERT_NE(chip, nullptr) << error;
    chip->SetFixedTime(1767268830);  // frozen: the set below reads back exactly
    chip->WriteAddress(0x2E);

    ASSERT_TRUE(RtcAccess::Write(context, 0x20, {0x5A, 0xA5, 0x3C}, "test", error)) << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(RtcAccess::Read(context, 0x20, 3, bytes, error)) << error;
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x5A, 0xA5, 0x3C}));

    // Set 23:45 through the time registers (BCD, 24 h)
    ASSERT_TRUE(RtcAccess::Write(context, Ds12887::kMinutes, {0x45}, "test", error)) << error;
    ASSERT_TRUE(RtcAccess::Write(context, Ds12887::kHours, {0x23}, "test", error)) << error;
    ASSERT_TRUE(RtcAccess::Read(context, Ds12887::kMinutes, 3, bytes, error)) << error;
    EXPECT_EQ(bytes[0], 0x45);
    EXPECT_EQ(bytes[2], 0x23);
    EXPECT_EQ(chip->GetAddress(), 0x2E) << "automation access never moves the guest's address latch";

    const StateNode report = DeviceState::Rtc(context);
    ASSERT_TRUE(report.find("available")->b);
    EXPECT_EQ(report.find("time")->find("hours")->i, 23);
    EXPECT_EQ(report.find("time")->find("minutes")->i, 45);
    EXPECT_EQ(report.find("time_mode")->s, "fixed");
    EXPECT_EQ(report.find("address_latch")->i, 0x2E);
    EXPECT_EQ(report.find("cells")->i, 256);
    const StateNode& dump = *report.find("dump");
    ASSERT_EQ(dump.items.size(), 16u);
    EXPECT_EQ(dump.items[2].s.substr(0, 12), "20: 5A A5 3C");
}

/// Ranges outside the chip are refused with the chip's size in the message
TEST_P(RtcAccess_Test, RangesOutsideTheChipAreRefused)
{
    Machine machine(GetParam());
    ASSERT_NE(machine.emulator, nullptr) << GetParam();
    EmulatorContext* context = machine.Context();
    if (auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(context->pPortDecoder))
        scorpion->SetSmucEnabled(true);

    std::vector<uint8_t> bytes;
    std::string error;
    EXPECT_FALSE(RtcAccess::Read(context, 0xF0, 17, bytes, error));
    EXPECT_NE(error.find("256 cells"), std::string::npos) << error;
    EXPECT_FALSE(RtcAccess::Read(context, 0, 0, bytes, error));
    EXPECT_FALSE(RtcAccess::Write(context, 0xFF, {1, 2}, "test", error));
}

/// The same cells as the device memory region "cmos" (debugger additions tdd §4): one path, so the region surfaces
/// (WebAPI /memory/region/cmos, CLI memory region, Lua / Python region_*, the Qt Device memory dialog) agree
TEST_P(RtcAccess_Test, CmosRegionIsTheSamePath)
{
    Machine machine(GetParam());
    ASSERT_NE(machine.emulator, nullptr) << GetParam();
    EmulatorContext* context = machine.Context();
    if (auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(context->pPortDecoder))
        scorpion->SetSmucEnabled(true);

    std::string error;
    Ds12887* chip = RtcAccess::Find(context, &error);
    ASSERT_NE(chip, nullptr) << error;
    IDeviceMemoryRegion* cmos = DeviceMemory::Find(context, "cmos", &error);
    ASSERT_NE(cmos, nullptr) << error;
    EXPECT_EQ(cmos->Size(), chip->GetCellCount());

    ASSERT_TRUE(DeviceMemory::Write(context, "cmos", 0x21, {0x77}, "test", error)) << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(RtcAccess::Read(context, 0x21, 1, bytes, error)) << error;
    EXPECT_EQ(bytes[0], 0x77);
    ASSERT_TRUE(DeviceMemory::Read(context, "cmos", 0x21, 1, bytes, error)) << error;
    EXPECT_EQ(bytes[0], 0x77);
}

/// ZX-Evo: the AVR's 4 KiB EEPROM is the region "eeprom"
TEST(RtcEepromRegion_Test, ZxEvoEepromBytes)
{
    Machine machine("ATM3");
    ASSERT_NE(machine.emulator, nullptr);
    EmulatorContext* context = machine.Context();
    auto* avr = dynamic_cast<EvoAvr*>(RtcAccess::Find(context));
    ASSERT_NE(avr, nullptr);
    std::string error;
    IDeviceMemoryRegion* eeprom = DeviceMemory::Find(context, "eeprom", &error);
    ASSERT_NE(eeprom, nullptr) << error;
    EXPECT_EQ(eeprom->Size(), 4096u);
    ASSERT_TRUE(DeviceMemory::Write(context, "eeprom", 0x123, {0xAB, 0xCD}, "test", error)) << error;
    EXPECT_EQ(avr->EepromData()[0x123], 0xAB);
    EXPECT_EQ(avr->EepromData()[0x124], 0xCD);
    EXPECT_FALSE(DeviceMemory::Write(context, "eeprom", 0xFFF, {1, 2}, "test", error)) << "past the end";
}

INSTANTIATE_TEST_SUITE_P(Machines, RtcAccess_Test, ::testing::Values("ATM3", "PROFI", "SCORPION", "PROFSCORP"));

/// No clock: every call answers with the reason instead of failing silently
TEST(RtcAccessAbsent_Test, MachinesWithoutAClockSayWhy)
{
    {
        Machine pentagon("PENTAGON");
        ASSERT_NE(pentagon.emulator, nullptr);
        std::string error;
        EXPECT_EQ(RtcAccess::Find(pentagon.Context(), &error), nullptr);
        EXPECT_EQ(error, "This machine has no CMOS clock");
        const StateNode report = DeviceState::Rtc(pentagon.Context());
        EXPECT_FALSE(report.find("available")->b);
    }
    {
        Machine scorpion("SCORPION");
        ASSERT_NE(scorpion.emulator, nullptr);
        std::string error;
        std::vector<uint8_t> bytes;
        EXPECT_FALSE(RtcAccess::Read(scorpion.Context(), 0, 1, bytes, error));
        EXPECT_NE(error.find("Scheme=SMUC"), std::string::npos) << "the shipped Scorpion has no SMUC: " << error;
    }
}
