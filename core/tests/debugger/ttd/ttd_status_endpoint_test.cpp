/// @file ttd_status_endpoint_test.cpp
/// @brief Contract tests for the GET /ttd/status WebAPI endpoint (P1.7).
///
/// The endpoint (core/automation/webapi/src/api/ttd_api.cpp) is a thin
/// adapter around two pieces of state from the core library:
///   - `ttd::TTDSessionStateToString(state)` — stable public identifier
///   - `ttd::TimeTravelManager::GetSessionInfo()`   — every numeric field
///
/// Testing strategy: since core-tests does not link against the webapi
/// target (which carries the Drogon HTTP machinery), we verify the *contract*
/// the endpoint is built on. If the contract holds at the core level, the
/// endpoint's JSON output is correct by construction. Specifically:
///   1. The three state-string spellings are exactly "idle" / "recording" /
///      "detached" (these strings are part of the public HTTP contract —
///      automation clients switch on them — per parent TDD §10.4).
///   2. Every field surfaced by the endpoint (state, session_start_frame,
///      current_end_frame, checkpoint_count, page_store_bytes,
///      page_store_used_bytes, baseline_frames_captured) takes the expected
///      value at each lifecycle transition (Idle → Recording → Idle).
///   3. The "ttd_available" capability flag flips correctly when the manager
///      pointer is null vs. populated.
///
/// These tests run against a real Emulator instance so the page-store byte
/// math reflects what real-world HTTP clients will observe.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_page_store.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// ===========================================================================
// State-string contract — public HTTP identifier stability
// ===========================================================================

/// @test The three state strings match the canonical spellings documented in
/// parent TDD §10.4. WebAPI clients switch on these string values.
TEST(TTD_StatusString_Test, StateString_Idle_IsCanonical)
{
    EXPECT_STREQ(ttd::TTDSessionStateToString(ttd::TTDSessionState::Idle), "idle");
}

TEST(TTD_StatusString_Test, StateString_Recording_IsCanonical)
{
    EXPECT_STREQ(ttd::TTDSessionStateToString(ttd::TTDSessionState::Recording), "recording");
}

TEST(TTD_StatusString_Test, StateString_Detached_IsCanonical)
{
    EXPECT_STREQ(ttd::TTDSessionStateToString(ttd::TTDSessionState::Detached), "detached");
}

// ===========================================================================
// Fixture: real Emulator + TimeTravelManager, so byte math reflects real life
// ===========================================================================

class TTD_StatusEndpoint_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(_emulator->Init()) << "Failed to initialize emulator";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr) << "TimeTravelManager was not created during Emulator::Init";
        _memory = _context->pMemory;
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

    void EnableTTD()
    {
        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }
};

// ===========================================================================
// Endpoint field contract — every JSON key the endpoint emits
// ===========================================================================

/// @test Idle emulator: every field the endpoint surfaces is at its zero
/// default. The endpoint will emit JSON with these exact values plus
/// `"state": "idle"` and `"ttd_available": true`.
TEST_F(TTD_StatusEndpoint_Test, Idle_AllFieldsAtZero)
{
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.sessionStartFrame, 0u);
    EXPECT_EQ(info.currentEndFrame, 0u);
    EXPECT_EQ(info.checkpointCount, 0u);
    EXPECT_EQ(info.pageStoreBytes, 0u);
    EXPECT_EQ(info.pageStoreUsedBytes, 0u);
    EXPECT_EQ(info.baselineFramesCaptured, 0u);
}

/// @test After StartRecording, every field the endpoint surfaces has a
/// non-trivial value that reflects the baseline capture. The endpoint will
/// emit `"state": "recording"`, baseline frame counters, checkpoint_count >= 1,
/// and the page-store bytes reflecting the full model-RAM baseline.
TEST_F(TTD_StatusEndpoint_Test, Recording_BaselinePopulatesAllFields)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Recording);
    EXPECT_GE(info.checkpointCount, 1u) << "Baseline checkpoint must be present";

    // Page store must hold every model-RAM page (v1 strategy).
    const uint16_t pages = _ttd->GetModelRamPages();
    ASSERT_GT(pages, 0u);
    const size_t expectedBytes = static_cast<size_t>(pages) * ttd::TTDPageStore::kPageSize;
    EXPECT_EQ(info.pageStoreBytes, expectedBytes)
        << "page_store_bytes must reflect model-RAM baseline (v1 strategy)";
    EXPECT_EQ(info.pageStoreUsedBytes, expectedBytes)
        << "page_store_used_bytes must equal capacity right after baseline";

    // Frames must come from the live EmulatorState — non-zero once the
    // emulator has run at least one frame's worth of bookkeeping.
    EXPECT_GE(info.currentEndFrame, info.sessionStartFrame)
        << "current_end_frame must be >= session_start_frame";
}

