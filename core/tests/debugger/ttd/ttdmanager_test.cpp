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
#include <sstream>
#include <thread>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttddirtytracker.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcodecpagestore.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"

namespace
{
/// Wait for the emulator to reach the requested state, with a timeout.
bool WaitForState(Emulator& emu, EmulatorStateEnum target, int timeoutMs = 1000)
{
    TestWait::For([&emu, target] { return emu.GetState() == target; },
                  std::chrono::milliseconds(timeoutMs));
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
        // Capture tests assert the legacy TurboSound peripheral; the shipped
        // default slot is FM now, so stage the default machine's ini with AY
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _emulator->SetCustomConfigPath(
            EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::AY));
        ASSERT_TRUE(_emulator->Init()) << "Failed to initialize emulator";

        EmulatorContext* ctx = _emulator->GetContext();
        ASSERT_NE(ctx, nullptr);
        _ttd = ctx->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr) << "TimeTravelManager was not created during Emulator::Init";
        _ttd->SetEnableWriteJournal(true);   // these tests use the write journal (off by default, D40)
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
    // Use XOR with existing value to guarantee a change regardless of prior content.
    for (uint32_t s = 0; s < 4; ++s)
    {
        size_t offset = s * ttd::TTDCodecPageStore::kPageSize;
        page0[offset] ^= 0xFF;  // Flip all bits - guaranteed different from baseline
    }

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
    // Use XOR to guarantee a change regardless of prior RAM content.
    for (uint16_t p : {0, 1, 2})
    {
        uint8_t* page = _memory->RAMPageAddress(p);
        ASSERT_NE(page, nullptr);
        for (uint32_t s = 0; s < 4; ++s)
        {
            size_t offset = s * ttd::TTDCodecPageStore::kPageSize;
            page[offset] ^= 0xFF;  // Flip all bits - guaranteed different from baseline
        }
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
    // cp0 is dangling here: _timeline is a std::vector and OnFrameBoundary() appended to it,
    // so compare against the value captured before the mutation.
    EXPECT_GT(cp1->time.frame, frameAtStart);
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

TEST_F(TimeTravelManager_Test, SessionHeapBytes_CountsPagePayloadsAndTheJournal)
{
    // B7: the page payloads are separate heap blocks from the slot table, and
    // the write journal commits ring chunks as the session writes; both are
    // the session's memory
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);

    const auto info = _ttd->GetSessionInfo();
    const size_t payloads = _ttd->GetPageStore().GetLivePayloadBytes();
    const size_t journal = _ttd->GetWriteJournal() ? _ttd->GetWriteJournal()->HeapBytes() : 0;
    ASSERT_GT(payloads, 0u);
    ASSERT_GT(journal, 0u);
    EXPECT_GE(info.sessionHeapBytes, _ttd->GetPageStore().GetCapacityBytes() + payloads + journal);
    _ttd->StopRecording();
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

// ===========================================================================
// Peripheral registry wiring
// ===========================================================================

/// Every device — core and model-specific alike — reaches a checkpoint through
/// TTDPeripheralRegistry. Nothing else registers them, so if this regresses a
/// recording silently loses peripheral state with no error anywhere.
TEST_F(TimeTravelManager_Test, StartRecording_RegistersConnectedCoreDevices)
{
    EnableTTD();

    EXPECT_EQ(_ttd->GetPeripheralRegistry().Count(), 0u)
        << "registrations are session-scoped, not eager";

    ASSERT_TRUE(_ttd->StartRecording());

    EmulatorContext* ctx = _emulator->GetContext();

    // Registration mirrors what the machine actually has: a device the model
    // does not provide must leave no entry at all, which is the whole point of
    // a registry over fixed per-device slots.
    EXPECT_EQ(_ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::Tape),
              ctx->pTape != nullptr);
    EXPECT_EQ(_ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::BetaDisk),
              ctx->pBetaDisk != nullptr);
    if (ctx->pSoundManager)
    {
        EXPECT_EQ(_ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::TurboSound),
                  ctx->pSoundManager->getTurboSound() != nullptr);
        EXPECT_EQ(_ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::Covox),
                  ctx->pSoundManager->getCovox() != nullptr);
    }
}

