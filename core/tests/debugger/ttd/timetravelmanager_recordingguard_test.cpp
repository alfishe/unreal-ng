/// @file timetravelmanager_recordingguard_test.cpp
/// @brief B9: while TTD records, actions that would drop or corrupt the
/// recording are refused with a reason; after StopRecording they work again
/// and the session status says why the history was dropped.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/stringhelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

using ttd::TTDGuardedAction;

class TimeTravelManager_RecordingGuard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _ttd = _emulator->GetContext()->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    void StartRecordingWithHistory()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        ASSERT_GT(_ttd->GetCheckpointCount(), 0u);
    }

    // A refused action must leave the recording exactly as it was
    void ExpectRecordingIntact(size_t checkpoints)
    {
        EXPECT_TRUE(_ttd->IsRecording());
        EXPECT_EQ(_ttd->GetCheckpointCount(), checkpoints);
        EXPECT_TRUE(_ttd->GetSessionInfo().lastDropReason.empty());
    }
};

TEST_F(TimeTravelManager_RecordingGuard_Test, NothingIsGuarded_WhenNotRecording)
{
    for (TTDGuardedAction action :
         {TTDGuardedAction::LoadSnapshot, TTDGuardedAction::LoadTape, TTDGuardedAction::LoadDisk,
          TTDGuardedAction::CreateDisk, TTDGuardedAction::LoadRom, TTDGuardedAction::Invalidate,
          TTDGuardedAction::DisableTimeTravel, TTDGuardedAction::DisableDebugMode,
          TTDGuardedAction::ChangeWriteJournal})
    {
        EXPECT_TRUE(_ttd->RecordingGuard(action).empty()) << static_cast<int>(action);
    }
}

TEST_F(TimeTravelManager_RecordingGuard_Test, EveryGuardedActionHasAReason_WhileRecording)
{
    StartRecordingWithHistory();
    for (TTDGuardedAction action :
         {TTDGuardedAction::LoadSnapshot, TTDGuardedAction::LoadTape, TTDGuardedAction::LoadDisk,
          TTDGuardedAction::CreateDisk, TTDGuardedAction::LoadRom, TTDGuardedAction::Invalidate,
          TTDGuardedAction::DisableTimeTravel, TTDGuardedAction::DisableDebugMode,
          TTDGuardedAction::ChangeWriteJournal})
    {
        const std::string reason = _ttd->RecordingGuard(action);
        EXPECT_NE(StringHelper::ToLower(reason).find("stop the recording first"), std::string::npos) << reason;
        EXPECT_EQ(_emulator->RecordingGuard(action), reason);
    }
}

TEST_F(TimeTravelManager_RecordingGuard_Test, MediaLoadsAreRefused_WhileRecording)
{
    StartRecordingWithHistory();
    const size_t checkpoints = _ttd->GetCheckpointCount();

    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    ExpectRecordingIntact(checkpoints);

    EXPECT_FALSE(_emulator->LoadTape(TestPathHelper::GetTestDataPath("loaders/tap/action.tap")));
    ExpectRecordingIntact(checkpoints);

    std::string error;
    EXPECT_FALSE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd"), 0, &error));
    EXPECT_EQ(error, _ttd->RecordingGuard(TTDGuardedAction::LoadDisk));
    ExpectRecordingIntact(checkpoints);

    error.clear();
    EXPECT_FALSE(_emulator->CreateBlankDisk(0, Emulator::BlankDiskFormat::Auto, 0, 0, &error));
    EXPECT_EQ(error, _ttd->RecordingGuard(TTDGuardedAction::CreateDisk));
    ExpectRecordingIntact(checkpoints);
}

TEST_F(TimeTravelManager_RecordingGuard_Test, RomLoadIsRefused_WithoutPausingTheMachine)
{
    StartRecordingWithHistory();
    const size_t checkpoints = _ttd->GetCheckpointCount();
    const bool pausedBefore = _emulator->IsPaused();

    EXPECT_FALSE(_emulator->LoadROM("any.rom"));
    EXPECT_EQ(_emulator->IsPaused(), pausedBefore);
    ExpectRecordingIntact(checkpoints);
}

TEST_F(TimeTravelManager_RecordingGuard_Test, SwitchingTtdFeaturesOffIsRefused_WhileRecording)
{
    StartRecordingWithHistory();

    EXPECT_FALSE(_fm->setFeature(Features::kTimeTravel, false));
    EXPECT_FALSE(_fm->setFeature(Features::kDebugMode, false));
    EXPECT_TRUE(_fm->isEnabled(Features::kTimeTravel));
    EXPECT_TRUE(_fm->isEnabled(Features::kDebugMode));
    EXPECT_EQ(_fm->refusalReason(Features::kTimeTravel, false),
              _ttd->RecordingGuard(TTDGuardedAction::DisableTimeTravel));
    EXPECT_EQ(_fm->refusalReason(Features::kDebugMode, false),
              _ttd->RecordingGuard(TTDGuardedAction::DisableDebugMode));
    EXPECT_TRUE(_ttd->IsRecording());

    _ttd->StopRecording();
    EXPECT_TRUE(_fm->refusalReason(Features::kTimeTravel, false).empty());
    EXPECT_TRUE(_fm->setFeature(Features::kTimeTravel, false));
}

TEST_F(TimeTravelManager_RecordingGuard_Test, WriteJournalModeIsFixed_WhileRecording)
{
    StartRecordingWithHistory();
    const bool mode = _ttd->GetEnableWriteJournal();

    EXPECT_TRUE(_ttd->SetEnableWriteJournal(mode));  // re-selecting is not a change
    EXPECT_FALSE(_ttd->SetEnableWriteJournal(!mode));
    EXPECT_EQ(_ttd->GetEnableWriteJournal(), mode);

    _ttd->StopRecording();
    EXPECT_TRUE(_ttd->SetEnableWriteJournal(!mode));
    EXPECT_EQ(_ttd->GetEnableWriteJournal(), !mode);
}

TEST_F(TimeTravelManager_RecordingGuard_Test, ActionsWorkAfterStop_AndStatusSaysWhyHistoryWasDropped)
{
    StartRecordingWithHistory();
    _ttd->StopRecording();
    EXPECT_GT(_ttd->GetCheckpointCount(), 0u);  // stop keeps the history

    EXPECT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
    EXPECT_EQ(_ttd->GetSessionInfo().lastDropReason, "snapshot-load");
}

TEST_F(TimeTravelManager_RecordingGuard_Test, DebuggerLiveHistoryIsNotProtected)
{
    // A DeZog session's rolling history: loads during a debug session keep
    // working, drop the history and say so in the status
    ASSERT_TRUE(_ttd->BeginDebuggerLiveHistory());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_ttd->IsRecording());

    EXPECT_TRUE(_ttd->RecordingGuard(TTDGuardedAction::LoadTape).empty());
    EXPECT_TRUE(_ttd->SetEnableWriteJournal(!_ttd->GetEnableWriteJournal()));
    EXPECT_TRUE(_emulator->LoadTape(TestPathHelper::GetTestDataPath("loaders/tap/action.tap")));
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
    EXPECT_EQ(_ttd->GetSessionInfo().lastDropReason, "tape-load");

    // The debugger restarts it on its next resume or step
    EXPECT_TRUE(_ttd->BeginDebuggerLiveHistory());
    EXPECT_TRUE(_ttd->IsRecording());
    _ttd->EndDebuggerLiveHistory();
}
