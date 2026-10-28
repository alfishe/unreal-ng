/// @file snapshotlauncher_test.cpp
/// @brief SnapshotLauncher (snapshot pipeline P7, PLAN #84): what a file needs of the running machine, and when the launcher
/// replaces the machine. An .spg switches to TS-Conf unless told not to; an .szx saved on another model is refused unless the
/// caller says switch_model=true or the configuration has [SNAPSHOT] SwitchModel=1. A .z80 / .sna the running machine cannot
/// hold (a 128K file on a 48K) follows the same rule: the machine it was made on, else what the plan's refusal needs.

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "loaders/snapshot/snapshotlauncher.h"

namespace
{
const char* kSzx128 = "loaders/szx/libspectrum/synth-128.szx";   // saved on a ZX Spectrum 128K

class SnapshotLauncher_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_pentagon, nullptr);
        _request.emulatorId = _pentagon->GetId();
        _request.path = TestPathHelper::GetTestDataPath(kSzx128);
    }
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    Emulator* _pentagon = nullptr;
    SnapshotLoadRequest _request;
};
}  // namespace

TEST_F(SnapshotLauncher_Test, AnSzxOfAnotherModelIsRefusedByDefault)
{
    const SnapshotLoadResult result = SnapshotLauncher::Load(_request);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.modelMismatch);
    EXPECT_FALSE(result.modelSwitched);
    EXPECT_EQ(result.requiredModel, "128k");
    EXPECT_NE(result.message.find("128k"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("Pentagon"), std::string::npos) << "it names the running machine too: " << result.message;
    EXPECT_NE(result.message.find("switch_model"), std::string::npos) << "and the way to switch: " << result.message;
    EXPECT_EQ(EmulatorManager::GetInstance()->GetEmulatorIds().size(), 1u) << "nothing was replaced";
}

TEST_F(SnapshotLauncher_Test, AskedToSwitchTheMachineIsReplacedAndTheFileLoads)
{
    _request.switchModel = true;
    const SnapshotLoadResult result = SnapshotLauncher::Load(_request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.modelSwitched);
    ASSERT_NE(result.emulator, nullptr);
    EXPECT_EQ(result.emulator->GetContext()->config.mem_model, MM_SPECTRUM128);
    EXPECT_NE(result.emulator->GetId(), result.previousEmulatorId);
    EXPECT_FALSE(result.report.refused);
}

TEST_F(SnapshotLauncher_Test, AskedNotToSwitchItIsRefused)
{
    _request.switchModel = false;
    const SnapshotLoadResult result = SnapshotLauncher::Load(_request);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.modelMismatch);
}

// The configuration can make switching the default
TEST_F(SnapshotLauncher_Test, TheConfigurationSettingMakesSwitchingTheDefault)
{
    _pentagon->GetContext()->config.snapshot_switch_model = true;
    const SnapshotLoadResult result = SnapshotLauncher::Load(_request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.modelSwitched);

    // ...and an explicit "no" still wins over it
    SnapshotLoadRequest again = _request;
    again.emulatorId = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero)->GetId();
    EmulatorManager::GetInstance()->GetEmulator(again.emulatorId)->GetContext()->config.snapshot_switch_model = true;
    again.switchModel = false;
    EXPECT_FALSE(SnapshotLauncher::Load(again).ok);
}

TEST_F(SnapshotLauncher_Test, AnSzxOfTheRunningModelLoadsWithoutASwitch)
{
    Emulator* machine = EmulatorTestHelper::CreateStandardEmulator("128k", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(machine, nullptr);
    _request.emulatorId = machine->GetId();
    const SnapshotLoadResult result = SnapshotLauncher::Load(_request);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.modelSwitched);
    EXPECT_FALSE(result.modelMismatch);
}