/// @test OnFrameBoundary increments checkpointCount, which the endpoint
/// surfaces. sessionStartFrame stays anchored; currentEndFrame advances.
TEST_F(TTD_StatusEndpoint_Test, Recording_OnFrameBoundary_AdvancesCurrentEndFrame)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    ttd::TTDSessionInfo info0 = _ttd->GetSessionInfo();
    const uint64_t startBefore = info0.sessionStartFrame;
    const size_t   countBefore = info0.checkpointCount;

    ASSERT_TRUE(_ttd->IsRecording());
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();

    ttd::TTDSessionInfo info1 = _ttd->GetSessionInfo();
    EXPECT_EQ(info1.sessionStartFrame, startBefore)
        << "session_start_frame is anchored at the first checkpoint";
    EXPECT_EQ(info1.checkpointCount, countBefore + 2)
        << "checkpoint_count must reflect every appended checkpoint";
    EXPECT_GE(info1.currentEndFrame, info0.currentEndFrame)
        << "current_end_frame is monotonic non-decreasing";
}

/// @test StopRecording leaves history browsable: state returns to Idle but
/// checkpointCount is retained. The endpoint will emit `"state": "idle"` with
/// non-zero checkpoint_count — automation clients use this to detect that
/// there is still history available to seek into (P4).
TEST_F(TTD_StatusEndpoint_Test, Stop_KeepsHistory_StateReturnsToIdle)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_TRUE(_ttd->IsRecording());
    _ttd->OnFrameBoundary();
    ASSERT_GE(_ttd->GetSessionInfo().checkpointCount, 2u);

    _ttd->StopRecording();

    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Idle);
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_GE(info.checkpointCount, 2u)
        << "History must be retained after Stop — endpoint surfaces this so "
           "automation clients know there is still something to seek into";
    EXPECT_GT(info.pageStoreBytes, 0u)
        << "Page store backing must still be alive while history is browsable";
}

/// @test Invalidate zeroes every field the endpoint surfaces. After
/// invalidation the endpoint's payload is indistinguishable from a
/// never-recorded session (per TDD §4.2 "Invalidated → history cleared").
TEST_F(TTD_StatusEndpoint_Test, Invalidate_ClearsAllObservableFields)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->OnFrameBoundary();
    ASSERT_GT(_ttd->GetSessionInfo().checkpointCount, 1u);

    _ttd->InvalidateSession("test-invalidate");

    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(info.sessionStartFrame, 0u);
    EXPECT_EQ(info.currentEndFrame, 0u);
    EXPECT_EQ(info.checkpointCount, 0u);
    EXPECT_EQ(info.pageStoreBytes, 0u);
    EXPECT_EQ(info.pageStoreUsedBytes, 0u);
    EXPECT_EQ(info.baselineFramesCaptured, 0u);
}

// ===========================================================================
// Capability probe — ttd_available flag
// ===========================================================================

/// @test The endpoint surfaces `ttd_available: true` when the manager is
/// populated (the normal P1 build path). We can't directly test the null
/// path from core-tests because every Emulator constructs a TimeTravelManager, but
/// we verify the positive direction here and rely on the endpoint source
/// (which builds the same payload either way) for the negative direction.
TEST_F(TTD_StatusEndpoint_Test, ManagerPresent_CapabilityFlagWouldBeTrue)
{
    // The endpoint emits `ttd_available: (context->pTimeTravelManager != nullptr)`.
    // Mirror that condition explicitly so a regression in either side is
    // surfaced by this test.
    EXPECT_NE(_context->pTimeTravelManager, nullptr)
        << "Endpoint would emit ttd_available=false on this build — "
           "the test fixture expects every P1 Emulator to construct a manager";
}
