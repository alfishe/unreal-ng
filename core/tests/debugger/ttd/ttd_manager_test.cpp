/// @file ttd_manager_test.cpp
/// @brief Integration tests for TimeTravelManager (per-frame capture orchestrator).
///
/// These tests spin up a real Emulator instance (matching the SharedMemory
/// test pattern) and exercise the recording/capture path end-to-end:
///   - StartRecording produces a baseline checkpoint
///   - OnFrameBoundary appends checkpoints with correct COW semantics
///   - Dirty pages get freshly Intern'd; clean pages AddRef the previous slot
///   - Stop / Invalidate reset state correctly
///   - The timeline reflects frame_counter values from the live EmulatorState

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/ttd_dirty_tracker.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_page_store.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{
/// Wait for the emulator to reach the requested state, with a timeout.
bool WaitForState(Emulator& emu, EmulatorStateEnum target, int timeoutMs = 1000)
{
    for (int i = 0; i < timeoutMs / 10; ++i)
    {
        if (emu.GetState() == target)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return emu.GetState() == target;
}
} // anonymous namespace

// ===========================================================================
// Fixture: real Emulator instance per test
// ===========================================================================

class TimeTravelManager_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory*  _memory = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(_emulator->Init()) << "Failed to initialize emulator";

        EmulatorContext* ctx = _emulator->GetContext();
        ASSERT_NE(ctx, nullptr);
        _ttd = ctx->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr) << "TimeTravelManager was not created during Emulator::Init";
        _memory = ctx->pMemory;
        ASSERT_NE(_memory, nullptr);
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
            _emulator = nullptr;
        }
    }

    /// Helper: enable debugmode + timetravel features.
    void EnableTTD()
    {
        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }
};

// ===========================================================================
// Construction / initial state
// ===========================================================================

TEST_F(TimeTravelManager_Test, FreshManager_IsIdle)
{
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
}

TEST_F(TimeTravelManager_Test, GetSessionInfo_InitiallyEmpty)
{
    auto info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(info.checkpointCount, 0u);
    EXPECT_EQ(info.pageStoreBytes, 0u);
    EXPECT_EQ(info.sessionStartFrame, 0u);
    EXPECT_EQ(info.currentEndFrame, 0u);
    // Real heap footprint counter is present (zero before any recording).
    EXPECT_EQ(info.sessionHeapBytes, 0u);
}

// ===========================================================================
// OnFrameBoundary is a no-op when not Recording
// ===========================================================================

TEST_F(TimeTravelManager_Test, OnFrameBoundary_NoOp_WhenNotRecording)
{
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
}

// ===========================================================================
// StartRecording
// ===========================================================================

TEST_F(TimeTravelManager_Test, StartRecording_CapturesBaseline)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
    EXPECT_TRUE(_ttd->IsRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);  // Baseline

    // Baseline should have Intern'd every model RAM page (v1 strategy)
    const ttd::TTDCheckpoint* baseline = _ttd->GetCheckpoint(0);
    ASSERT_NE(baseline, nullptr);

    // modelRamPages is reported by the manager; for a 128K default machine
    // it should be 8 pages.
    uint16_t expectedPages = _ttd->GetModelRamPages();
    ASSERT_GT(expectedPages, 0u);
    EXPECT_EQ(baseline->ramPages.size(), expectedPages);

    // Every baseline page must have a real storeIndex (no NEVER_TOUCHED)
    for (uint32_t p = 0; p < expectedPages; ++p)
    {
        EXPECT_FALSE(baseline->ramPages[p].IsNeverTouched())
            << "Baseline page " << p << " should be Intern'd, not NEVER_TOUCHED";
    }
}

TEST_F(TimeTravelManager_Test, StartRecording_PageStoreGrows_ByModelRamPagesTimesPageSize)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint16_t pages = _ttd->GetModelRamPages();
    size_t expectedBytes = static_cast<size_t>(pages) * ttd::TTDPageStore::kPageSize;

    EXPECT_EQ(_ttd->GetPageStore().GetCapacityBytes(), expectedBytes);
    EXPECT_EQ(_ttd->GetPageStore().GetUsedSlots(), static_cast<uint32_t>(pages));
}

