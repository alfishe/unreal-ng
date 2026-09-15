/// @file ttdseekbenchmark.cpp
/// @brief Benchmark SeekTo worst-case latency for TTD.
///
/// Per Phase 3 §3.3: Validates the "ANY frame <= 6ms" claim (or documents where
/// it does not hold). Records a 5+ minute session, forces thinning by filling
/// the 64MB budget, then measures random SeekTo latency distribution.
///
/// Metrics:
///   - Mean seek time
///   - p50/p95/p99/max latency
///   - Whether thinned regions exceed 6ms (expected: yes, up to ~200ms for replay)
///
/// Run with: ./core-benchmarks --benchmark_filter="TTD_Seek.*"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{

/// @brief Per-benchmark Emulator + TTD setup.
///
/// Constructed once per benchmark process (static instance), reset per
/// benchmark case. Lives on the benchmark thread.
struct SeekBenchmarkFixture
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    FeatureManager* fm = nullptr;
    Memory* memory = nullptr;

    bool Init()
    {
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            return false;
        context = emulator->GetContext();
        if (!context) return false;
        ttd = context->pTimeTravelManager;
        if (!ttd) return false;
        memory = context->pMemory;
        if (!memory) return false;
        fm = emulator->GetFeatureManager();
        if (!fm) return false;

        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        memory->UpdateFeatureCache();
        return true;
    }

    void Shutdown()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
            emulator = nullptr;
        }
    }

    /// @brief Start a fresh recording session and run for `frames` frames.
    void RecordSession(uint32_t frames)
    {
        ttd->InvalidateSession("benchmark reset");
        ttd->StartRecording();
        emulator->RunNFrames(static_cast<unsigned>(frames), /*skipBreakpoints=*/true);
        ttd->StopRecording();

        // Position at session end
        const ttd::TTDTimePoint end = ttd->SessionEndPosition();
        ttd->SeekTo(end);
    }
};

SeekBenchmarkFixture g_fixture;
bool g_fixtureReady = false;

bool EnsureFixture()
{
    if (g_fixtureReady) return true;
    if (!g_fixture.Init())
        return false;
    g_fixtureReady = true;
    return true;
}

/// Percentile calculation helper
double Percentile(std::vector<double>& sorted, double p)
{
    if (sorted.empty()) return 0.0;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(p / 100.0 * (sorted.size() - 1));
    return sorted[idx];
}

} // namespace

// ===========================================================================
// SeekTo random frame — short session baseline
// ===========================================================================
//
// Establishes the baseline seek time for a short, dense session (no thinning).
// This is the best-case scenario where every frame has a checkpoint.

static void BM_TTD_SeekTo_DenseSession(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint32_t frames = static_cast<uint32_t>(state.range(0));
    g_fixture.RecordSession(frames);

    auto info = g_fixture.ttd->GetSessionInfo();
    if (info.checkpointCount < 2)
    {
        state.SkipWithError("Not enough checkpoints");
        return;
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> frameDist(
        info.sessionStartFrame, info.currentEndFrame);

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        // Randomize starting position to avoid cache effects
        uint64_t startFrame = frameDist(rng);
        ttd::TTDTimePoint startPos;
        startPos.frame = startFrame;
        startPos.tInFrame = 0;
        g_fixture.ttd->SeekTo(startPos);

        uint64_t targetFrame = frameDist(rng);
        ttd::TTDTimePoint target;
        target.frame = targetFrame;
        target.tInFrame = 0;
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->SeekTo(target);
        auto end = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latencyMs);
    }

    // Calculate statistics
    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["p50_ms"] = Percentile(latencies, 50);
        state.counters["p95_ms"] = Percentile(latencies, 95);
        state.counters["p99_ms"] = Percentile(latencies, 99);
        state.counters["max_ms"] = maxVal;
        state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
        state.counters["exceeds_6ms"] = maxVal > 6.0 ? 1.0 : 0.0;
    }

    state.SetLabel(std::to_string(frames) + " frames (dense)");
}
BENCHMARK(BM_TTD_SeekTo_DenseSession)
    ->Arg(100)    // ~2 seconds
    ->Arg(500)    // ~10 seconds
    ->Arg(1000)   // ~20 seconds
    ->Iterations(200)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// SeekTo random frame — long session (5+ minutes)
