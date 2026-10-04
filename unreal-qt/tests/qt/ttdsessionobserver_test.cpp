// The UI's TTD observers (the toolbar's "Capturing" tooltip, the TTD widget's
// telemetry) poll an instance's time travel session from the UI thread while
// another thread records, evicts, stops and invalidates it, and automation may
// remove the instance outright (2026-10-03 crash: the tooltip timer's
// GetSessionInfo -> GetHeapBreakdown walked the timeline and read a checkpoint
// freed under it after a TTD stop; docs/inprogress/2026-10-03-qt-ttd-heap-crash).
//
// The UI reads through TtdSessionObserver: a context lease plus the manager's
// published snapshot. Here the UI thread hammers that read - and the tooltip
// built from it - while an automation thread drives the machine through
// recording cycles; then the instance is removed while the UI keeps reading.

#include <QCoreApplication>
#include <atomic>
#include <gtest/gtest.h>
#include <thread>

#include "_helpers/testwaithelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ttdsessionobserver.h"

class TtdSessionObserver_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    /// One UI-side poll, as ToolBarManager::updateActiveTooltips does it; false on a torn snapshot
    static bool PollLikeTheToolbar(Emulator* emulator, std::atomic<uint64_t>& reads)
    {
        const std::optional<ttd::TTDSessionInfo> info = TtdSessionObserver::Read(emulator);
        if (!info)
            return true;
        reads++;
        const QString tip = TtdSessionObserver::CaptureToolTip(*info);
        if (!tip.startsWith(QStringLiteral("Capturing")))
            return false;
        if (info->checkpointCount != 0 && info->currentEndFrame < info->sessionStartFrame)
            return false;
        // A recording always holds its baseline
        return !(info->state == ttd::TTDSessionState::Recording && info->checkpointCount == 0);
    }

    EmulatorManager* _manager = nullptr;
};

// The UI thread polls while an automation thread records (with and without
// eviction from the front), stops and invalidates, cycle after cycle.
// Runtime: ~30 recorded frames on the automation thread (~0.15 s). With the old
// live read (GetSessionInfo from this thread) it crashes about one run in five;
// more cycles did not raise that rate
TEST_F(TtdSessionObserver_Test, PollWhileAutomationRecordsStopsAndInvalidates)
{
    auto emulator = _manager->CreateEmulator();  // the UI keeps its shared_ptr, as MainWindow does
    ASSERT_NE(emulator, nullptr);
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);

    constexpr int kCycles = 10;
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread automation([&] {
        for (int cycle = 0; cycle < kCycles && !failed; ++cycle)
        {
            if (!ttd->StartRecording())
            {
                failed = true;
                break;
            }
            ttd->SetHistoryLimit(cycle % 2 ? 2 : 0, 0);  // odd cycles evict checkpoints from the front
            emulator->RunNFrames(3);                     // this thread drives the machine: it records
            ttd->StopRecording();
            ttd->InvalidateSession("automation cycle");
        }
        ttd->SetHistoryLimit(0, 0);
        done = true;
    });

    std::atomic<uint64_t> reads{0};
    uint64_t torn = 0;
    while (!done.load(std::memory_order_acquire))
    {
        if (!PollLikeTheToolbar(emulator.get(), reads))
            torn++;
        QCoreApplication::processEvents();
    }
    automation.join();

    EXPECT_FALSE(failed.load()) << "StartRecording refused";
    EXPECT_EQ(torn, 0u);
    EXPECT_GT(reads.load(), 0u);

    const std::optional<ttd::TTDSessionInfo> last = TtdSessionObserver::Read(emulator.get());
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(last->checkpointCount, 0u);
}

// Automation removes the instance while the UI polls it: the removal waits for
// a poll in flight (its lease), and polls afterwards find no session
TEST_F(TtdSessionObserver_Test, PollWhileAutomationRemovesTheInstance)
{
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(2);

    std::atomic<bool> removed{false};
    std::thread remover([&] {
        _manager->RemoveEmulator(emulator->GetId());
        removed = true;
    });

    std::atomic<uint64_t> reads{0};
    uint64_t torn = 0;
    while (!removed.load(std::memory_order_acquire))
    {
        if (!PollLikeTheToolbar(emulator.get(), reads))
            torn++;
    }
    remover.join();

    EXPECT_EQ(torn, 0u);
    EXPECT_TRUE(emulator->IsReleased());
    EXPECT_FALSE(TtdSessionObserver::Read(emulator.get()).has_value());
}