TEST_F(TimeTravelManager_Test, StartRecording_Idempotent)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);

    // Calling again should be a no-op (already Recording)
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);
}

// ===========================================================================
// OnFrameBoundary when Recording
// ===========================================================================

TEST_F(TimeTravelManager_Test, OnFrameBoundary_WhenRecording_AppendsCheckpoint)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);

    _ttd->OnFrameBoundary();
    EXPECT_EQ(_ttd->GetCheckpointCount(), 2u);

    _ttd->OnFrameBoundary();
    EXPECT_EQ(_ttd->GetCheckpointCount(), 3u);
}

TEST_F(TimeTravelManager_Test, OnFrameBoundary_CleanFrame_AddRefs_AllPages)
{
    // After StartRecording, no writes have happened, so the next OnFrameBoundary
    // should AddRef every page (no new Interns). Page store capacity stays the same.
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint32_t baselineCapacity = _ttd->GetPageStore().GetCapacity();
    uint32_t baselineUsed     = _ttd->GetPageStore().GetUsedSlots();
    uint16_t pages            = _ttd->GetModelRamPages();

    ASSERT_EQ(baselineUsed, static_cast<uint32_t>(pages));

    // Clean frame — every page shared via AddRef
    _ttd->OnFrameBoundary();

    EXPECT_EQ(_ttd->GetCheckpointCount(), 2u);
    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), baselineCapacity);  // No growth
    // Each slot now has refcount=2 (baseline + this checkpoint)
    for (uint32_t p = 0; p < pages; ++p)
    {
        EXPECT_EQ(_ttd->GetPageStore().GetRefCount(p), 2u)
            << "Page " << p << " should have refcount=2 after one clean frame";
    }
}

TEST_F(TimeTravelManager_Test, OnFrameBoundary_DirtyFrame_InternsOnlyDirtyPages)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint16_t pages = _ttd->GetModelRamPages();
    ASSERT_GE(pages, 1u);

    // Manually mark page 0 dirty through the dirty tracker
    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);
    tracker->MarkDirty(0);  // Page 0

    uint32_t capBefore = _ttd->GetPageStore().GetCapacity();

    _ttd->OnFrameBoundary();

    // One new Intern for page 0 → capacity grew by 1
    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), capBefore + 1);
    // Other pages were clean → AddRef'd, same slot index as baseline
    // Page 0 has a NEW storeIndex (= capBefore, the freshly grown slot)
    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(1);
    ASSERT_NE(cp, nullptr);
    EXPECT_NE(cp->ramPages[0].storeIndex, 0u);  // Not the baseline slot
}

TEST_F(TimeTravelManager_Test, OnFrameBoundary_MultipleDirtyPages_InternsAll)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint16_t pages = _ttd->GetModelRamPages();
    ASSERT_GE(pages, 3u);

    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);

    // Mark 3 pages dirty
    tracker->MarkDirty(0);
    tracker->MarkDirty(1);
    tracker->MarkDirty(2);

    uint32_t capBefore = _ttd->GetPageStore().GetCapacity();

    _ttd->OnFrameBoundary();

    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), capBefore + 3);
}

// ===========================================================================
// Checkpoint content correctness
// ===========================================================================

TEST_F(TimeTravelManager_Test, Checkpoint_Records_FrameCounter)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    const ttd::TTDCheckpoint* cp0 = _ttd->GetCheckpoint(0);
    ASSERT_NE(cp0, nullptr);
    uint64_t frameAtStart = cp0->time.frame;

    // Simulate frame advancing by mutating EmulatorState directly.
    // (OnFrameBoundary reads frame_counter from EmulatorState.)
    EmulatorContext* ctx = _emulator->GetContext();
    ctx->emulatorState.frame_counter = frameAtStart + 5;

    _ttd->OnFrameBoundary();

    const ttd::TTDCheckpoint* cp1 = _ttd->GetCheckpoint(1);
    ASSERT_NE(cp1, nullptr);
    EXPECT_EQ(cp1->time.frame, frameAtStart + 5);
    EXPECT_GT(cp1->time.frame, cp0->time.frame);
}

