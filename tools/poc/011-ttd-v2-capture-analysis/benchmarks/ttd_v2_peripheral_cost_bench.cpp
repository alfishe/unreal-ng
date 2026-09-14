/// @file ttdperipheralbenchmark.cpp
/// @brief Benchmark peripheral capture/restore cost for TTD.
///
/// Per Phase 3 §3.2: Measures per-frame capture cost and per-seek restore cost
/// for peripherals of various sizes. This data feeds the 5-6ms per-frame budget
/// analysis and validates that peripheral serialization stays affordable.
///
/// Metrics:
///   - `ttd_peripheral_capture_ns` (per device, per frame)
///   - `ttd_peripheral_restore_ns` (per device, full state)
///   - `ttd_peripheral_restore_small_change_ns` (per device, compressed payload)
///   - `ttd_peripheral_bytes_per_frame` (full capture, payload-compressed)
///
/// The delta-encoding variants were retargeted when the delta API was cut
/// from the final TTD design (see the section notes below).
///
/// Test sizes: 256B, 4KB, 32KB, 512KB (covering AY through GeneralSound SRAM)
///
/// Run with: ./core-benchmarks --benchmark_filter="TTD_Peripheral.*"

#include <benchmark/benchmark.h>

#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"

#include <cstring>
#include <random>
#include <vector>

namespace
{

/// Mock peripheral for benchmarking with configurable state size.
class BenchmarkPeripheral : public ttd::TTDSerializable
{
public:
    explicit BenchmarkPeripheral(size_t stateSize, ttd::PeripheralId id = ttd::PeripheralId::Count)
        : _stateSize(stateSize), _id(id)
    {
        _state.resize(stateSize, 0);
    }

    size_t TTDStateSize() const override { return _stateSize; }

    void TTDSaveState(uint8_t* dst) const override
    {
        std::memcpy(dst, _state.data(), _stateSize);
    }

    void TTDLoadState(const uint8_t* src) override
    {
        std::memcpy(_state.data(), src, _stateSize);
    }

    std::string TTDDeviceName() const override { return "BenchmarkPeripheral"; }
    ttd::PeripheralId TTDPeripheralId() const override { return _id; }

    void FillRandom(uint32_t seed)
    {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<> dist(0, 255);
        for (auto& b : _state)
            b = static_cast<uint8_t>(dist(gen));
    }

    void MutatePercent(double pct, uint32_t seed)
    {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<size_t> posDist(0, _stateSize - 1);
        std::uniform_int_distribution<> valDist(0, 255);

        size_t changeCnt = static_cast<size_t>(_stateSize * pct);
        for (size_t i = 0; i < changeCnt; ++i)
        {
            size_t pos = posDist(gen);
            _state[pos] = static_cast<uint8_t>(valDist(gen));
        }
    }

    const std::vector<uint8_t>& GetState() const { return _state; }

private:
    size_t _stateSize;
    ttd::PeripheralId _id;
    std::vector<uint8_t> _state;
};

const char* SizeLabel(int64_t bytes)
{
    static char buf[32];
    if (bytes >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%lldMB", bytes / (1024 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%lldKB", bytes / 1024);
    else
        snprintf(buf, sizeof(buf), "%lldB", bytes);
    return buf;
}

} // namespace

// ===========================================================================
// Peripheral capture (TTDSaveState) — size sweep
// ===========================================================================

static void BM_TTD_Peripheral_Capture(benchmark::State& state)
{
    const size_t stateSize = static_cast<size_t>(state.range(0));
    BenchmarkPeripheral peripheral(stateSize, ttd::PeripheralId::GeneralSound);
    peripheral.FillRandom(42);

    std::vector<uint8_t> buffer(stateSize);

    for (auto _ : state)
    {
        peripheral.TTDSaveState(buffer.data());
        benchmark::DoNotOptimize(buffer.data());
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(stateSize));
    state.SetLabel(SizeLabel(stateSize));
    state.counters["state_bytes"] = static_cast<double>(stateSize);
}
BENCHMARK(BM_TTD_Peripheral_Capture)
    ->Arg(256)           // AY register set
    ->Arg(4096)          // Typical peripheral
    ->Arg(32768)         // Large peripheral (WD1793 + track cache)
    ->Arg(524288)        // GeneralSound 512KB SRAM
    ->Unit(benchmark::kNanosecond);