/// Captured checkpoints must carry a blob for every registered device, and the
/// blob must decode back to that device's declared state size.
TEST_F(TimeTravelManager_Test, Checkpoints_CarryABlobPerRegisteredDevice)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();

    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1);
    ASSERT_NE(cp, nullptr);

    const auto& registry = _ttd->GetPeripheralRegistry();
    ASSERT_GT(registry.Count(), 0u) << "nothing registered — test proves nothing";

    for (uint8_t id = 0; id < static_cast<uint8_t>(ttd::PeripheralId::Count); ++id)
    {
        const auto pid = static_cast<ttd::PeripheralId>(id);
        if (!registry.IsRegistered(pid))
        {
            EXPECT_EQ(cp->peripheralBlobs.count(id), 0u)
                << "unregistered device " << +id << " must not occupy a blob";
            continue;
        }

        ttd::TTDSerializable* device = registry.GetDevice(pid);
        ASSERT_NE(device, nullptr);
        if (device->TTDStateSize() == 0)
            continue;   // stateless device contributes nothing by design

        ASSERT_EQ(cp->peripheralBlobs.count(id), 1u)
            << "no blob for registered device '" << device->TTDDeviceName() << "'";

        const auto decoded =
            ttd::TTDPeripheralRegistry::DecodeBlob(id, cp->peripheralBlobs.at(id));
        if (device->TTDVariableSize())
            EXPECT_TRUE(!decoded.empty() && decoded.size() <= device->TTDStateSize())
                << "blob for '" << device->TTDDeviceName() << "' is empty or larger than its worst case";
        else
            EXPECT_EQ(decoded.size(), device->TTDStateSize())
                << "blob for '" << device->TTDDeviceName() << "' does not decode to its state size";
    }
}

/// Ending a session must drop every registration, including the devices owned
/// by the emulator rather than by the manager.
TEST_F(TimeTravelManager_Test, InvalidateSession_DropsAllRegistrations)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_GT(_ttd->GetPeripheralRegistry().Count(), 0u);

    _ttd->InvalidateSession("test");

    EXPECT_EQ(_ttd->GetPeripheralRegistry().Count(), 0u);
}

// ===========================================================================
// Recording acceleration lock: a recording captures the code at real speed,
// whichever path enters Recording (StartRecording, ResumeRecordingFrom) and
// until the session returns to Idle (StopRecording, InvalidateSession).
// Host speed multiplier forced to 1x and turbo mode forced off, both restored
// on release and refused while the lock is held.
// ===========================================================================

class TimeTravelManagerRecordingLock_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Core* _core = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        EmulatorContext* context = _emulator->GetContext();
        _core = context->pCore;
        _ttd = context->pTimeTravelManager;
        _ttd->SetEnableWriteJournal(true);   // these tests use the write journal (off by default, D40)
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_core, nullptr);
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_fm, nullptr);
        // Deliberately NOT pre-enabling 'timetravel': that alone engages the
        // FeatureManager gate, and these tests set speed/turbo before recording
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

    void RunFrames(unsigned n)
    {
        _emulator->RunNFrames(n, /*skipBreakpoints=*/true);
    }
};

TEST_F(TimeTravelManagerRecordingLock_Test, StartRecordingForcesHostSpeedTo1xAndStopRestoresIt)
{
    ASSERT_TRUE(_emulator->SetSpeedMultiplier(4));
    RunFrames(1);
    ASSERT_EQ(_core->GetSpeedMultiplier(), 4);

    ASSERT_TRUE(_ttd->StartRecording());

    // Applied immediately, not at the next frame: the baseline is already 1x
    EXPECT_EQ(_core->GetHostSpeedMultiplier(), 1);
    EXPECT_EQ(_core->GetSpeedMultiplier(), 1);

    _ttd->StopRecording();
    EXPECT_EQ(_core->GetHostSpeedMultiplier(), 4);
}

