/// @file timetravelcontroller_blackbox_test.cpp
/// @brief The black box's settings change from the UI's thread while the machine runs (unreal-qt's Always Record):
/// SetBlackBox and SetBlackBoxMinutes park the machine for the change, as every other control operation does, so a
/// restart due at a frame boundary never reads them half-written, and the machine runs on afterwards.
///
/// The machine runs on its own thread at real speed for some frames: over the 50 ms budget (~200 ms: each change
/// parks the machine until its frame ends). The data race itself is not an assertion here: ThreadSanitizer would
/// report it on this test without the parking.

#include <gtest/gtest.h>

#include <chrono>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class TimeTravelController_BlackBox_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool RunsOn(uint64_t frames)
    {
        const uint64_t from = _context->emulatorState.frame_counter;
        return TestWait::For([&] { return _context->emulatorState.frame_counter >= from + frames; },
                             std::chrono::milliseconds(5000));
    }
};

TEST_F(TimeTravelController_BlackBox_Test, SettingsChangedWhileTheMachineRunsParkItAndLetItRunOn)
{
    _ttd->SetBlackBox(true, 1);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->StartAsync();
    ASSERT_TRUE(RunsOn(3));

    // The UI toggles Always Record and its window while the machine records and reaches its boundaries
    for (int i = 0; i < 5; ++i)
    {
        _ttd->SetBlackBoxMinutes(2 + i % 3);
        _ttd->SetBlackBox(i % 2 == 0, 1);
        EXPECT_FALSE(_emulator->IsPaused()) << "parked for the change only";
    }
    ASSERT_TRUE(RunsOn(2));

    // Off while it records: stopped, and no new session starts afterwards
    _ttd->SetBlackBox(true, 1);
    ASSERT_TRUE(RunsOn(2));
    _ttd->StopRecording();
    _ttd->SetBlackBox(false);
    EXPECT_FALSE(_ttd->IsBlackBox());
    ASSERT_TRUE(RunsOn(3));
    EXPECT_FALSE(_ttd->IsRecording()) << "nothing restarts a black box that was switched off";
    _emulator->Pause();
}
