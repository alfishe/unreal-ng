// memory_interface_microbenchmark.cpp — debug-vs-fast memory interface
// throughput, settling the "TTD-lite third interface" question with data.
//
// Sprint 0, Item 0.5 — overhead doc §5 (last row), §6.
//
// The Z80 emulation has two memory interfaces today (see z80.h):
//
//   * FastMemIf — pure read/write into the bank pointer table. No checks,
//     no side effects. This is what runs when debug mode is off.
//   * DbgMemIf — same memory semantics PLUS breakpoint checks and the
//     MemoryAccessTracker pipeline. This is what runs when debug mode is on.
//
// TTD recording needs *one more* kind of side effect: every byte written
// (and optionally every byte read) must be appended to a ring buffer for
// later replay. The architecture question is whether to:
//
//   (a) Reuse DbgMemIf and add TTD logic to it (more branches on the hot
//       path even when neither breakpoints nor tracking are wanted).
//   (b) Introduce a third MemIf variant that ONLY does the TTD ring buffer
//       append — no breakpoint checks, no tracker.
//   (c) Make TTD capture frame-bulk (snapshot all RAM at the frame boundary)
//       and keep using FastMemIf intra-frame.
//
// This benchmark provides the numbers needed to choose. It measures:
//
//   1. FastMemIf read/write throughput (the upper bound).
//   2. DbgMemIf read/write throughput (today's debug-mode cost).
//   3. A "TTD-lite" interface: FastMemIf plus a write-ring-buffer append.
//      Implemented inline here as a *proxy* — when we ship the real
//      interface in a later sprint, swap it in and the numbers will be
//      directly comparable because the access pattern is identical.
//
// The access pattern is a synthetic but representative mix:
//   - Sequential reads (memcpy-like, e.g. LDIR)
//   - Sequential writes (memset-like, e.g. LDIR)
//   - Random reads (stack-machine code)
//   - Random writes (video mem updates)
//
// Run with: ./core-benchmarks --benchmark_filter="TTD_MemIf.*"
//

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace
{

/// Memory bank layout for the microbenchmark. We avoid pulling in the full
/// Memory class — its init pipeline is heavy and not relevant to the
/// throughput question we're answering here.
struct FlatMemory
{
    static constexpr size_t kBankSize = 16 * 1024;       // 16KB per Z80 bank
    static constexpr size_t kNumBanks = 4;               // Four 16KB banks = 64KB
    static constexpr size_t kTotalSize = kBankSize * kNumBanks;

    uint8_t* banks[kNumBanks];

    FlatMemory()
    {
        for (size_t i = 0; i < kNumBanks; ++i)
        {
            banks[i] = static_cast<uint8_t*>(std::aligned_alloc(64, kBankSize));
            std::memset(banks[i], 0, kBankSize);
        }
    }

    ~FlatMemory()
    {
        for (size_t i = 0; i < kNumBanks; ++i) std::free(banks[i]);
    }

    FlatMemory(const FlatMemory&) = delete;
    FlatMemory& operator=(const FlatMemory&) = delete;
};

/// Pre-computed address sequences for random-access patterns.
/// Using a fixed sequence ensures every benchmark variant hits the same
/// addresses, so the comparison is purely about the per-access cost.
struct AccessPattern
{
    std::vector<uint16_t> sequential;  // 0..kTotalSize-1
    std::vector<uint16_t> randomReads;
    std::vector<uint16_t> randomWrites;

    explicit AccessPattern(size_t count)
    {
        sequential.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            sequential.push_back(static_cast<uint16_t>(i % FlatMemory::kTotalSize));
        }
        std::mt19937 rng(98765);
        randomReads.reserve(count);
        randomWrites.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            randomReads.push_back(static_cast<uint16_t>(rng() % FlatMemory::kTotalSize));
            randomWrites.push_back(static_cast<uint16_t>(rng() % FlatMemory::kTotalSize));
        }
    }
};

/// Inline helpers — the compiler will inline these in the FastMemIf path.
inline uint8_t FastRead(const FlatMemory& m, uint16_t addr)
{
    const size_t bank = addr >> 14;
    const size_t off  = addr & 0x3FFF;
    return m.banks[bank][off];
}

inline void FastWrite(FlatMemory& m, uint16_t addr, uint8_t v)
{
    const size_t bank = addr >> 14;
    const size_t off  = addr & 0x3FFF;
    m.banks[bank][off] = v;
}

