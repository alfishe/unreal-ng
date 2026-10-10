/// @file timetravelmanager_publishedinfo_test.cpp
/// @brief TimeTravelController::GetPublishedSessionInfo - the snapshot observers on
/// other threads read instead of the live session (TDD section 7.2).
///
/// 2026-10-03 crash: the Qt toolbar's tooltip timer called GetSessionInfo() on
/// the UI thread; its GetHeapBreakdown walked the timeline while another thread
/// stopped / invalidated the session, and read a freed checkpoint. Observers
/// now read the published copy, which only the thread that drives the session
/// writes. These tests pin when it is published, and that a reader on another
/// thread can poll it while the owner records, evicts, stops and invalidates.

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class TimeTravelManager_PublishedInfo_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
};

// Every session operation publishes before it returns; frame boundaries
// publish only once an observer asked (no cost for a session nobody watches)
TEST_F(TimeTravelManager_PublishedInfo_Test, PublishedOnOperationsAndOnRequest)
{
    EXPECT_EQ(_ttd->GetPublishedSessionInfo().state, ttd::TTDSessionState::Idle);

    ASSERT_TRUE(_ttd->StartRecording());
    ttd::TTDSessionInfo published = _ttd->GetPublishedSessionInfo();  // also asks for the next one
    EXPECT_EQ(published.state, ttd::TTDSessionState::Recording);
    EXPECT_EQ(published.checkpointCount, 1u) << "the baseline, published by StartRecording";
    EXPECT_GT(published.sessionHeapBytes, 0u);

    // Asked: the next frame boundary publishes (the first one is never inside the interval)
    _emulator->RunNFrames(1);
    published = _ttd->GetPublishedSessionInfo();
    EXPECT_EQ(published.checkpointCount, 2u);

    // GetSessionInfo (the owner's live read) publishes what it computed
    _emulator->RunNFrames(2);
    const ttd::TTDSessionInfo live = _ttd->GetSessionInfo();
    published = _ttd->GetPublishedSessionInfo();
    EXPECT_EQ(live.checkpointCount, 4u);
    EXPECT_EQ(published.checkpointCount, live.checkpointCount);
    EXPECT_EQ(published.currentEndFrame, live.currentEndFrame);
    EXPECT_EQ(published.sessionHeapBytes, live.sessionHeapBytes);

    _ttd->StopRecording();
    published = _ttd->GetPublishedSessionInfo();
    EXPECT_EQ(published.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(published.checkpointCount, 4u) << "a stop keeps the history";

    _ttd->InvalidateSession("test");
    published = _ttd->GetPublishedSessionInfo();
    EXPECT_EQ(published.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(published.checkpointCount, 0u);
    EXPECT_EQ(published.sessionHeapBytes, _ttd->GetSessionInfo().sessionHeapBytes);
}

// Nobody asked: recording frames leave the snapshot alone (and cost nothing)
TEST_F(TimeTravelManager_PublishedInfo_Test, FrameBoundaryPublishesOnlyWhenAsked)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3);
    // The first read returns what StartRecording published: no frame since asked
    EXPECT_EQ(_ttd->GetPublishedSessionInfo().checkpointCount, 1u);
    _ttd->StopRecording();
}

// The history limit set from another thread while the loop records is applied by
// the machine's thread, not by the caller (no eviction beside the capture)
TEST_F(TimeTravelManager_PublishedInfo_Test, HistoryLimitAppliesAtOnceWithoutLoop)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(6);
    _ttd->SetHistoryLimit(3, 0);  // synchronous mode: the caller drives the machine
    // Published at once, the same as the session: the engine trims whole segments, so the six frames recorded
    // before the limit (one segment) stay until the segments recorded after it cover the window (decision 41)
    const ttd::TTDSessionInfo published = _ttd->GetPublishedSessionInfo();
    EXPECT_EQ(published.historyLimitFrames, 3u);
    EXPECT_EQ(published.checkpointCount, _ttd->GetSessionInfo().checkpointCount);
    _emulator->RunNFrames(12);
    const ttd::TTDSessionInfo later = _ttd->GetSessionInfo();
    EXPECT_GT(later.evictedCheckpoints, 0u) << "the segments recorded under the limit drop the older ones";
    EXPECT_LE(later.checkpointCount, 3u + 1u);
    _ttd->StopRecording();
}