TEST_F(TimeTravelManagerRecordingLock_Test, SpeedChangeWhileRecordingIsRefusedAndKeepsTheSession)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    const size_t checkpoints = _ttd->GetCheckpointCount();

    EXPECT_FALSE(_emulator->SetSpeedMultiplier(4));
    EXPECT_FALSE(_core->SetSpeedMultiplier(16));  // the direct path the Qt UI used
    EXPECT_EQ(_core->GetHostSpeedMultiplier(), 1);

    // Re-selecting 1x is a no-op, not a session-invalidating change
    EXPECT_TRUE(_emulator->SetSpeedMultiplier(1));

    EXPECT_TRUE(_ttd->IsRecording());
    EXPECT_EQ(_ttd->GetCheckpointCount(), checkpoints);
}

TEST_F(TimeTravelManagerRecordingLock_Test, TurboModeIsForcedOffRefusedAndRestored)
{
    ASSERT_TRUE(_fm->setFeature(Features::kTurboMode, true));
    ASSERT_TRUE(_core->IsTurboMode());

    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_FALSE(_core->IsTurboMode());
    EXPECT_FALSE(_fm->setFeature(Features::kTurboMode, true));
    EXPECT_FALSE(_core->IsTurboMode());

    _ttd->StopRecording();
    EXPECT_TRUE(_core->IsTurboMode());
}

TEST_F(TimeTravelManagerRecordingLock_Test, ResumeRecordingFromHistoryEngagesTheLock)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    // Idle-with-history: acceleration is allowed again
    ASSERT_TRUE(_fm->setFeature(Features::kTurboMode, true));
    ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{1, 0}));
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);

    ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->CurrentPosition()));
    ASSERT_TRUE(_ttd->IsRecording());
    EXPECT_FALSE(_core->IsTurboMode());
    EXPECT_FALSE(_fm->setFeature(Features::kTurboMode, true));
    EXPECT_FALSE(_emulator->SetSpeedMultiplier(4));

    _ttd->StopRecording();
    EXPECT_TRUE(_core->IsTurboMode());
}

TEST_F(TimeTravelManagerRecordingLock_Test, RewindWhileRecordingKeepsTheLock)
{
    ASSERT_TRUE(_fm->setFeature(Features::kTurboMode, true));
    ASSERT_TRUE(_emulator->SetSpeedMultiplier(4));
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    // Passes through Detached internally; the lock holds until Idle, so no
    // restored speed/turbo leaks into the history being re-recorded
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(ttd::TTDTimePoint{1, 0}));
    ASSERT_TRUE(_ttd->IsRecording());
    EXPECT_FALSE(_core->IsTurboMode());
    EXPECT_EQ(_core->GetHostSpeedMultiplier(), 1);
    EXPECT_FALSE(_emulator->SetSpeedMultiplier(4));

    // The pre-recording settings survive the rewind
    _ttd->StopRecording();
    EXPECT_TRUE(_core->IsTurboMode());
    EXPECT_EQ(_core->GetHostSpeedMultiplier(), 4);
}

TEST_F(TimeTravelManagerRecordingLock_Test, InvalidatingARecordingReleasesTheLock)
{
    ASSERT_TRUE(_fm->setFeature(Features::kTurboMode, true));
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_FALSE(_core->IsTurboMode());

    _ttd->InvalidateSession("test");
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

    EXPECT_FALSE(_fm->isTtdRecordingActive());
    EXPECT_TRUE(_core->IsTurboMode());
    EXPECT_TRUE(_emulator->SetSpeedMultiplier(4));
}

