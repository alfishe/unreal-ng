/// @file timetravelmanager_sessioninfo_test.cpp
/// @brief The session summary (TimeTravelManager::GetSessionInfo,
/// GetLatestSessionInfo) read from another thread while a recording runs on
/// the emulation thread. The toolbar's tooltip did this every 200 ms and
/// walked the checkpoint list while the recording appended to it and dropped
/// its oldest entries (the history limit): a crash in GetHeapBreakdown.
///
/// Runs the emulator for half a second of real time (frames at 50 per
/// second, the recording's speed): slower than the 50 ms guideline, the
/// race needs frames to pass while the summary is read.

#include <gtest/gtest.h>

#include <chrono>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

TEST(TimeTravelManager_SessionInfo_Test, ASummaryReadOnAnotherThreadWhileRecording)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    emulator->GetContext()->pMemory->UpdateFeatureCache();
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    ttd->SetHistoryLimit(4, 0);   // every frame appends one checkpoint and drops the oldest
    ASSERT_TRUE(ttd->StartRecording());
    emulator->StartAsync();
    // Running, and past the history limit (checkpoints are being dropped); a
    // loaded host takes a while to bring the thread up
    const uint64_t startFrame = emulator->GetContext()->emulatorState.frame_counter;
    ASSERT_TRUE(TestWait::For(
        [&] { return emulator->IsRunning() && emulator->GetContext()->emulatorState.frame_counter > startFrame + 8; },
        std::chrono::seconds(10)));

    // This thread is not the emulation thread: every read crosses threads
    size_t reads = 0;
    uint64_t lastEnd = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < until)
    {
        const ttd::TTDSessionInfo fresh = ttd->GetSessionInfo();
        const ttd::TTDSessionInfo latest = ttd->GetLatestSessionInfo();
        for (const ttd::TTDSessionInfo* info : {&fresh, &latest})
        {
            ASSERT_EQ(info->state, ttd::TTDSessionState::Recording);
            ASSERT_GE(info->checkpointCount, 1u);
            ASSERT_LE(info->checkpointCount, 5u) << "the history limit holds";
            ASSERT_LE(info->sessionStartFrame, info->currentEndFrame);
            ASSERT_GT(info->sessionHeapBytes, 0u);
        }
        ASSERT_GE(fresh.currentEndFrame, lastEnd) << "a later read never shows an earlier frame";
        lastEnd = fresh.currentEndFrame;
        reads += 2;
    }
    EXPECT_GT(reads, 20u);
    // Computed by the recording thread at frame boundaries, never by this one
    // while the timeline changes (the race does not show on every run; this does)
    EXPECT_GT(ttd->SessionInfoPublications(), 0u);
    EXPECT_GT(lastEnd, 0u);

    emulator->Stop();
    ttd->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
}