// A reader thread polls the snapshot (and the atomic state) all through a
// recording that grows, evicts, stops and is invalidated on the owner's thread.
// Before the fix the reader would have walked the live timeline and freed
// checkpoints; now every snapshot it sees is whole.
// Runtime: ~30 recorded Pentagon frames on the owner thread.
TEST_F(TimeTravelManager_PublishedInfo_Test, ReaderThreadPollsWhileOwnerRecordsStopsAndInvalidates)
{
    std::atomic<bool> done{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> torn{0};

    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire))
        {
            const ttd::TTDSessionInfo info = _ttd->GetPublishedSessionInfo();
            (void)_ttd->IsRecording();
            if (info.checkpointCount != 0 && info.currentEndFrame < info.sessionStartFrame)
                torn++;
            if (info.checkpointCount == 0 && info.state == ttd::TTDSessionState::Recording)
                torn++;  // a recording always holds its baseline
            reads++;
        }
    });

    for (int cycle = 0; cycle < 10; ++cycle)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _ttd->SetHistoryLimit(cycle % 2 ? 2 : 0, 0);  // odd cycles evict from the front
        _emulator->RunNFrames(3);
        (void)_ttd->GetSessionInfo();
        _ttd->StopRecording();
        _ttd->InvalidateSession("test cycle");
    }
    _ttd->SetHistoryLimit(0, 0);

    // The reader got some snapshots in (never ending before it read once)
    EXPECT_TRUE(TestWait::For([&] { return reads.load() > 0; }));
    done = true;
    reader.join();

    EXPECT_EQ(torn.load(), 0u);
    const ttd::TTDSessionInfo last = _ttd->GetPublishedSessionInfo();
    EXPECT_EQ(last.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(last.checkpointCount, 0u);
}

// The automation surfaces (WebAPI ttd/status and the other TTD endpoints, CLI,
// Lua, Python, GDB) read through ReadSessionInfo() and the guarded session
// queries, on their own threads, while the machine records on its loop thread
// and another control thread starts, evicts, bookmarks, stops and clears.
// ReadSessionInfo never pauses the machine; the guarded queries park it for
// their run. Runtime: real-time frames on the loop thread plus the pause handshakes
// of every start, stop and parked query (~1 s) - a
// recording runs at 1x (the recording lock), there is no faster way to have
// the loop thread capture beside the readers.
TEST_F(TimeTravelManager_PublishedInfo_Test, AutomationReadsWhileTheLoopRecords)
{
    _emulator->EnableTurboMode(false);
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->GetState() == StateRun; }));

    std::atomic<bool> done{false};
    std::atomic<uint64_t> statusReads{0};
    std::atomic<uint64_t> queries{0};
    std::atomic<uint64_t> torn{0};
    std::thread automation([&] {
        uint64_t lastQueryFrame = ~uint64_t(0);
        while (!done.load(std::memory_order_acquire))
        {
            // GET ttd/status (and the CLI / Lua / Python / GDB status reads)
            const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
            if (info.checkpointCount != 0 && info.currentEndFrame < info.sessionStartFrame)
                torn++;
            if (info.state == ttd::TTDSessionState::Recording && info.checkpointCount == 0)
                torn++;
            statusReads++;

            // The list / query endpoints park the machine for their run: at most
            // once per emulated frame, or a client spinning on them would starve it
            const uint64_t frame = _context->emulatorState.frame_counter;
            if (frame != lastQueryFrame)
            {
                lastQueryFrame = frame;
                (void)_ttd->GetBookmarks();
                (void)_ttd->GetExternalEvents().SnapshotEvents();
                (void)_ttd->SessionEndPosition();
                (void)_ttd->QueryCoverageSummary(0, info.currentEndFrame);
                queries++;
            }
        }
    });

    const auto waitFrames = [this](uint64_t frames) {
        const uint64_t target = _context->emulatorState.frame_counter + frames;
        return TestWait::For([&] { return _context->emulatorState.frame_counter >= target; },
                             std::chrono::milliseconds(5000));
    };
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        ASSERT_TRUE(waitFrames(2));
        _ttd->SetHistoryLimit(cycle % 2 ? 1 : 0, 0);  // odd cycles evict beside the capture
        (void)_ttd->AddBookmark(_ttd->CurrentPosition(), "cycle" + std::to_string(cycle));
        ASSERT_TRUE(waitFrames(1));
        _ttd->StopRecording();
        _ttd->InvalidateSession("test cycle");
    }
    _ttd->SetHistoryLimit(0, 0);

    EXPECT_TRUE(TestWait::For([&] { return queries.load() > 0; }));
    done = true;
    automation.join();
    _emulator->Stop();

    EXPECT_EQ(torn.load(), 0u);
    EXPECT_GT(statusReads.load(), 0u);
    const ttd::TTDSessionInfo last = _ttd->ReadSessionInfo();
    EXPECT_EQ(last.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(last.checkpointCount, 0u);
}
