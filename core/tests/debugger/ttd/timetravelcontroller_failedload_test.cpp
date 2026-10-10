/// @file timetravelcontroller_failedload_test.cpp
/// @brief A tape or disk that fails to load changes nothing, the TTD session included: the session the old media
/// described ends only once the new medium is in (MediaManager::Insert). Before, Emulator::LoadTape / LoadDisk ended
/// it ahead of the insert, so a broken file had already stopped the black box or dropped a kept history.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class TimeTravelController_FailedLoad_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        _ttd = context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    static std::string Garbage(const std::string& name)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream(path, std::ios::binary) << "this is not a medium of any kind";
        return path;
    }
};

TEST_F(TimeTravelController_FailedLoad_Test, ABrokenTapeKeepsTheBlackBoxRecording)
{
    _ttd->SetBlackBox(true, 1);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    const size_t checkpoints = _ttd->GetCheckpointCount();

    std::string error;
    ASSERT_FALSE(_emulator->LoadTape(Garbage("broken.tzx"), &error));
    EXPECT_TRUE(_ttd->IsRecording()) << "the black box goes on: " << error;
    EXPECT_EQ(_ttd->GetCheckpointCount(), checkpoints);
    EXPECT_TRUE(_ttd->GetSessionInfo().lastStopReason.empty()) << _ttd->GetSessionInfo().lastStopReason;

    // A tape that loads ends the session, named after the load
    const std::string tape = (TestPathHelper::FindProjectRoot() / "testdata/contention/rak-timing-test/timing.tap").string();
    ASSERT_TRUE(_emulator->LoadTape(tape, &error)) << error;
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "tape-load");
}

TEST_F(TimeTravelController_FailedLoad_Test, ABrokenDiskKeepsTheHistory)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    ASSERT_TRUE(_ttd->HasHistory());

    std::string error;
    ASSERT_FALSE(_emulator->LoadDisk(Garbage("broken.scl"), 0, &error));
    EXPECT_TRUE(_ttd->HasHistory()) << "the history stays browsable: " << error;
}