TEST_F(TimeTravelManagerRecordingLock_Test, ShortcutsStayOffWhileReplayingAStoppedSession)
{
    ASSERT_TRUE(_fm->setFeature(Features::kFastTape, true));
    ASSERT_TRUE(_fm->setFeature(Features::kFastDisk, true));
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();
    ASSERT_TRUE(_fm->isEnabled(Features::kFastTape));  // live again after stop

    // Replay runs under replay mode before the state turns Detached: a trap
    // firing there would load the tape differently from what was recorded
    _ttd->EnterReplayMode();
    EXPECT_FALSE(_fm->isEnabled(Features::kFastTape));
    _ttd->ExitReplayMode();

    ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{1, 0}));
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
    EXPECT_FALSE(_fm->isEnabled(Features::kFastTape));
    EXPECT_FALSE(_fm->isEnabled(Features::kFastDisk));
    EXPECT_FALSE(_fm->setFeature(Features::kTurboTape, true));

    // Pacing-only turbo does not change the guest's behavior: allowed in history
    EXPECT_TRUE(_fm->setFeature(Features::kTurboMode, true));

    _ttd->InvalidateSession("test");
    EXPECT_TRUE(_fm->isEnabled(Features::kFastTape));
    EXPECT_TRUE(_fm->isEnabled(Features::kFastDisk));
}

/// Contention changes timing in both directions: a timeline replays only with the setting it was recorded with
TEST_F(TimeTravelManagerRecordingLock_Test, ContentionSwitchIsFixedForTheTimeline)
{
    ASSERT_TRUE(_fm->isEnabled(Features::kContention));
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_FALSE(_fm->setFeature(Features::kContention, false)) << "recording";
    EXPECT_TRUE(_fm->isEnabled(Features::kContention));
    EXPECT_TRUE(_fm->setFeature(Features::kContention, true)) << "setting the current value is not a change";
    RunFrames(3);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{1, 0}));
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
    EXPECT_FALSE(_fm->setFeature(Features::kContention, false)) << "positioned in history";
    EXPECT_TRUE(_core->IsContentionSwitchOn());

    _ttd->InvalidateSession("test");
    EXPECT_TRUE(_fm->setFeature(Features::kContention, false));
    EXPECT_FALSE(_core->IsContentionSwitchOn());
    EXPECT_TRUE(_fm->setFeature(Features::kContention, true));
}

TEST_F(TimeTravelManagerRecordingLock_Test, EnablingTheTimeTravelFeatureAloneLocksNothing)
{
    // The feature only arms the capture machinery; the lock belongs to a recording
    ASSERT_TRUE(_fm->setFeature(Features::kTimeTravel, true));

    EXPECT_FALSE(_fm->isTtdRecordingActive());
    EXPECT_TRUE(_fm->setFeature(Features::kFastTape, true));
    EXPECT_TRUE(_fm->isEnabled(Features::kFastTape));
    EXPECT_TRUE(_fm->setFeature(Features::kTurboMode, true));
    EXPECT_TRUE(_core->IsTurboMode());
    EXPECT_TRUE(_emulator->SetSpeedMultiplier(2));
}

// ===========================================================================
// Write journal authority: find-last may answer from the journal only when it
// took every write since the session start (current-state B3)
// ===========================================================================

class TimeTravelManagerJournal_Test : public TimeTravelManagerRecordingLock_Test
{
protected:
    /// DI; loop: INC A; LD (#C000),A; OUT (#FE),A; JP loop
    void InstallWritingProgram()
    {
        Memory* memory = _emulator->GetContext()->pMemory;
        const uint8_t program[] = {0xF3, 0x3C, 0x32, 0x00, 0xC0, 0xD3, 0xFE, 0xC3, 0x01, 0x80};
        for (uint16_t i = 0; i < sizeof(program); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        _emulator->GetZ80State()->pc = 0x8000;
        _emulator->GetZ80State()->sp = 0xFF00;
    }

    static ttd::TTDSearchQuery LastWriteToC000()
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = 0xC000;
        q.addrTo = 0xC000;
        q.access = ttd::TTDAccessType::Write;
        return q;
    }
};

