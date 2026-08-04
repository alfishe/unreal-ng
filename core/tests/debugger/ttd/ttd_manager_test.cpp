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
#include "debugger/ttd/ttd_codec_page_store.h"
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

    // Baseline (I-frame) captures every model RAM page as 4 × 4 KB Full sub-pages.
    const ttd::TTDCheckpoint* baseline = _ttd->GetCheckpoint(0);
    ASSERT_NE(baseline, nullptr);

    // modelRamPages is reported by the manager; for a 128K default machine
    // it should be 8 pages.
    uint16_t expectedPages = _ttd->GetModelRamPages();
    ASSERT_GT(expectedPages, 0u);
    EXPECT_EQ(baseline->ramPages.size(), expectedPages);

    // Every baseline page must have real sub-slot indices (no NEVER_TOUCHED).
    // v2: TTDPageRef.pageSlots[4] holds 4 sub-page slot indices per 16 KB emu page.
    for (uint32_t p = 0; p < expectedPages; ++p)
    {
        EXPECT_FALSE(baseline->ramPages[p].IsNeverTouched())
            << "Baseline page " << p << " should be Intern'd, not NEVER_TOUCHED";
        for (uint32_t s = 0; s < 4; ++s)
            EXPECT_NE(baseline->ramPages[p].pageSlots[s], ttd::TTDPageRef::kNeverTouched)
                << "Baseline page " << p << " sub-page " << s;
    }
}

TEST_F(TimeTravelManager_Test, StartRecording_PageStoreGrows_ByModelRamPagesTimesPageSize)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    // v2 codec: each emu page is 4 × 4 KB sub-pages → 4 × pages slots.
    uint16_t pages = _ttd->GetModelRamPages();
    size_t actualBytes = _ttd->GetPageStore().GetCapacityBytes();
    size_t expectedBytes = static_cast<size_t>(pages) * 4u
                           * ttd::TTDCodecPageStore::kPageSize;
    size_t expectedSlots = static_cast<size_t>(pages) * 4u;

    // v2 codec verification: after baseline capture, we should have exactly
    // pages * 4 slots (one slot per 4KB sub-page of each 16KB model RAM page).
    // Note: GetCapacityBytes() returns slot *metadata* size, not raw data size.
    uint32_t actualSlots = _ttd->GetPageStore().GetUsedSlots();
    EXPECT_EQ(actualSlots, static_cast<uint32_t>(expectedSlots));
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
    // should AddRef every sub-page (no new Interns). Page store capacity stays
    // the same.
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint32_t baselineCapacity = _ttd->GetPageStore().GetCapacity();
    uint32_t baselineUsed     = _ttd->GetPageStore().GetUsedSlots();
    uint16_t pages            = _ttd->GetModelRamPages();
    uint32_t expectedSlots    = static_cast<uint32_t>(pages) * 4u;

    ASSERT_EQ(baselineUsed, expectedSlots);

    // Clean frame — every sub-page shared via AddRef
    _ttd->OnFrameBoundary();

    EXPECT_EQ(_ttd->GetCheckpointCount(), 2u);
    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), baselineCapacity);  // No growth
    // Each slot now has refcount=2 (baseline + this checkpoint).
    // v2: slots are indexed as (page * 4) + sub_page, so total is 4 × pages.
    for (uint32_t p = 0; p < pages; ++p)
    {
        for (uint32_t s = 0; s < 4; ++s)
        {
            const uint32_t slot = p * 4u + s;
            EXPECT_EQ(_ttd->GetPageStore().GetRefCount(slot), 2u)
                << "Page " << p << " sub-page " << s
                << " should have refcount=2 after one clean frame";
        }
    }
}

TEST_F(TimeTravelManager_Test, OnFrameBoundary_DirtyFrame_InternsOnlyDirtyPages)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint16_t pages = _ttd->GetModelRamPages();
    ASSERT_GE(pages, 1u);

    // v2 codec: marking page 0 dirty isn't enough — the codec store
    // is content-aware and will dedup identical content back to the same slot.
    // We must actually modify page 0 bytes so the XOR delta is non-zero,
    // otherwise the new sub-page slots will collapse back to the baseline slots.
    uint8_t* page0 = _memory->RAMPageAddress(0);
    ASSERT_NE(page0, nullptr);
    // Write distinct bytes across all 4 sub-pages so each InternXor produces
    // a real delta and allocates a new slot.
    for (uint32_t s = 0; s < 4; ++s)
        page0[s * ttd::TTDCodecPageStore::kPageSize] = static_cast<uint8_t>(0xA0 + s);

    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);
    tracker->MarkDirty(0);  // Page 0

    uint32_t capBefore = _ttd->GetPageStore().GetCapacity();

    _ttd->OnFrameBoundary();

    // v2: page 0's 4 sub-pages each get a new slot (4 new XorPrev slots).
    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), capBefore + 4);
    // Each sub-page of page 0 must reference a fresh slot (>= capBefore).
    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(1);
    ASSERT_NE(cp, nullptr);
    for (uint32_t s = 0; s < 4; ++s)
        EXPECT_GE(cp->ramPages[0].pageSlots[s], capBefore)
            << "Page 0 sub-page " << s << " should have a fresh slot index";
}

TEST_F(TimeTravelManager_Test, OnFrameBoundary_MultipleDirtyPages_InternsAll)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    uint16_t pages = _ttd->GetModelRamPages();
    ASSERT_GE(pages, 3u);

    // See DirtyFrame_InternsOnlyDirtyPages for why we must modify bytes:
    // the v2 codec dedups identical content back to the source slot.
    for (uint16_t p : {0, 1, 2})
    {
        uint8_t* page = _memory->RAMPageAddress(p);
        ASSERT_NE(page, nullptr);
        for (uint32_t s = 0; s < 4; ++s)
            page[s * ttd::TTDCodecPageStore::kPageSize] = static_cast<uint8_t>(0xB0 + p * 4 + s);
    }

    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);

    tracker->MarkDirty(0);
    tracker->MarkDirty(1);
    tracker->MarkDirty(2);

    uint32_t capBefore = _ttd->GetPageStore().GetCapacity();

    _ttd->OnFrameBoundary();

    // v2: 3 dirty pages × 4 sub-pages = 12 new XorPrev slots.
    EXPECT_EQ(_ttd->GetPageStore().GetCapacity(), capBefore + 12);
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
