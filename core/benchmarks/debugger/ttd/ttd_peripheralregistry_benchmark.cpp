#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"

/// Peripheral capture / restore of the TTD registry on the worst case: 32 KB of incompressible data (zstd does maximum
/// work). Moved from the unit test TTDPeripheralRegistryTest.PerformanceBudget, which asserted microseconds per
/// iteration (a speed of the host and of the build type: ~20 us Release, ~1000 us Debug). The old gate's budget was
/// 10 000 us per iteration for both; compare the counters below against it
namespace
{
class Peripheral : public ttd::TTDSerializable
{
public:
    explicit Peripheral(size_t size) : _state(size, 0) {}
    size_t TTDStateSize() const override { return _state.size(); }
    void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, _state.data(), _state.size()); }
    void TTDLoadState(const uint8_t* src) override { std::memcpy(_state.data(), src, _state.size()); }
    std::string TTDDeviceName() const override { return "BenchPeripheral"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::GeneralSound; }
    std::vector<uint8_t> _state;
};

void BM_TTDPeripheralRegistry_Capture(benchmark::State& state)
{
    constexpr size_t kSize = 32768;
    Peripheral large(kSize);
    std::mt19937 gen(0x5EED);
    for (auto& b : large._state)
        b = static_cast<uint8_t>(gen());
    ttd::TTDPeripheralRegistry registry;
    registry.Register(&large);
    for (auto _ : state)
    {
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        registry.CaptureAll(blobs);
        benchmark::DoNotOptimize(blobs);
    }
}
BENCHMARK(BM_TTDPeripheralRegistry_Capture)->Unit(benchmark::kMicrosecond);

void BM_TTDPeripheralRegistry_Restore(benchmark::State& state)
{
    constexpr size_t kSize = 32768;
    Peripheral large(kSize);
    std::mt19937 gen(0x5EED);
    for (auto& b : large._state)
        b = static_cast<uint8_t>(gen());
    ttd::TTDPeripheralRegistry registry;
    registry.Register(&large);
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    for (auto _ : state)
        registry.RestoreAll(blobs);
}
BENCHMARK(BM_TTDPeripheralRegistry_Restore)->Unit(benchmark::kMicrosecond);
}  // namespace