TEST_F(TimeTravelManagerJournal_Test, FindLastWithoutAJournalReplays)
{
    // The feature pre-allocates a journal that then stays empty
    ASSERT_TRUE(_fm->setFeature(Features::kTimeTravel, true));
    _ttd->SetEnableWriteJournal(false);
    InstallWritingProgram();
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    const auto found = _ttd->FindLastAccess(LastWriteToC000());
    ASSERT_TRUE(found.has_value()) << "an empty journal answered \"no match\" instead of replaying";
    EXPECT_GE(found->pc, 0x8000);
    EXPECT_LE(found->pc, 0x8009);
}

TEST_F(TimeTravelManagerJournal_Test, FindLastAfterTheJournalWasSwitchedOffReplays)
{
    _ttd->SetEnableWriteJournal(true);
    InstallWritingProgram();
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    const uint64_t journalEndFrame = _ttd->CurrentPosition().frame;
    _ttd->SetEnableWriteJournal(false);  // the writes of the next frames are not journaled
    RunFrames(2);
    _ttd->StopRecording();

    // The program writes #C000 every few T-states, so the last write is in the
    // last recorded frame - not in the journal's last one
    const auto found = _ttd->FindLastAccess(LastWriteToC000());
    ASSERT_TRUE(found.has_value());
    EXPECT_GT(found->time.frame, journalEndFrame)
        << "the answer came from the journal, which stopped before the last writes";
}

/// With debug mode switched back off by a stop, replay-based queries still
/// observe accesses: the replay engages the debug memory path itself.
TEST_F(TimeTravelManagerJournal_Test, ReplayAfterAStopSeesWritesWithDebugModeOff)
{
    _ttd->SetEnableWriteJournal(false);  // force the replay path
    InstallWritingProgram();
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();
    ASSERT_FALSE(_fm->isEnabled(Features::kDebugMode)) << "precondition: the stop restored debug mode";

    const auto found = _ttd->FindLastAccess(LastWriteToC000());
    EXPECT_TRUE(found.has_value()) << "replay did not observe the program's writes";
}

/// "Resume recording from here" after a stop captures a correct history: the
/// capture flags a stop switched off come back on, so the frames recorded
/// after the resume restore the RAM they were captured from.
TEST_F(TimeTravelManagerJournal_Test, ResumeAfterAStopRecordsACorrectHistory)
{
    InstallWritingProgram();
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(6);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{3, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->CurrentPosition()));
    const size_t firstNew = _ttd->GetCheckpointCount();
    RunFrames(4);
    _ttd->StopRecording();
    ASSERT_GT(_ttd->GetCheckpointCount(), firstNew);

    Memory* memory = _emulator->GetContext()->pMemory;
    for (size_t idx = firstNew; idx < _ttd->GetCheckpointCount(); ++idx)
    {
        ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(idx));
        const Z80State* cpu = _emulator->GetZ80State();
        if (cpu->pc < 0x8001 || cpu->pc > 0x8007)
            continue;
        const uint8_t expected = static_cast<uint8_t>(cpu->pc == 0x8002 ? cpu->a - 1 : cpu->a);
        EXPECT_EQ(memory->DirectReadFromZ80Memory(0xC000), expected) << "checkpoint " << idx;
    }
}

/// A file records whether its journal is complete: one with a gap is answered
/// by replay after the load too, not by the journal it carries.
TEST_F(TimeTravelManagerJournal_Test, LoadedIncompleteJournalReplays)
{
    _ttd->SetEnableWriteJournal(true);
    InstallWritingProgram();
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    const uint64_t journalEndFrame = _ttd->CurrentPosition().frame;
    _ttd->SetEnableWriteJournal(false);
    RunFrames(2);
    _ttd->StopRecording();

    // The journal is written to a file only while journaling is on
    _ttd->SetEnableWriteJournal(true);
    std::stringstream file(std::ios::in | std::ios::out | std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(file, err)) << err;
    ASSERT_TRUE(_ttd->DeserializeSession(file, err)) << err;
    ASSERT_NE(_ttd->GetWriteJournal(), nullptr);
    ASSERT_FALSE(_ttd->GetWriteJournal()->IsEmpty()) << "precondition: the file carries a journal";

    const auto found = _ttd->FindLastAccess(LastWriteToC000());
    ASSERT_TRUE(found.has_value());
    EXPECT_GT(found->time.frame, journalEndFrame) << "the loaded session answered from its incomplete journal";
}