// ===========================================================================
// Peripheral restore (TTDLoadState) — size sweep
// ===========================================================================

static void BM_TTD_Peripheral_Restore(benchmark::State& state)
{
    const size_t stateSize = static_cast<size_t>(state.range(0));
    BenchmarkPeripheral peripheral(stateSize, ttd::PeripheralId::GeneralSound);
    peripheral.FillRandom(42);

    // Capture a reference state
    std::vector<uint8_t> savedState(stateSize);
    peripheral.TTDSaveState(savedState.data());

    for (auto _ : state)
    {
        peripheral.TTDLoadState(savedState.data());
        benchmark::DoNotOptimize(peripheral.GetState().data());
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(stateSize));
    state.SetLabel(SizeLabel(stateSize));
    state.counters["state_bytes"] = static_cast<double>(stateSize);
}
BENCHMARK(BM_TTD_Peripheral_Restore)
    ->Arg(256)
    ->Arg(4096)
    ->Arg(32768)
    ->Arg(524288)
    ->Unit(benchmark::kNanosecond);

// ===========================================================================
// Registry CaptureAll — full state, size sweep
// ===========================================================================

static void BM_TTD_Registry_CaptureAll_Full(benchmark::State& state)
{
    const size_t stateSize = static_cast<size_t>(state.range(0));
    ttd::TTDPeripheralRegistry registry;
    BenchmarkPeripheral peripheral(stateSize, ttd::PeripheralId::GeneralSound);
    peripheral.FillRandom(42);

    registry.Register(ttd::PeripheralId::GeneralSound, &peripheral);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;

    for (auto _ : state)
    {
        blobs.clear();
        registry.CaptureAll(blobs);
        benchmark::DoNotOptimize(blobs);
    }

    // Report compressed size for final iteration
    if (!blobs.empty())
    {
        auto it = blobs.find(static_cast<uint8_t>(ttd::PeripheralId::GeneralSound));
        if (it != blobs.end())
        {
            state.counters["blob_bytes"] = static_cast<double>(it->second.size());
            state.counters["compression_ratio"] = static_cast<double>(stateSize) / it->second.size();
        }
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(stateSize));
    state.SetLabel(std::string("full ") + SizeLabel(stateSize));
    state.counters["state_bytes"] = static_cast<double>(stateSize);
}
BENCHMARK(BM_TTD_Registry_CaptureAll_Full)
    ->Arg(256)
    ->Arg(4096)
    ->Arg(32768)
    ->Arg(524288)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// Registry CaptureAll — small change
// ===========================================================================
//
// The delta-encoding API this sweep originally measured was cut from the
// final TTD design; the registry now compresses full payloads instead. The
// question survives: capture cost and blob size when only changePct of the
// state differs from the previous frame.

