/// @file ttd_capture_cost_gate_test.cpp
/// @brief Phase 2 Item 7 — Capture-cost CI gate.
///
/// Per parent TDD §15.1 and the implementation plan §3.A2 Item 7:
///   "Wire a CI gate that fails when the per-frame capture cost regresses
///    beyond a defined budget."
///
/// This is NOT a micro-benchmark (those live in core/benchmarks/). It is a
/// wall-clock regression gate that runs in the regular test runner, so it
/// participates in CI on every commit. The budget is intentionally generous
/// (10 ms / frame on a 128 KB Pentagon) so it does NOT flake on slow CI
/// runners — its job is to catch O(n^2) blowups and accidental full-RAM
/// copies on every frame, not to track fine-grained perf.
///
/// Test methodology:
///   1. Set up a Pentagon 128 emulator with TTD enabled.
///   2. Load a snapshot (Dizzy Y — same fixture as the divergence corpus).
///   3. Start TTD recording.
///   4. Run kFrames frames, measuring wall-clock time.
///   5. Assert: total / frames <= kBudgetMsPerFrame.
///
/// The budget was chosen as 10 ms/frame for the 128 KB model. Sprint 0's
/// benchmark numbers (commit ad0c101a) put full-frame capture (Snapshot +
/// HashSnapshot + 128 KB RAM digest) at ~70 us on the development machine.
/// 10 ms is ~140x that — enough headroom for any CI runner, tight enough
/// that an O(n) → O(n^2) regression in the capture path fails immediately.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/test_path_helper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "debugger/ttd/timetravelmanager.h"

namespace {

constexpr int    kFrames           = 300;
constexpr double kBudgetMsPerFrame = 10.0;  // See file header for rationale.

Emulator* MakeTtdEmulator(const std::string& modelName = "PENTAGON",
                          LoggerLevel log = LoggerLevel::LogError)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator(modelName, log);
    if (!emu)
        return nullptr;

    FeatureManager* fm = emu->GetFeatureManager();
    if (fm)
    {
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
    }

    EmulatorContext* ctx = emu->GetContext();
    if (ctx && ctx->pMemory)
        ctx->pMemory->UpdateFeatureCache();

    return emu;
}

} // anonymous namespace

// =========================================================================
// Pentagon 128 — capture cost must stay under kBudgetMsPerFrame.
//
// We pick Pentagon 128 (8 RAM pages = 128 KB) as the reference model because
// it's the most common development target and has the highest sustained
// write rate of the v1-supported models (RAM-banking demo scenes churn all
// 8 pages). If the gate fails here, it would also fail on smaller models.
// =========================================================================

TEST(TTD_Capture_Cost_Gate_Test, Pentagon128_StaysUnderBudget)
{
    Emulator* emu = MakeTtdEmulator("PENTAGON");
    ASSERT_NE(emu, nullptr);
    auto cleanup = [&]() { EmulatorTestHelper::CleanupEmulator(emu); };

    EmulatorContext* ctx = emu->GetContext();
    ASSERT_NE(ctx, nullptr);
    ASSERT_NE(ctx->pTimeTravelManager, nullptr);

    // Use Dizzy Y as a representative 48K-ish workload. The snapshot loads
    // fine on Pentagon (it ignores the extra RAM pages until banking flips).
    // We don't care about divergence correctness here — just frame rate
    // under capture load.
    const std::string snapshotPath =
        TestPathHelper::GetTestDataPath("loaders/sna/Dizzy Y.sna");
    ASSERT_TRUE(emu->LoadSnapshot(snapshotPath))
        << "Dizzy Y snapshot not found at " << snapshotPath;

    ASSERT_TRUE(ctx->pTimeTravelManager->StartRecording());

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i)
        emu->RunFrame(/*skipBreakpoints=*/true);
    const auto t1 = std::chrono::steady_clock::now();

    const double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double perFrameMs = totalMs / kFrames;

    // Log even on pass — useful for tracking drift in CI logs.
    RecordProperty("total_ms",       std::to_string(totalMs));
    RecordProperty("per_frame_ms",   std::to_string(perFrameMs));
    RecordProperty("frames",         std::to_string(kFrames));
    RecordProperty("budget_ms",      std::to_string(kBudgetMsPerFrame));
    RecordProperty("checkpoint_count",
                   std::to_string(ctx->pTimeTravelManager->GetCheckpointCount()));

    EXPECT_LT(perFrameMs, kBudgetMsPerFrame)
        << "TTD capture cost regression: per-frame cost " << perFrameMs
        << " ms exceeds budget " << kBudgetMsPerFrame << " ms. "
        << "Total " << totalMs << " ms over " << kFrames << " frames, "
        << ctx->pTimeTravelManager->GetCheckpointCount() << " checkpoints captured.";

    cleanup();
}

// =========================================================================
// 48K model — same gate, smaller RAM. Sanity check that the budget holds
// across model sizes (the capture path's per-page cost should be linear;
// if it isn't, this gate fires before the 128K one does).
// =========================================================================

TEST(TTD_Capture_Cost_Gate_Test, Spectrum48_StaysUnderBudget)
{
    Emulator* emu = MakeTtdEmulator("48K");
    ASSERT_NE(emu, nullptr);
    auto cleanup = [&]() { EmulatorTestHelper::CleanupEmulator(emu); };

    EmulatorContext* ctx = emu->GetContext();
    ASSERT_NE(ctx, nullptr);
    ASSERT_NE(ctx->pTimeTravelManager, nullptr);

    const std::string snapshotPath =
        TestPathHelper::GetTestDataPath("loaders/sna/Dizzy Y.sna");
    ASSERT_TRUE(emu->LoadSnapshot(snapshotPath))
        << "Dizzy Y snapshot not found at " << snapshotPath;

    ASSERT_TRUE(ctx->pTimeTravelManager->StartRecording());

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i)
        emu->RunFrame(/*skipBreakpoints=*/true);
    const auto t1 = std::chrono::steady_clock::now();

    const double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double perFrameMs = totalMs / kFrames;

    RecordProperty("total_ms",       std::to_string(totalMs));
    RecordProperty("per_frame_ms",   std::to_string(perFrameMs));
    RecordProperty("frames",         std::to_string(kFrames));
    RecordProperty("budget_ms",      std::to_string(kBudgetMsPerFrame));
    RecordProperty("checkpoint_count",
                   std::to_string(ctx->pTimeTravelManager->GetCheckpointCount()));

    EXPECT_LT(perFrameMs, kBudgetMsPerFrame)
        << "TTD capture cost regression on 48K model: per-frame cost "
        << perFrameMs << " ms exceeds budget " << kBudgetMsPerFrame << " ms.";

    cleanup();
}