// ===========================================================================
// Tool edits (scripts, debugger surfaces) during a recording
// ===========================================================================

TEST_F(TimeTravelManagerRecordingLock_Test, ToolEditWhileRecordingReachesTheHistory)
{
    Memory* memory = _emulator->GetContext()->pMemory;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    const uint8_t romByte = memory->DirectReadFromZ80Memory(0x0000);

    _emulator->EditMemoryFromTool("test edit", [&] {
        memory->ToolWriteToZ80Memory(0xC000, 0x5A);
        memory->ToolWriteToZ80Memory(0x0000, static_cast<uint8_t>(romByte ^ 0xFF));  // ROM: ignored like a CPU write
    });
    RunFrames(1);
    const size_t afterEdit = _ttd->GetCheckpointCount() - 1;
    _ttd->StopRecording();

    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x0000), romByte) << "a tool write patched ROM";

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(afterEdit));
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0xC000), 0x5A) << "the checkpoint after the edit lost it";

    const auto markers = _ttd->GetExternalEvents().SnapshotEvents();
    ASSERT_EQ(markers.size(), 1u);
    EXPECT_EQ(markers.front().kind, ttd::TTDExternalEventKind::DebuggerEdit);
}

TEST_F(TimeTravelManagerRecordingLock_Test, PhysicalPageEditWhileRecordingReachesTheHistory)
{
    Memory* memory = _emulator->GetContext()->pMemory;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);

    // Page 5 is the 128K screen page, mapped at #4000 on every model
    _emulator->EditMemoryFromTool("test page edit", [&] {
        memory->RAMPageAddress(5)[0x1234] = 0xA5;
        memory->MarkRamPageEdited(5);
    });
    RunFrames(1);
    const size_t afterEdit = _ttd->GetCheckpointCount() - 1;
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(afterEdit));
    EXPECT_EQ(memory->RAMPageAddress(5)[0x1234], 0xA5);
}

TEST_F(TimeTravelManagerRecordingLock_Test, FeatureListShowsTheStateInEffect)
{
    ASSERT_TRUE(_fm->setFeature(Features::kTurboMode, true));
    ASSERT_TRUE(_ttd->StartRecording());

    bool listed = false;
    for (const FeatureManager::FeatureInfo& f : _fm->listFeatures())
    {
        if (f.id == Features::kTurboMode || f.id == Features::kFastTape)
        {
            EXPECT_FALSE(f.enabled) << f.id << " is listed on while TTD holds it off";
            listed = true;
        }
    }
    EXPECT_TRUE(listed);

    // Refused, not unknown
    EXPECT_FALSE(_fm->setFeature(Features::kTurboMode, true));
    EXPECT_TRUE(_fm->hasFeature(Features::kTurboMode));
    EXPECT_FALSE(_fm->hasFeature("nosuchfeature"));
}

// ===========================================================================
// Search window (TD-8): what part of history a backward search examined, and
// reverse-continue honoring a barrier on the coverage-index path
// ===========================================================================

class TimeTravelManagerSearchWindow_Test : public TimeTravelManagerRecordingLock_Test
{
protected:
    static constexpr uint16_t kCounter = 0x9000;  // 16-bit countdown
    static constexpr uint16_t kTarget = 0x8100;   // subroutine called while the countdown runs
    static constexpr uint16_t kStore = 0x8008;    // LD (#9000),HL - the write to the counter