static void BM_TTD_Registry_CaptureAll_SmallChange(benchmark::State& state)
{
    const size_t stateSize = static_cast<size_t>(state.range(0));
    const double changePct = state.range(1) / 100.0;

    ttd::TTDPeripheralRegistry registry;
    BenchmarkPeripheral peripheral(stateSize, ttd::PeripheralId::GeneralSound);
    peripheral.FillRandom(42);

    registry.Register(ttd::PeripheralId::GeneralSound, &peripheral);

    // Capture the untouched baseline (full_bytes reference)
    std::unordered_map<uint8_t, std::vector<uint8_t>> prevBlobs;
    registry.CaptureAll(prevBlobs);

    // Apply small mutation
    peripheral.MutatePercent(changePct, 123);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    uint32_t seed = 200;

    for (auto _ : state)
    {
        state.PauseTiming();
        // Reset state to baseline + mutation for each iteration
        peripheral.FillRandom(42);
        peripheral.MutatePercent(changePct, seed++);
        state.ResumeTiming();

        blobs.clear();
        registry.CaptureAll(blobs);
        benchmark::DoNotOptimize(blobs);
    }

    // Report sizes
    if (!blobs.empty())
    {
        auto it = blobs.find(static_cast<uint8_t>(ttd::PeripheralId::GeneralSound));
        if (it != blobs.end())
        {
            state.counters["small_change_blob_bytes"] = static_cast<double>(it->second.size());
        }
    }
    if (!prevBlobs.empty())
    {
        auto it = prevBlobs.find(static_cast<uint8_t>(ttd::PeripheralId::GeneralSound));
        if (it != prevBlobs.end())
        {
            state.counters["full_bytes"] = static_cast<double>(it->second.size());
        }
    }

    state.SetLabel(std::string("small-change ") + SizeLabel(stateSize) +
                   " " + std::to_string(static_cast<int>(changePct * 100)) + "% change");
    state.counters["state_bytes"] = static_cast<double>(stateSize);
    state.counters["change_pct"] = changePct * 100;
}
BENCHMARK(BM_TTD_Registry_CaptureAll_SmallChange)
    ->Args({4096, 1})        // 4KB, 1% change
    ->Args({4096, 5})        // 4KB, 5% change
    ->Args({32768, 1})       // 32KB, 1% change
    ->Args({32768, 5})       // 32KB, 5% change
    ->Args({524288, 1})      // 512KB, 1% change (typical GS audio stream)
    ->Args({524288, 5})      // 512KB, 5% change
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// Registry RestoreAll — full state, size sweep
// ===========================================================================

static void BM_TTD_Registry_RestoreAll_Full(benchmark::State& state)
{
    const size_t stateSize = static_cast<size_t>(state.range(0));
    ttd::TTDPeripheralRegistry registry;
    BenchmarkPeripheral peripheral(stateSize, ttd::PeripheralId::GeneralSound);
    peripheral.FillRandom(42);

    registry.Register(ttd::PeripheralId::GeneralSound, &peripheral);

    // Capture state
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    // Clear peripheral state
    peripheral.FillRandom(999);

    for (auto _ : state)
    {
        registry.RestoreAll(blobs);
        benchmark::DoNotOptimize(peripheral.GetState().data());
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(stateSize));
    state.SetLabel(std::string("full ") + SizeLabel(stateSize));
    state.counters["state_bytes"] = static_cast<double>(stateSize);
}
BENCHMARK(BM_TTD_Registry_RestoreAll_Full)
    ->Arg(256)
    ->Arg(4096)
    ->Arg(32768)
    ->Arg(524288)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// Registry RestoreAll — small-change capture restore
// ===========================================================================
//
// Originally the delta decode path; with delta encoding cut, this restores
// from a capture of a slightly-mutated state (the payload the per-frame
// budget actually has to decode in the shipped design).

static void BM_TTD_Registry_RestoreAll_SmallChange(benchmark::State& state)
{
    const size_t stateSize = static_cast<size_t>(state.range(0));
    const double changePct = state.range(1) / 100.0;

    ttd::TTDPeripheralRegistry registry;
    BenchmarkPeripheral peripheral(stateSize, ttd::PeripheralId::GeneralSound);
    peripheral.FillRandom(42);

    registry.Register(ttd::PeripheralId::GeneralSound, &peripheral);

    // Mutate and capture
    peripheral.MutatePercent(changePct, 123);
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    // Corrupt peripheral state
    peripheral.FillRandom(999);

    for (auto _ : state)
    {
        registry.RestoreAll(blobs);
        benchmark::DoNotOptimize(peripheral.GetState().data());
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(stateSize));
    state.SetLabel(std::string("small-change ") + SizeLabel(stateSize) +
                   " " + std::to_string(static_cast<int>(changePct * 100)) + "% change");
    state.counters["state_bytes"] = static_cast<double>(stateSize);
}
BENCHMARK(BM_TTD_Registry_RestoreAll_SmallChange)
    ->Args({4096, 1})
    ->Args({4096, 5})
    ->Args({32768, 1})
    ->Args({32768, 5})
    ->Args({524288, 1})
    ->Args({524288, 5})
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// Multiple peripherals — realistic config
// ===========================================================================