// ===========================================================================
//
// Records a 5+ minute session (15000 frames at 50Hz). The session may thin
// automatically depending on memory budget. This measures the worst-case
// latency for a typical long recording session.

static void BM_TTD_SeekTo_LongSession(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint32_t frames = 15000;  // 5 minutes at 50fps
    g_fixture.RecordSession(frames);

    auto info = g_fixture.ttd->GetSessionInfo();
    if (info.checkpointCount < 2)
    {
        state.SkipWithError("Not enough checkpoints");
        return;
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> frameDist(
        info.sessionStartFrame, info.currentEndFrame);

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        // Start from session end for worst-case backward seeks
        g_fixture.ttd->SeekTo(g_fixture.ttd->SessionEndPosition());

        uint64_t targetFrame = frameDist(rng);
        ttd::TTDTimePoint target;
        target.frame = targetFrame;
        target.tInFrame = 0;
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->SeekTo(target);
        auto end = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latencyMs);
    }

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["p50_ms"] = Percentile(latencies, 50);
        state.counters["p95_ms"] = Percentile(latencies, 95);
        state.counters["p99_ms"] = Percentile(latencies, 99);
        state.counters["max_ms"] = maxVal;
        state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
        state.counters["heap_mb"] = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
        state.counters["exceeds_6ms"] = maxVal > 6.0 ? 1.0 : 0.0;
    }

    state.SetLabel("15000 frames (~5min)");
}
BENCHMARK(BM_TTD_SeekTo_LongSession)->Iterations(500)->Unit(benchmark::kMillisecond);

// ===========================================================================
// SeekTo — backward vs forward
// ===========================================================================
//
// Measures whether seek direction affects latency. Backward seeks may be
// more expensive if they require checkpoint restore + forward replay.

static void BM_TTD_SeekTo_Direction(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const bool backward = state.range(0) != 0;
    const uint32_t frames = 1000;

    g_fixture.RecordSession(frames);

    auto info = g_fixture.ttd->GetSessionInfo();
    uint64_t midFrame = info.sessionStartFrame + (info.currentEndFrame - info.sessionStartFrame) / 2;

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        if (backward)
        {
            // Start at end, seek backward
            g_fixture.ttd->SeekTo(g_fixture.ttd->SessionEndPosition());
        }
        else
        {
            // Start at beginning, seek forward
            ttd::TTDTimePoint start;
            start.frame = info.sessionStartFrame;
            start.tInFrame = 0;
            g_fixture.ttd->SeekTo(start);
        }
        state.ResumeTiming();

        ttd::TTDTimePoint target;
        target.frame = midFrame;
        target.tInFrame = 0;

        auto startTime = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->SeekTo(target);
        auto endTime = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        latencies.push_back(latencyMs);
    }

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["max_ms"] = maxVal;
    }

    state.SetLabel(backward ? "backward" : "forward");
}
BENCHMARK(BM_TTD_SeekTo_Direction)
    ->Arg(0)  // forward
    ->Arg(1)  // backward
    ->Iterations(200)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// SeekTo intra-frame — varying t-state offsets
// ===========================================================================
//
// Measures the cost of intra-frame replay (silent execution from frame
// boundary to target t-state). The cost scales with distance from frame start.