    /// loop: LD HL,(#9000) / LD A,H / OR L / JR Z,loop / DEC HL / LD (#9000),HL /
    ///       CALL #8100 / JR loop;   #8100: RET.   Interrupts off.
    /// The target runs `calls` times, then never again.
    void InstallCountdown(uint16_t calls)
    {
        Memory* memory = _emulator->GetContext()->pMemory;
        const uint8_t loop[] = {0x2A, 0x00, 0x90, 0x7C, 0xB5, 0x28, 0xF9, 0x2B,
                                0x22, 0x00, 0x90, 0xCD, 0x00, 0x81, 0x18, 0xF0};
        for (uint16_t i = 0; i < sizeof(loop); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
        memory->DirectWriteToZ80Memory(kTarget, 0xC9);
        memory->DirectWriteToZ80Memory(kCounter, static_cast<uint8_t>(calls));
        memory->DirectWriteToZ80Memory(kCounter + 1, static_cast<uint8_t>(calls >> 8));
        Z80State* z80 = _emulator->GetZ80State();
        z80->pc = 0x8000;
        z80->sp = 0xBF00;
        z80->iff1 = z80->iff2 = 0;
    }

    uint16_t Counter() const
    {
        Memory* memory = _emulator->GetContext()->pMemory;
        return static_cast<uint16_t>(memory->DirectReadFromZ80Memory(kCounter) |
                                     (memory->DirectReadFromZ80Memory(kCounter + 1) << 8));
    }

    /// Record: the countdown runs out during the third frame, a marker is placed
    /// right after the last call in that same frame, then three quiet frames.
    /// @return the marker position
    ttd::TTDTimePoint RecordCountdownThenMarker(bool marker = true)
    {
        InstallCountdown(2000);  // ~780 calls a frame: runs out in the third frame
        EXPECT_TRUE(_ttd->StartRecording());
        RunFrames(2);
        while (Counter() != 0)
            _emulator->RunTStates(100, /*skipBreakpoints=*/true);
        const ttd::TTDTimePoint at = _ttd->CurrentPosition();
        EXPECT_GT(at.tInFrame, 1000u) << "precondition: the last call and the marker share a frame";
        if (marker)
            _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "search-window barrier");
        RunFrames(3);
        _ttd->StopRecording();
        return at;
    }
};

/// A candidate frame the barrier keeps from replaying must stop the scan: the
/// index says the target ran there, and that run is later than any earlier
/// frame's. Skipping it answered with an older call.
TEST_F(TimeTravelManagerSearchWindow_Test, ReverseContinueStopsAtABarrierInACandidateFrame)
{
    _ttd->SetEnableCoverageIndex(true);
    const ttd::TTDTimePoint marker = RecordCountdownThenMarker();

    const auto r = _ttd->ReverseContinue({kTarget});
    EXPECT_FALSE(r.matched) << "answered with a call from frame " << r.arrivedAt.frame
                            << ", older than the calls in the barrier frame " << marker.frame;
    EXPECT_STREQ(r.blockingMarker.reason, "search-window barrier");
    ASSERT_TRUE(r.window.searched);
    EXPECT_EQ(r.window.from.frame, marker.frame);
    EXPECT_EQ(r.window.from.tInFrame, r.blockingMarker.time.tInFrame);
}

/// Same session, coverage index off: the plain scan already stopped at the
/// barrier; the window says where.
TEST_F(TimeTravelManagerSearchWindow_Test, ReverseContinueWithoutIndexReportsTheBarrierWindow)
{
    _ttd->SetEnableCoverageIndex(false);
    const ttd::TTDTimePoint marker = RecordCountdownThenMarker();

    const auto r = _ttd->ReverseContinue({kTarget});
    EXPECT_FALSE(r.matched);
    EXPECT_STREQ(r.blockingMarker.reason, "search-window barrier");
    ASSERT_TRUE(r.window.searched);
    EXPECT_EQ(r.window.from.frame, marker.frame);
    EXPECT_EQ(r.window.to.frame, _ttd->SessionEndPosition().frame);
}

