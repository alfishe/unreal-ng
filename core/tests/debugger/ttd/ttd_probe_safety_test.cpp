/// @file ttd_probe_safety_test.cpp
/// @brief Verifies the TTDAccessProbe doesn't leak armed state into normal
///        emulation after FindLastAccess or StepBackInstruction completes.
///
/// The probe is an inline instance in EmulatorContext. If a bug leaves it
/// armed after a reverse-search operation, every subsequent memory write/read
/// in normal emulation would be recorded as a "hit", corrupting future
/// searches and consuming memory. This test verifies the disarm invariant.

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_probe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_Probe_Safety_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
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

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

// ===========================================================================
// Probe is disarmed after FindLastAccess
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, ProbeDisarmed_AfterFindLastAccess)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->RecordMemoryWrite(0x4000, 0, 0x42, 0x1234, 1);
    RunFrames(2);
    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x4000;
    q.addrTo   = 0x4000;
    q.access   = ttd::TTDAccessType::Write;
    auto result = _ttd->FindLastAccess(q);

    ASSERT_TRUE(result.has_value());

    // The probe MUST be disarmed after FindLastAccess returns.
    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

// ===========================================================================
// Probe is disarmed after FindLastAccess that uses replay fallback
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, ProbeDisarmed_AfterFindLastAccess_ReplayFallback)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    // Read access type forces replay fallback (Read is not journaled).
    ttd::TTDSearchQuery q;
    q.addrFrom = 0;
    q.addrTo   = 0xFFFF;
    q.access   = ttd::TTDAccessType::Read;
    auto result = _ttd->FindLastAccess(q);

    // Probe must be disarmed regardless of whether a match was found.
    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

// ===========================================================================
// Probe is disarmed after StepBackInstruction
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, ProbeDisarmed_AfterStepBackInstruction)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    // Step back (uses FindLastAccess with Execute probe internally).
    _ttd->SeekTo({3, 0});
    _ttd->StepBackInstruction();

    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

// ===========================================================================
// Probe is disarmed after StepForwardInstruction
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, ProbeDisarmed_AfterStepForwardInstruction)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    _ttd->SeekTo({2, 0});
    _ttd->StepForwardInstruction();

    EXPECT_FALSE(_context->ttdProbe.IsArmed());
}

// ===========================================================================
// Probe is disarmed after FindLastAccess returns no match
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, ProbeDisarmed_AfterFindLastAccess_NoMatch)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0xDEAD;
    q.addrTo   = 0xDEAD;
    q.access   = ttd::TTDAccessType::Write;
    auto result = _ttd->FindLastAccess(q);

    EXPECT_FALSE(result.has_value());

    // Even on no-match, probe must be disarmed.
    EXPECT_FALSE(_context->ttdProbe.IsArmed());
}

// ===========================================================================
// Repeated FindLastAccess calls don't accumulate hits
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, RepeatedFindLast_NoHitAccumulation)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->RecordMemoryWrite(0x4000, 0, 0x42, 0x1234, 1);
    RunFrames(3);
    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x4000;
    q.addrTo   = 0x4000;
    q.access   = ttd::TTDAccessType::Write;

    // Call FindLastAccess multiple times.
    for (int i = 0; i < 5; ++i)
    {
        auto result = _ttd->FindLastAccess(q);
        ASSERT_TRUE(result.has_value());
        EXPECT_FALSE(_context->ttdProbe.IsArmed());
        EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u)
            << "Iteration " << i << ": hits leaked from previous call";
    }
}

// ===========================================================================
// Probe is disarmed even when FindLastAccess hits a marker barrier
// ===========================================================================

TEST_F(TTD_Probe_Safety_Test, ProbeDisarmed_AfterMarkerBarrier)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "test");
    RunFrames(2);
    _ttd->StopRecording();

    // Read query that would require replay, blocked by marker.
    ttd::TTDSearchQuery q;
    q.addrFrom = 0;
    q.addrTo   = 0xFFFF;
    q.access   = ttd::TTDAccessType::Read;

    ttd::TTDExternalEvent blockingMarker;
    auto result = _ttd->FindLastAccess(q, &blockingMarker);

    // Marker may block or not — either way, probe must be disarmed.
    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}
