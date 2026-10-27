/// @file ttdreadswithoutpark_test.cpp
/// @brief A client reading the timeline's end or the bookmarks while a recording runs does not park the machine.
///
/// 2026-10-18: a companion (a Stream Deck bridge) polled GET /ttd/position 50 times a second for a live readout.
/// SessionEndPosition held a SessionOperation, which parks a recording machine: 50 holds a second, each a stall of the
/// frame (up to 222 ms with the session publish every park does), and the main loop skipped its frame wait after a
/// park at a frame boundary. The machine ran fast or slow by turns, the audio ring lost its balance (52+ FPS, the AY
/// wobbling). The reads now take what the machine's thread noted; no hold reaches the arbiter.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/executionarbiter.h"
#include "emulator/memory/memory.h"

namespace
{
class TTDReadsWithoutPark_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _emulator->GetContext()->pMemory->UpdateFeatureCache();
        _emulator->GetContext()->config.turbo_mode = false;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
        _emulator = nullptr;
    }

    uint64_t Applied() { return _emulator->GetArbiter().GetSnapshot().applied; }

    /// The loop running and recording, a bookmark at the baseline
    template <class TTD>
    void RunRecording(TTD* ttd)
    {
        ASSERT_TRUE(ttd->StartRecording());
        ASSERT_TRUE(ttd->AddBookmark(ttd->SessionEndPosition(), "here"));
        _emulator->StartAsync();
        const uint64_t& frame = _emulator->GetContext()->emulatorState.frame_counter;
        ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning() && frame > 3; }, std::chrono::seconds(10)));
    }

    /// 200 reads of the end and the bookmarks from this thread (not the machine's): no request reaches the arbiter,
    /// every end is a recorded frame start no later than the machine
    template <class TTD>
    void ReadWithoutPark(TTD* ttd)
    {
        const uint64_t& frame = _emulator->GetContext()->emulatorState.frame_counter;
        const uint64_t before = Applied();
        uint64_t lastEnd = 0;
        for (int i = 0; i < 200; i++)
        {
            const ttd::TTDTimePoint end = ttd->SessionEndPosition();
            EXPECT_GE(end.frame, lastEnd) << "the end only moves forward";
            EXPECT_LE(end.frame, frame);
            lastEnd = end.frame;
            ttd::TTDBookmark bookmark;
            EXPECT_TRUE(ttd->FindBookmark("here", bookmark));
            EXPECT_EQ(ttd->GetBookmarks().size(), 1u);
        }
        EXPECT_EQ(Applied(), before) << "a read held the CPU";
        EXPECT_GT(lastEnd, 0u);

        // The machine parked, the end read the timeline itself: the same point
        _emulator->Pause();
        ASSERT_TRUE(_emulator->WaitForPauseConfirmation(5000));
        EXPECT_EQ(ttd->SessionEndPosition().frame, ttd->GetSessionInfo().currentEndFrame);
        EXPECT_GT(Applied(), before) << "the pause itself goes through the arbiter (the counter counts)";
    }

    Emulator* _emulator = nullptr;
};
}  // namespace

// Runtime: ~150 ms, boot-bound - the loop runs ~4 Pentagon frames at real speed: a hold only exists with it running
TEST_F(TTDReadsWithoutPark_Test, EndAndBookmarksAreReadWithoutParkingTheRecordingMachine)
{
    ttd::TimeTravelController* ttd = _emulator->GetContext()->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    RunRecording(ttd);
    ReadWithoutPark(ttd);
}