TEST_F(SnapshotLauncher_Test, WhatAFileNeeds)
{
    SnapshotLauncher::Need need;
    std::string error;

    ASSERT_TRUE(SnapshotLauncher::NeedOf(_request.path, MM_PENTAGON, 128, need, error)) << error;
    EXPECT_TRUE(need.differs);
    EXPECT_FALSE(need.programOnly) << "an SZX is a snapshot of a machine, not a program that runs on one machine only";
    EXPECT_EQ(need.description, "ZX-Spectrum 128k");
    ASSERT_TRUE(SnapshotLauncher::NeedOf(_request.path, MM_SPECTRUM128, 128, need, error));
    EXPECT_FALSE(need.differs);

    ASSERT_TRUE(SnapshotLauncher::NeedOf(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"), MM_PENTAGON, 128, need, error));
    EXPECT_TRUE(need.model.empty()) << "an SNA loads on any machine";
    EXPECT_FALSE(need.differs);

    // A NextZXOS .snx is made for the Next (the name alone decides; the file is not read)
    ASSERT_TRUE(SnapshotLauncher::NeedOf("program.SNX", MM_PENTAGON, 128, need, error));
    EXPECT_EQ(need.model, "NEXT");
    EXPECT_TRUE(need.programOnly);
    EXPECT_TRUE(need.differs);

    EXPECT_FALSE(SnapshotLauncher::NeedOf("missing.szx", MM_PENTAGON, 128, need, error));
    EXPECT_FALSE(error.empty());
}

namespace
{
/// A 48K and the snapshots that need more: the plan refuses them, the launcher names the machine
class SnapshotLauncher48_Test : public SnapshotLauncher_Test
{
protected:
    void SetUp() override
    {
        _spectrum48 = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_spectrum48, nullptr);
        _request.emulatorId = _spectrum48->GetId();
    }

    SnapshotLoadResult LoadOn48(const std::string& file, std::optional<bool> switchModel)
    {
        _request.path = TestPathHelper::GetTestDataPath(file);
        _request.switchModel = switchModel;
        return SnapshotLauncher::Load(_request);
    }

    Emulator* _spectrum48 = nullptr;
};
}  // namespace

TEST_F(SnapshotLauncher48_Test, A128kZ80IsRefusedByDefaultWithTheMachineItNeeds)
{
    const SnapshotLoadResult result = LoadOn48("loaders/z80/libspectrum/synth-128.z80", std::nullopt);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.modelMismatch);
    EXPECT_EQ(result.requiredModel, "128k");
    EXPECT_NE(result.message.find("switch_model"), std::string::npos) << result.message;
    EXPECT_EQ(EmulatorManager::GetInstance()->GetEmulatorIds().size(), 1u) << "nothing was replaced";
}

TEST_F(SnapshotLauncher48_Test, A128kZ80AskedToSwitchLoadsOnTheMachineItWasMadeOn)
{
    const struct
    {
        const char* file;
        MEM_MODEL model;
    } cases[] = {{"loaders/z80/libspectrum/synth-128.z80", MM_SPECTRUM128},
                 {"loaders/z80/libspectrum/synth-pentagon.z80", MM_PENTAGON},
                 {"loaders/z80/libspectrum/synth-plus3.z80", MM_PLUS3}};
    for (const auto& c : cases)
    {
        SetUp();
        const SnapshotLoadResult result = LoadOn48(c.file, true);
        ASSERT_TRUE(result.ok) << c.file << ": " << result.message;
        EXPECT_TRUE(result.modelSwitched) << c.file;
        ASSERT_NE(result.emulator, nullptr);
        EXPECT_EQ(result.emulator->GetContext()->config.mem_model, c.model) << c.file;
        EXPECT_FALSE(result.report.refused) << c.file;
    }
}

TEST_F(SnapshotLauncher48_Test, A128kSnaAskedToSwitchLoadsOnA128k)
{
    const SnapshotLoadResult result = LoadOn48("loaders/sna/action.sna", true);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.modelSwitched);
    EXPECT_EQ(result.emulator->GetContext()->config.mem_model, MM_SPECTRUM128);
}

TEST_F(SnapshotLauncher48_Test, A48kZ80LoadsWithoutASwitch)
{
    const SnapshotLoadResult result = LoadOn48("loaders/z80/newbench.z80", true);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.modelSwitched);
    EXPECT_TRUE(result.requiredModel.empty());
}