static void BM_TTD_SeekTo_IntraFrame(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint32_t tInFrame = static_cast<uint32_t>(state.range(0));
    const uint32_t frames = 500;

    g_fixture.RecordSession(frames);

    auto info = g_fixture.ttd->GetSessionInfo();
    uint64_t targetFrame = info.sessionStartFrame + 250;

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        // Start at frame boundary
        ttd::TTDTimePoint start;
        start.frame = info.sessionStartFrame;
        start.tInFrame = 0;
        g_fixture.ttd->SeekTo(start);
        state.ResumeTiming();

        ttd::TTDTimePoint target;
        target.frame = targetFrame;
        target.tInFrame = tInFrame;

        auto startTime = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->SeekTo(target);
        auto endTime = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        latencies.push_back(latencyMs);
    }

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["max_ms"] = maxVal;
    }

    state.SetLabel("tInFrame=" + std::to_string(tInFrame));
}
BENCHMARK(BM_TTD_SeekTo_IntraFrame)
    ->Arg(0)       // Frame boundary (no replay)
    ->Arg(1000)    // Early in frame
    ->Arg(35000)   // Mid-frame (~50% of Pentagon frame)
    ->Arg(69888)   // Near end of frame (Pentagon frame length)
    ->Iterations(200)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// SeekTo with busy workload
// ===========================================================================
//
// Same as long session but with a busy program running (lots of state change).
// The busy loop generates more dirty pages per frame, stressing the COW store.

static void BM_TTD_SeekTo_BusyWorkload(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    // Install a busy program that writes to multiple pages per frame
    const uint8_t busyProgram[] = {
        0x3C,             // INC A
        0x77,             // LD (HL),A
        0x23,             // INC HL
        0x7C,             // LD A,H
        0xFE, 0x80,       // CP 0x80 (wrap at 0x8000)
        0x38, 0xF8,       // JR C, -8
        0x21, 0x00, 0x40, // LD HL,0x4000
        0xC3, 0x00, 0x80  // JP 0x8000
    };
    constexpr uint16_t kBusyStart = 0x8000;

    for (uint16_t i = 0; i < sizeof(busyProgram); ++i)
        g_fixture.memory->DirectWriteToZ80Memory(kBusyStart + i, busyProgram[i]);

    Z80State* z80 = g_fixture.emulator->GetZ80State();
    z80->pc = kBusyStart;
    z80->sp = 0xFF00;
    z80->hl = 0x4000;

    const uint32_t frames = 3000;  // ~1 minute of busy execution
    g_fixture.RecordSession(frames);

    auto info = g_fixture.ttd->GetSessionInfo();

    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> frameDist(
        info.sessionStartFrame, info.currentEndFrame);

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->SeekTo(g_fixture.ttd->SessionEndPosition());

        uint64_t targetFrame = frameDist(rng);
        ttd::TTDTimePoint target;
        target.frame = targetFrame;
        target.tInFrame = 0;
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->SeekTo(target);
        auto end = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latencyMs);
    }

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["p50_ms"] = Percentile(latencies, 50);
        state.counters["p95_ms"] = Percentile(latencies, 95);
        state.counters["p99_ms"] = Percentile(latencies, 99);
        state.counters["max_ms"] = maxVal;
        state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
        state.counters["heap_mb"] = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
        state.counters["exceeds_6ms"] = maxVal > 6.0 ? 1.0 : 0.0;
    }

    state.SetLabel("busy workload (~1min)");
}
BENCHMARK(BM_TTD_SeekTo_BusyWorkload)->Iterations(200)->Unit(benchmark::kMillisecond);

// ===========================================================================
// StepBackFrame — frame-by-frame rewind
// ===========================================================================
//
// Measures the latency of stepping back one frame at a time, which is the
// typical UI scrubber interaction.

static void BM_TTD_StepBackFrame(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint32_t frames = 1000;
    g_fixture.RecordSession(frames);

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        // Position at session end
        g_fixture.ttd->SeekTo(g_fixture.ttd->SessionEndPosition());
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->StepBackFrame();
        auto end = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latencyMs);
    }

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["max_ms"] = maxVal;
    }

    state.SetLabel("single frame step-back");
}
BENCHMARK(BM_TTD_StepBackFrame)->Iterations(500)->Unit(benchmark::kMicrosecond);