/// DbgMemIf-equivalent: same as Fast plus a breakpoint-lookup branch.
/// We approximate the real DbgMemIf cost by including a single integer
/// compare against a "would-this-address-hit-a-breakpoint" predicate.
/// The real DbgMemIf walks a BreakpointManager structure; this proxy
/// uses a bitmap for O(1) lookup, which is *faster* than the real thing,
/// so it's a conservative lower bound on the debug-mode cost.
struct BreakpointBitmap
{
    std::vector<bool> hits;  // hits[addr] = true if a breakpoint is set
    BreakpointBitmap() : hits(FlatMemory::kTotalSize, false)
    {
        // Sprinkle a few breakpoints (typical user has <20 set).
        for (int i = 0; i < 16; ++i) hits[i * 4096] = true;
    }
};

inline uint8_t DbgRead(const FlatMemory& m, const BreakpointBitmap& bp, uint16_t addr)
{
    const size_t bank = addr >> 14;
    const size_t off  = addr & 0x3FFF;
    const uint8_t v   = m.banks[bank][off];
    if (bp.hits[addr]) { /* would call HandleMemoryRead; elided */ }
    return v;
}

inline void DbgWrite(FlatMemory& m, const BreakpointBitmap& bp, uint16_t addr, uint8_t v)
{
    const size_t bank = addr >> 14;
    const size_t off  = addr & 0x3FFF;
    m.banks[bank][off] = v;
    if (bp.hits[addr]) { /* would call HandleMemoryWrite; elided */ }
}

/// TTD-lite interface: Fast plus a write-ring-buffer append (reads are free).
/// This is the *lower bound* on what a real TTD recorder would pay per write.
struct TTDRingBuffer
{
    struct Record { uint16_t addr; uint8_t value; };
    std::vector<Record> records;
    size_t head = 0;

    explicit TTDRingBuffer(size_t capacity) : records(capacity), head(0) {}

    inline void Append(uint16_t addr, uint8_t value)
    {
        records[head] = Record{addr, value};
        head = (head + 1) & (records.size() - 1);  // Power-of-2 wrap
    }
};

inline void TTDWrite(FlatMemory& m, TTDRingBuffer& rb, uint16_t addr, uint8_t v)
{
    const size_t bank = addr >> 14;
    const size_t off  = addr & 0x3FFF;
    m.banks[bank][off] = v;
    rb.Append(addr, v);
}

} // namespace

/// region <FastMemIf baseline>

static void BM_TTD_MemIf_Fast_SequentialReads(benchmark::State& state)
{
    FlatMemory mem;
    AccessPattern pat(64 * 1024);
    uint8_t sink = 0;
    for (auto _ : state)
    {
        for (uint16_t a : pat.sequential) sink ^= FastRead(mem, a);
        benchmark::DoNotOptimize(sink);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.sequential.size()));
}
BENCHMARK(BM_TTD_MemIf_Fast_SequentialReads)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_Fast_SequentialWrites(benchmark::State& state)
{
    FlatMemory mem;
    AccessPattern pat(64 * 1024);
    for (auto _ : state)
    {
        uint8_t v = 0;
        for (uint16_t a : pat.sequential) FastWrite(mem, a, v++);
        benchmark::DoNotOptimize(mem.banks[0][0]);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.sequential.size()));
}
BENCHMARK(BM_TTD_MemIf_Fast_SequentialWrites)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_Fast_RandomReads(benchmark::State& state)
{
    FlatMemory mem;
    AccessPattern pat(64 * 1024);
    uint8_t sink = 0;
    for (auto _ : state)
    {
        for (uint16_t a : pat.randomReads) sink ^= FastRead(mem, a);
        benchmark::DoNotOptimize(sink);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.randomReads.size()));
}
BENCHMARK(BM_TTD_MemIf_Fast_RandomReads)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_Fast_RandomWrites(benchmark::State& state)
{
    FlatMemory mem;
    AccessPattern pat(64 * 1024);
    for (auto _ : state)
    {
        uint8_t v = 0;
        for (uint16_t a : pat.randomWrites) FastWrite(mem, a, v++);
        benchmark::DoNotOptimize(mem.banks[0][0]);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.randomWrites.size()));
}
BENCHMARK(BM_TTD_MemIf_Fast_RandomWrites)->Unit(benchmark::kNanosecond);

