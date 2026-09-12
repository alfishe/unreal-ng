/// @file ttdcapturecostgate_test.cpp
/// @brief Phase 2 Item 7 — capture-cost CI gate.
///
/// Per parent TDD §15.1 and the implementation plan §3.A2 Item 7:
///   "Wire a CI gate that fails when the per-frame capture cost regresses
///    beyond a defined budget."
///
/// This is NOT a micro-benchmark (those live in core/benchmarks/). It is a
/// regression gate that runs in the regular test runner, so it participates in
/// CI on every commit. Its job is to catch an accidental full-RAM copy per
/// frame or an algorithmic blowup in the capture path - not to track fine
/// timings.
///
/// ## Why this is not a wall-clock budget any more
///
/// It used to time `RunFrame()` over 100 frames and assert the total stayed
/// under 10 ms/frame. That could not do its job, for two measured reasons:
///
///  1. **It timed the wrong thing.** Capture is 4-8% of a recorded frame; the
///     other ~92% is ordinary emulation. At 1.1 ms/frame against a 10 ms
///     budget, capture could get *thirty times* slower and the gate would
///     still pass. A "capture cost gate" that cannot see a 30x capture
///     regression is decoration.
///  2. **The margin was not what the header claimed.** The 140x headroom was
///     computed against capture alone (~70 us) while the assertion measured
///     the whole frame. In Debug the real figure was 5.5 ms against the 10 ms
///     budget - 1.8x - which is a latent flake on a loaded CI runner, not a
///     generous margin.
///
/// ## What it measures instead
///
/// The page store's own accounting, which is exact and has no clock in it.
/// Across four runs the recorded payload was byte-identical every time
/// (75 B/frame on Pentagon, 74 B/frame on 48K), while the wall-clock share of
/// the same runs swung between 3.9% and 8.0%. One of those two numbers can
/// carry an assertion; the other cannot.
///
/// What the byte budget actually catches, verified by mutation rather than
/// assumed:
///
///  - **Losing compression or delta coding.** Making the page store keep raw
///    bytes took the payload from 75 to 8055 B/frame and failed both models
///    with a clear message. Under the same mutation the wall-clock share read
///    -9.8% and +3.1% on the two models - pure noise. A 100x increase in
///    stored data is invisible to a clock and unmissable to a counter.
///  - It does **not** catch "capture was offered more pages". Forcing the
///    dirty tracker to report all 8 RAM pages every frame left the payload
///    byte-identical at 75 B/frame, because the store dedupes unchanged
///    content. That regression costs time, not bytes, which is what the ratio
///    check below is for - and it is worth knowing the store is structurally
///    immune to it.
///
/// The wall clock is kept only as a *ratio* - capture time as a share of frame
/// time - where both halves scale together, so a slow runner cancels out. It
/// is set at 50% against a measured 4-8%, i.e. it fires only on a ~10x capture
/// slowdown that the byte budget cannot see (a scan that got quadratic without
/// storing more).

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcodecpagestore.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace {

/// 30 frames is enough to leave the opening I-frame behind and measure the
/// steady-state COW behaviour. It used to be 100; the byte accounting is exact,
/// so the extra 70 frames bought precision the old wall-clock average needed
/// and this one does not.
constexpr int kFrames = 30;

/// ~55x the measured 75 B/frame. Losing compression alone lands at ~8000
/// B/frame (measured), so the budget sits comfortably between normal operation
/// and the regression, with no room for a false positive from machine speed.
constexpr size_t kMaxPayloadBytesPerFrame = 4096;

/// Capture as a share of total frame time. Measured 4-8%; both sides of the
/// ratio scale with machine speed, so this survives a slow CI runner and still
/// catches a ~10x capture slowdown.
constexpr double kMaxCaptureShare = 0.50;

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

/// A fixture rather than two free TESTs so the body below can call
/// RecordProperty (a member of ::testing::Test) and so both models share one
/// implementation - they used to be copy-paste twins and a budget change had
/// to be made twice.
class TTD_Capture_Cost_Gate_Test : public ::testing::Test
{
protected:
    void RunCaptureCostGate(const std::string& modelName);
};

void TTD_Capture_Cost_Gate_Test::RunCaptureCostGate(const std::string& modelName)
{
    Emulator* emu = MakeTtdEmulator(modelName);
    ASSERT_NE(emu, nullptr);

    EmulatorContext* ctx = emu->GetContext();
    ASSERT_NE(ctx, nullptr);
    ASSERT_NE(ctx->pTimeTravelManager, nullptr);

    // Dizzy Y as a representative workload - the same fixture the divergence
    // corpus uses. Correctness is not the subject here, only capture cost.
    const std::string snapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/Dizzy Y.sna");
    ASSERT_TRUE(emu->LoadSnapshot(snapshotPath)) << "Dizzy Y snapshot not found at " << snapshotPath;

    // Baseline: the same frames with recording OFF. Subtracting this is what
    // makes the timing figure below about capture rather than about how fast
    // this machine emulates a Z80.
    const auto base0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i)
        emu->RunFrame(/*skipBreakpoints=*/true);
    const auto base1 = std::chrono::steady_clock::now();
    const double baselineMs = std::chrono::duration<double, std::milli>(base1 - base0).count();

    ASSERT_TRUE(ctx->pTimeTravelManager->StartRecording());
    const size_t payloadBefore = ctx->pTimeTravelManager->GetPageStore().GetLivePayloadBytes();

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i)
        emu->RunFrame(/*skipBreakpoints=*/true);
    const auto t1 = std::chrono::steady_clock::now();
    const double recordedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    const size_t payloadAfter = ctx->pTimeTravelManager->GetPageStore().GetLivePayloadBytes();
    const size_t payloadPerFrame = (payloadAfter - payloadBefore) / kFrames;
    const size_t checkpoints = ctx->pTimeTravelManager->GetCheckpointCount();
    const double captureShare = recordedMs > 0.0 ? (recordedMs - baselineMs) / recordedMs : 0.0;

    RecordProperty("model", modelName);
    RecordProperty("frames", std::to_string(kFrames));
    RecordProperty("payload_bytes_per_frame", std::to_string(payloadPerFrame));
    RecordProperty("payload_budget_bytes_per_frame", std::to_string(kMaxPayloadBytesPerFrame));
    RecordProperty("baseline_ms", std::to_string(baselineMs));
    RecordProperty("recorded_ms", std::to_string(recordedMs));
    RecordProperty("capture_share", std::to_string(captureShare));
    RecordProperty("checkpoint_count", std::to_string(checkpoints));

    // Capture actually happened. Without this the byte assertion below passes
    // trivially when recording silently stops - zero bytes is under any budget.
    EXPECT_GE(checkpoints, static_cast<size_t>(kFrames))
        << "recording captured " << checkpoints << " checkpoints over " << kFrames
        << " frames - the gate below would pass on an empty session";

    EXPECT_LT(payloadPerFrame, kMaxPayloadBytesPerFrame)
        << "TTD capture volume regression on " << modelName << ": " << payloadPerFrame
        << " bytes/frame exceeds the " << kMaxPayloadBytesPerFrame << " byte budget over " << kFrames
        << " frames. Losing compression or delta coding in the page store looks exactly like this.";

    EXPECT_LT(captureShare, kMaxCaptureShare)
        << "TTD capture time regression on " << modelName << ": capture is " << (captureShare * 100.0)
        << "% of frame time (" << recordedMs << " ms recorded vs " << baselineMs
        << " ms baseline over " << kFrames << " frames), budget " << (kMaxCaptureShare * 100.0) << "%.";

    EmulatorTestHelper::CleanupEmulator(emu);
}

/// Pentagon 128 (8 RAM pages) is the reference model: the most common
/// development target and the highest sustained write rate of the v1-supported
/// models, since RAM-banking demo scenes churn all 8 pages.
TEST_F(TTD_Capture_Cost_Gate_Test, Pentagon128_StaysUnderBudget)
{
    RunCaptureCostGate("PENTAGON");
}

/// Same gate on the smaller model. The capture path's per-page cost should be
/// linear in pages touched, so if it is not, this one fires alongside.
TEST_F(TTD_Capture_Cost_Gate_Test, Spectrum48_StaysUnderBudget)
{
    RunCaptureCostGate("48K");
}