TEST_F(TimeTravelManagerSearchWindow_Test, ReverseContinueMatchWindowEndsAtTheMatch)
{
    _ttd->SetEnableCoverageIndex(true);
    const ttd::TTDTimePoint lastCall = RecordCountdownThenMarker(/*marker=*/false);
    const ttd::TTDTimePoint end = _ttd->SessionEndPosition();

    const auto r = _ttd->ReverseContinue({kTarget});
    ASSERT_TRUE(r.matched);
    EXPECT_EQ(r.arrivedAt.frame, lastCall.frame);
    ASSERT_TRUE(r.window.searched);
    EXPECT_EQ(r.window.from.frame, r.arrivedAt.frame);
    EXPECT_EQ(r.window.from.tInFrame, r.arrivedAt.tInFrame);
    EXPECT_EQ(r.window.to.frame, end.frame);
}

/// Journal path: a match ends the window at the match; a final "no match"
/// covers the whole session (the journal ignores markers - it is ground truth).
TEST_F(TimeTravelManagerSearchWindow_Test, FindLastJournalWindow)
{
    _ttd->SetEnableWriteJournal(true);
    RecordCountdownThenMarker();
    const ttd::TTDTimePoint start = _ttd->GetCheckpoint(0)->time;

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = kCounter;
    q.access = ttd::TTDAccessType::Write;
    ttd::TTDExternalEvent blocked;
    ttd::TTDSearchWindow window;
    const auto hit = _ttd->FindLastAccess(q, &blocked, &window);
    ASSERT_TRUE(hit.has_value());
    ASSERT_TRUE(window.searched);
    EXPECT_EQ(window.from.frame, hit->time.frame);
    EXPECT_EQ(window.from.tInFrame, hit->time.tInFrame);

    q.addrFrom = q.addrTo = 0xA123;  // never written
    window = {};
    EXPECT_FALSE(_ttd->FindLastAccess(q, &blocked, &window).has_value());
    ASSERT_TRUE(window.searched);
    EXPECT_EQ(window.from.frame, start.frame) << "a final journal 'no match' covers the whole session";
    EXPECT_EQ(window.from.tInFrame, start.tInFrame);
}

/// Replay path (no journal): the scan stops at the barrier and says so.
TEST_F(TimeTravelManagerSearchWindow_Test, FindLastReplayWindowStopsAtTheBarrier)
{
    _ttd->SetEnableWriteJournal(false);
    const ttd::TTDTimePoint marker = RecordCountdownThenMarker();

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = kCounter;
    q.access = ttd::TTDAccessType::Write;
    ttd::TTDExternalEvent blocked;
    ttd::TTDSearchWindow window;
    EXPECT_FALSE(_ttd->FindLastAccess(q, &blocked, &window).has_value());
    EXPECT_STREQ(blocked.reason, "search-window barrier");
    ASSERT_TRUE(window.searched);
    EXPECT_EQ(window.from.frame, marker.frame);
    EXPECT_EQ(window.from.tInFrame, blocked.time.tInFrame);
    EXPECT_EQ(window.to.frame, _ttd->SessionEndPosition().frame);
}

TEST_F(TimeTravelManagerSearchWindow_Test, RefusedSearchHasNoWindow)
{
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = kCounter;
    ttd::TTDSearchWindow window;
    EXPECT_FALSE(_ttd->FindLastAccess(q, nullptr, &window).has_value());  // no history
    EXPECT_FALSE(window.searched);
    EXPECT_FALSE(_ttd->ReverseContinue({kTarget}).window.searched);
}

// Write-journal segments and their status: timetravelmanager_journalsegments_test.cpp (D40)

/// Phase 5 Step 4: this file is one of v1's own (ttdv1tests.h), so the
/// machines its tests create record with v1, not with the engine
TEST(TimeTravelManager_Backend_Test, MachinesOfV1TestsRecordWithV1)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    EXPECT_EQ(static_cast<void*>(context->pTimeTravelHooks), static_cast<void*>(context->pTimeTravelManager))
        << "the core calls v1's manager";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