/// endregion </FastMemIf baseline>

/// region <DbgMemIf>

static void BM_TTD_MemIf_Dbg_SequentialReads(benchmark::State& state)
{
    FlatMemory mem;
    BreakpointBitmap bp;
    AccessPattern pat(64 * 1024);
    uint8_t sink = 0;
    for (auto _ : state)
    {
        for (uint16_t a : pat.sequential) sink ^= DbgRead(mem, bp, a);
        benchmark::DoNotOptimize(sink);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.sequential.size()));
}
BENCHMARK(BM_TTD_MemIf_Dbg_SequentialReads)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_Dbg_SequentialWrites(benchmark::State& state)
{
    FlatMemory mem;
    BreakpointBitmap bp;
    AccessPattern pat(64 * 1024);
    for (auto _ : state)
    {
        uint8_t v = 0;
        for (uint16_t a : pat.sequential) DbgWrite(mem, bp, a, v++);
        benchmark::DoNotOptimize(mem.banks[0][0]);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.sequential.size()));
}
BENCHMARK(BM_TTD_MemIf_Dbg_SequentialWrites)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_Dbg_RandomReads(benchmark::State& state)
{
    FlatMemory mem;
    BreakpointBitmap bp;
    AccessPattern pat(64 * 1024);
    uint8_t sink = 0;
    for (auto _ : state)
    {
        for (uint16_t a : pat.randomReads) sink ^= DbgRead(mem, bp, a);
        benchmark::DoNotOptimize(sink);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.randomReads.size()));
}
BENCHMARK(BM_TTD_MemIf_Dbg_RandomReads)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_Dbg_RandomWrites(benchmark::State& state)
{
    FlatMemory mem;
    BreakpointBitmap bp;
    AccessPattern pat(64 * 1024);
    for (auto _ : state)
    {
        uint8_t v = 0;
        for (uint16_t a : pat.randomWrites) DbgWrite(mem, bp, a, v++);
        benchmark::DoNotOptimize(mem.banks[0][0]);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.randomWrites.size()));
}
BENCHMARK(BM_TTD_MemIf_Dbg_RandomWrites)->Unit(benchmark::kNanosecond);

/// endregion </DbgMemIf>

/// region <TTD-lite third interface (proxy)>

static void BM_TTD_MemIf_TTDLite_SequentialWrites(benchmark::State& state)
{
    FlatMemory mem;
    TTDRingBuffer rb(1 << 20);  // 1M entries (~3MB) — well within L2/L3
    AccessPattern pat(64 * 1024);
    for (auto _ : state)
    {
        uint8_t v = 0;
        for (uint16_t a : pat.sequential) TTDWrite(mem, rb, a, v++);
        benchmark::DoNotOptimize(mem.banks[0][0]);
        benchmark::DoNotOptimize(rb.head);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.sequential.size()));
}
BENCHMARK(BM_TTD_MemIf_TTDLite_SequentialWrites)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_TTDLite_RandomWrites(benchmark::State& state)
{
    FlatMemory mem;
    TTDRingBuffer rb(1 << 20);
    AccessPattern pat(64 * 1024);
    for (auto _ : state)
    {
        uint8_t v = 0;
        for (uint16_t a : pat.randomWrites) TTDWrite(mem, rb, a, v++);
        benchmark::DoNotOptimize(mem.banks[0][0]);
        benchmark::DoNotOptimize(rb.head);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.randomWrites.size()));
}
BENCHMARK(BM_TTD_MemIf_TTDLite_RandomWrites)->Unit(benchmark::kNanosecond);

static void BM_TTD_MemIf_TTDLite_RandomReads(benchmark::State& state)
{
    // Reads are *not* recorded in this TTD-lite variant — they should match
    // the FastMemIf read cost. This benchmark confirms that.
    FlatMemory mem;
    TTDRingBuffer rb(1 << 20);
    AccessPattern pat(64 * 1024);
    uint8_t sink = 0;
    for (auto _ : state)
    {
        for (uint16_t a : pat.randomReads) sink ^= FastRead(mem, a);
        benchmark::DoNotOptimize(sink);
        benchmark::DoNotOptimize(rb.head);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pat.randomReads.size()));
}
BENCHMARK(BM_TTD_MemIf_TTDLite_RandomReads)->Unit(benchmark::kNanosecond);

/// endregion </TTD-lite third interface (proxy)>