TEST_F(TimeTravelManager_Test, Checkpoint_StoresCpuState)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);

    // CPU state should be a snapshot of whatever Z80 had at capture time.
    // We don't assert exact register values (those depend on emulator init),
    // but the struct should be populated (e.g., IM is 0/1/2).
    EXPECT_LE(cp->cpu.im, 2u);
}

// ===========================================================================
// Stop / Invalidate
// ===========================================================================

TEST_F(TimeTravelManager_Test, StopRecording_RetainsHistory)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    ASSERT_EQ(_ttd->GetCheckpointCount(), 3u);

    _ttd->StopRecording();

    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    EXPECT_FALSE(_ttd->IsRecording());
    // History retained
    EXPECT_EQ(_ttd->GetCheckpointCount(), 3u);
}

TEST_F(TimeTravelManager_Test, OnFrameBoundary_NoOp_AfterStop)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->StopRecording();
    size_t before = _ttd->GetCheckpointCount();

    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();

    EXPECT_EQ(_ttd->GetCheckpointCount(), before);  // No growth
}

TEST_F(TimeTravelManager_Test, InvalidateSession_DropsAllHistory)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    ASSERT_GT(_ttd->GetCheckpointCount(), 1u);
    ASSERT_GT(_ttd->GetPageStore().GetCapacity(), 0u);

    _ttd->InvalidateSession("test");

    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), 0u);
    EXPECT_EQ(_ttd->GetPageStore().GetUsedSlots(), 0u);
}

TEST_F(TimeTravelManager_Test, InvalidateSession_Idempotent)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->InvalidateSession("first");
    _ttd->InvalidateSession("second");  // No-op on already-empty state
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
}

TEST_F(TimeTravelManager_Test, StartRecording_AfterInvalidate_StartedFresh)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    _ttd->InvalidateSession("test");

    // Restart
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);  // Only baseline again
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
}

// ===========================================================================
// Page refcount integrity through the lifecycle
// ===========================================================================

TEST_F(TimeTravelManager_Test, PageStore_NoLeaks_AfterFullLifecycle)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    uint16_t pages = _ttd->GetModelRamPages();

    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();

    // After 4 checkpoints (baseline + 3 frames), every slot's refcount
    // should be exactly 4 (one per checkpoint) IF no pages were dirty.
    // The clean AddRef path bumps refcount by 1 per frame.
    for (uint32_t p = 0; p < pages; ++p)
    {
        EXPECT_EQ(_ttd->GetPageStore().GetRefCount(p), 4u)
            << "Page " << p << " refcount should be 4 (one per checkpoint)";
    }

    // Invalidate releases everything
    _ttd->InvalidateSession("lifecycle-end");
    EXPECT_EQ(_ttd->GetPageStore().GetUsedSlots(), 0u);
}

TEST_F(TimeTravelManager_Test, Destructor_ReleasesPageStoreRefs)
{
    // Use a standalone TimeTravelManager on a separate context so we can destroy it
    // without tearing down the fixture's emulator.
    Emulator secondary(LoggerLevel::LogError);
    ASSERT_TRUE(secondary.Init());
    EmulatorContext* ctx = secondary.GetContext();
    ASSERT_NE(ctx->pTimeTravelManager, nullptr);

    FeatureManager* fm = secondary.GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);
    ctx->pMemory->UpdateFeatureCache();

    ASSERT_TRUE(ctx->pTimeTravelManager->StartRecording());
    ctx->pTimeTravelManager->OnFrameBoundary();
    ASSERT_GT(ctx->pTimeTravelManager->GetCheckpointCount(), 1u);

    // Stop the emulator so the TimeTravelManager can be cleanly destroyed on
    // Emulator::Release. The destructor must release all page refs —
    // verified indirectly by no leak sanitizer complaints.
    secondary.Stop();
    secondary.Release();
    // If we reached here without crashing, the destructor is clean.
    SUCCEED();
}

