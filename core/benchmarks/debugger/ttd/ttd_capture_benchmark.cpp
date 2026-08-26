// ttd_capture_benchmark.cpp — measures the per-frame cost of capturing
// a machine-state hash, which is the foundational TTD recording overhead.
//
// Sprint 0, Item 0.5 — overhead doc §6.
//
// This benchmark does NOT yet exercise a real TTD recorder; it measures the
// primitives that any TTD capture path will be built from:
//
//   1. CaptureSnapshot() alone — pure field extraction from Z80State +
//      EmulatorState into the POD snapshot. Constant-time, O(1).
//   2. HashSnapshot() alone — FNV-1a over the POD snapshot. Constant-time.
//   3. HashBytes() over RAM — dominates total capture cost. Parameterized
//      over realistic RAM sizes (48K, 128K, 512K, 1M, 4M).
//   4. FullCapture() — CaptureSnapshot + HashSnapshot + HashBytes(RAM).
//      This is what gets called at every frame boundary when TTD recording
//      is on.
//
// Run with: ./core-benchmarks --benchmark_filter="TTD_Capture.*"
//

#include <benchmark/benchmark.h>

#include "debugger/ttd/machine_state_hash.h"
#include "emulator/cpu/z80.h"
#include "emulator/platform.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

/// Populate a Z80State with values that defeat trivial hash-collision shortcuts.
/// The exact values don't matter — we only need a stable, non-trivial input.
void FillCanonical(Z80State& z, uint16_t seed)
{
    std::memset(&z, 0, sizeof(z));
    z.pc = seed + 0x1000;
    z.sp = seed + 0x2000;
    z.af = seed + 0x0001;
    z.bc = seed + 0x0002;
    z.de = seed + 0x0003;
    z.hl = seed + 0x0004;
    z.ix = seed + 0x0005;
    z.iy = seed + 0x0006;
    z.alt.af = seed + 0x0010;
    z.alt.bc = seed + 0x0011;
    z.alt.de = seed + 0x0012;
    z.alt.hl = seed + 0x0013;
    z.i = static_cast<uint8_t>(seed + 0x40);
    z.r_low = static_cast<uint8_t>(seed + 0x41);
    z.r_hi = static_cast<uint8_t>(seed + 0x42);
    z.im = 1;
    z.memptr = seed + 0x6000;
    z.q = static_cast<uint8_t>(seed + 0x60);
}

void FillCanonical(EmulatorState& s, uint16_t seed)
{
    std::memset(&s, 0, sizeof(s));
    s.p7FFD = static_cast<uint8_t>(seed + 0x01);
    s.pFE = static_cast<uint8_t>(seed + 0x02);
    s.pBFFD = static_cast<uint8_t>(seed + 0x04);
    s.pFFFD = static_cast<uint8_t>(seed + 0x05);
    s.border_attr = static_cast<uint8_t>(seed + 0x12);
    s.t_states = seed * 1000ULL;
    s.frame_counter = seed;
}

} // namespace

/// region <CaptureSnapshot alone>

static void BM_TTD_Capture_SnapshotOnly(benchmark::State& state)
{
    Z80State z;
    FillCanonical(z, 0x1234);
    EmulatorState es;
    FillCanonical(es, 0x1234);

    for (auto _ : state)
    {
        auto snap = ttd::CaptureSnapshot(z, es, 0);
        benchmark::DoNotOptimize(snap);
    }
}
BENCHMARK(BM_TTD_Capture_SnapshotOnly)
    ->Iterations(1'000'000)
    ->Unit(benchmark::kNanosecond);

/// endregion </CaptureSnapshot alone>

/// region <HashSnapshot alone>

static void BM_TTD_Capture_HashSnapshotOnly(benchmark::State& state)
{
    Z80State z;
    FillCanonical(z, 0x1234);
    EmulatorState es;
    FillCanonical(es, 0x1234);
    const auto snap = ttd::CaptureSnapshot(z, es, 0);

    for (auto _ : state)
    {
        uint64_t h = ttd::HashSnapshot(snap);
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_TTD_Capture_HashSnapshotOnly)
    ->Iterations(1'000'000)
    ->Unit(benchmark::kNanosecond);

/// endregion </HashSnapshot alone>

/// region <HashBytes over RAM — size sweep>

/// Parameterized RAM digest benchmark. The arg is RAM size in bytes.
static void BM_TTD_Capture_HashRAM(benchmark::State& state)
{
    const size_t ramBytes = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> ram(ramBytes);
    // Fill with non-trivial data so the compiler can't elide the work.
    for (size_t i = 0; i < ramBytes; ++i)
    {
        ram[i] = static_cast<uint8_t>(i * 31 + 17);
    }

    for (auto _ : state)
    {
        uint64_t h = ttd::HashBytes(ram.data(), ramBytes);
        benchmark::DoNotOptimize(h);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(ramBytes));
    state.SetLabel(std::to_string(ramBytes / 1024) + "KB");
}
BENCHMARK(BM_TTD_Capture_HashRAM)
    ->Arg(48 * 1024)       // 48K Spectrum
    ->Arg(128 * 1024)      // 128K / Pentagon 128
    ->Arg(512 * 1024)      // Pentagon 512
    ->Arg(1024 * 1024)     // 1MB (ATM2, large Profi)
    ->Arg(4 * 1024 * 1024) // 4MB (ZX Evo, TS-Conf)
    ->Iterations(100)
    ->Unit(benchmark::kMicrosecond);

/// endregion </HashBytes over RAM — size sweep>

/// region <Full per-frame capture>

/// Full per-frame capture cost: CaptureSnapshot + HashSnapshot + RAM digest.
/// This is the number to compare against a 50Hz / 60Hz / 100Hz frame budget
/// to decide whether per-frame full-state capture is affordable.
static void BM_TTD_Capture_FullFrame(benchmark::State& state)
{
    const size_t ramBytes = static_cast<size_t>(state.range(0));

    Z80State z;
    FillCanonical(z, 0x1234);
    EmulatorState es;
    FillCanonical(es, 0x1234);

    std::vector<uint8_t> ram(ramBytes);
    for (size_t i = 0; i < ramBytes; ++i)
    {
        ram[i] = static_cast<uint8_t>(i * 31 + 17);
    }

    for (auto _ : state)
    {
        uint64_t ramDigest = ttd::HashBytes(ram.data(), ramBytes);
        auto snap = ttd::CaptureSnapshot(z, es, ramDigest);
        uint64_t h = ttd::HashSnapshot(snap);
        benchmark::DoNotOptimize(h);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(ramBytes));
    state.SetLabel(std::to_string(ramBytes / 1024) + "KB");
}
BENCHMARK(BM_TTD_Capture_FullFrame)
    ->Arg(48 * 1024)
    ->Arg(128 * 1024)
    ->Arg(512 * 1024)
    ->Iterations(200)
    ->Unit(benchmark::kMicrosecond);

/// endregion </Full per-frame capture>