static void BM_TTD_Registry_MultiPeripheral(benchmark::State& state)
{
    ttd::TTDPeripheralRegistry registry;

    // Simulate realistic config: TurboSound + BetaDisk + Tape + Covox
    BenchmarkPeripheral turboSound(64, ttd::PeripheralId::TurboSound);    // 64B AY regs x2
    BenchmarkPeripheral betaDisk(2048, ttd::PeripheralId::BetaDisk);      // WD1793 state
    BenchmarkPeripheral tape(256, ttd::PeripheralId::Tape);               // Tape state
    BenchmarkPeripheral covox(16, ttd::PeripheralId::Covox);              // Covox DAC

    turboSound.FillRandom(1);
    betaDisk.FillRandom(2);
    tape.FillRandom(3);
    covox.FillRandom(4);

    registry.Register(ttd::PeripheralId::TurboSound, &turboSound);
    registry.Register(ttd::PeripheralId::BetaDisk, &betaDisk);
    registry.Register(ttd::PeripheralId::Tape, &tape);
    registry.Register(ttd::PeripheralId::Covox, &covox);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;

    for (auto _ : state)
    {
        blobs.clear();
        registry.CaptureAll(blobs);
        benchmark::DoNotOptimize(blobs);
    }

    size_t totalState = registry.TotalStateSize();
    size_t totalBlob = 0;
    for (const auto& kv : blobs)
        totalBlob += kv.second.size();

    state.counters["devices"] = 4;
    state.counters["total_state_bytes"] = static_cast<double>(totalState);
    state.counters["total_blob_bytes"] = static_cast<double>(totalBlob);
    state.SetLabel("4 devices: TS+BDI+Tape+Covox");
}
BENCHMARK(BM_TTD_Registry_MultiPeripheral)->Unit(benchmark::kMicrosecond);

// ===========================================================================
// GeneralSound 512KB — payload compression effectiveness
// ===========================================================================
//
// Originally validated the TTD claim that delta encoding achieves >10x
// compression for streaming audio workloads (~5% change per frame). The
// delta encoder was cut from the final design; this now measures how the
// registry's plain payload compression behaves for the same workload.

static void BM_TTD_GeneralSound_CompressionEffectiveness(benchmark::State& state)
{
    constexpr size_t kGSSramSize = 512 * 1024;
    const double changePct = state.range(0) / 100.0;

    ttd::TTDPeripheralRegistry registry;
    BenchmarkPeripheral gs(kGSSramSize, ttd::PeripheralId::GeneralSound);
    gs.FillRandom(42);

    registry.Register(ttd::PeripheralId::GeneralSound, &gs);

    // Capture full baseline
    std::unordered_map<uint8_t, std::vector<uint8_t>> fullBlobs;
    registry.CaptureAll(fullBlobs);
    size_t fullSize = fullBlobs[static_cast<uint8_t>(ttd::PeripheralId::GeneralSound)].size();

    uint32_t seed = 100;
    size_t totalBlobSize = 0;
    size_t captureCount = 0;

    for (auto _ : state)
    {
        state.PauseTiming();
        gs.MutatePercent(changePct, seed++);
        state.ResumeTiming();

        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        registry.CaptureAll(blobs);
        benchmark::DoNotOptimize(blobs);

        totalBlobSize += blobs[static_cast<uint8_t>(ttd::PeripheralId::GeneralSound)].size();
        captureCount++;
    }

    double avgBlobSize = static_cast<double>(totalBlobSize) / captureCount;
    double compressionFactor = static_cast<double>(fullSize) / avgBlobSize;

    state.counters["full_bytes"] = static_cast<double>(fullSize);
    state.counters["avg_blob_bytes"] = avgBlobSize;
    state.counters["compression_factor"] = compressionFactor;
    state.counters["change_pct"] = changePct * 100;
    state.SetLabel(std::to_string(static_cast<int>(changePct * 100)) + "% change");
}
BENCHMARK(BM_TTD_GeneralSound_CompressionEffectiveness)
    ->Arg(1)   // 1% change (~5KB)
    ->Arg(5)   // 5% change (~25KB) — typical audio streaming
    ->Arg(10)  // 10% change
    ->Arg(50)  // 50% change — worst case
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);