// ===========================================================================
// Session info
// ===========================================================================

TEST_F(TimeTravelManager_Test, GetSessionInfo_AfterStart_ReflectsBaseline)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    auto info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Recording);
    EXPECT_EQ(info.checkpointCount, 1u);
    EXPECT_GT(info.pageStoreBytes, 0u);
    EXPECT_GT(info.pageStoreUsedBytes, 0u);
}

TEST_F(TimeTravelManager_Test, GetSessionInfo_FrameBounds_UpdateWithCapture)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    auto info0 = _ttd->GetSessionInfo();
    EmulatorContext* ctx = _emulator->GetContext();
    ctx->emulatorState.frame_counter = info0.currentEndFrame + 7;

    _ttd->OnFrameBoundary();

    auto info1 = _ttd->GetSessionInfo();
    EXPECT_GT(info1.currentEndFrame, info0.currentEndFrame);
    EXPECT_EQ(info1.checkpointCount, info0.checkpointCount + 1);
}

// ===========================================================================
// sessionHeapBytes — real heap footprint counter (not a percentage, not an
// estimate). The COW page store always reports page_store_bytes ==
// page_store_used_bytes because it auto-grows to fit the working set, so
// those fields are useless for "how much memory is my recording using?".
// sessionHeapBytes is the real number: page-store backing vector +
// per-checkpoint metadata + journal backing + scratch buffers.
// ===========================================================================

TEST_F(TimeTravelManager_Test, SessionHeapBytes_NonZero_AfterStartRecording)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    auto info = _ttd->GetSessionInfo();
    // After StartRecording the baseline checkpoint captured all model RAM
    // pages, so the page store backing vector is non-zero. The baseline
    // checkpoint struct itself also contributes sizeof(TTDCheckpoint).
    EXPECT_GT(info.sessionHeapBytes, 0u);
    // sessionHeapBytes counts the page store backing (capacity, not just
    // live slots), so it must be >= pageStoreBytes.
    EXPECT_GE(info.sessionHeapBytes, info.pageStoreBytes);
}

TEST_F(TimeTravelManager_Test, SessionHeapBytes_GrowsWith_CheckpointCount)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    auto info0 = _ttd->GetSessionInfo();

    // Force several OnFrameBoundary calls — each appends a checkpoint
    // struct (sizeof(TTDCheckpoint) + ramPages vector capacity) which
    // directly adds to the heap footprint.
    EmulatorContext* ctx = _emulator->GetContext();
    for (int i = 0; i < 5; ++i)
    {
        ctx->emulatorState.frame_counter++;
        _ttd->OnFrameBoundary();
    }

    auto info1 = _ttd->GetSessionInfo();
    EXPECT_GT(info1.checkpointCount, info0.checkpointCount);
    // Each new checkpoint adds at minimum sizeof(TTDCheckpoint) bytes plus
    // its page-ref vector capacity (modelRamPages * sizeof(TTDPageRef)).
    // The heap counter MUST reflect that growth.
    EXPECT_GT(info1.sessionHeapBytes, info0.sessionHeapBytes)
        << "sessionHeapBytes must grow as checkpoints are captured";
}

TEST_F(TimeTravelManager_Test, SessionHeapBytes_DropsToZero_OnInvalidate)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    EmulatorContext* ctx = _emulator->GetContext();
    for (int i = 0; i < 3; ++i)
    {
        ctx->emulatorState.frame_counter++;
        _ttd->OnFrameBoundary();
    }
    ASSERT_GT(_ttd->GetSessionInfo().sessionHeapBytes, 0u);

    _ttd->InvalidateSession("test");

    auto info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.sessionHeapBytes, 0u)
        << "Invalidate must release every heap allocation the session owned";
    EXPECT_EQ(info.checkpointCount, 0u);
}
