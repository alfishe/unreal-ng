/// @file ttd_v2_compression_bench.cpp
/// @brief Compare compression approaches for TTD frame data using zstd
///
/// Three approaches compared:
/// 1. XOR + zstd           - XOR frames, compress with zstd-1
/// 2. Delta + zstd         - Sparse delta encode, compress with zstd-1
/// 3. XOR + Delta + zstd   - XOR frames, then delta encode, then zstd-1
///
/// Tests BOTH memory content (GS SRAM) AND TTD struct records.

#include <benchmark/benchmark.h>
#include <vector>
#include <random>
#include <cstring>
#include <cmath>
#include <zstd.h>

constexpr int kZstdLevel = 1;

std::vector<uint8_t> CompressZstd(const uint8_t* src, size_t len)
{
    size_t bound = ZSTD_compressBound(len);
    std::vector<uint8_t> out(bound);
    size_t n = ZSTD_compress(out.data(), out.capacity(), src, len, kZstdLevel);
    if (ZSTD_isError(n)) return {};
    out.resize(n);
    return out;
}

bool DecompressZstd(const std::vector<uint8_t>& compressed, uint8_t* out, size_t outLen)
{
    size_t n = ZSTD_decompress(out, outLen, compressed.data(), compressed.size());
    return !ZSTD_isError(n) && n == outLen;
}

struct DeltaRun {
    uint32_t offset;
    uint16_t length;
};

std::vector<uint8_t> EncodeDelta(const uint8_t* data, size_t len)
{
    std::vector<uint8_t> out;
    out.reserve(len / 4);
    size_t i = 0;
    while (i < len) {
        while (i < len && data[i] == 0) ++i;
        if (i >= len) break;
        size_t start = i;
        while (i < len && data[i] != 0 && (i - start) < 65535) ++i;
        DeltaRun run{static_cast<uint32_t>(start), static_cast<uint16_t>(i - start)};
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&run), reinterpret_cast<uint8_t*>(&run) + sizeof(run));
        out.insert(out.end(), data + start, data + i);
    }
    return out;
}

void DecodeDelta(const uint8_t* encoded, size_t encodedLen, uint8_t* out, size_t outLen)
{
    std::memset(out, 0, outLen);
    size_t pos = 0;
    while (pos + sizeof(DeltaRun) <= encodedLen) {
        DeltaRun run;
        std::memcpy(&run, encoded + pos, sizeof(run));
        pos += sizeof(run);
        if (pos + run.length > encodedLen) break;
        std::memcpy(out + run.offset, encoded + pos, run.length);
        pos += run.length;
    }
}

static void BM_TTD_Compress_XOR_Zstd_Random(benchmark::State& state) {
    const size_t gsSize = 512 * 1024;
    const double pct = state.range(0) / 1000.0;
    
    std::vector<uint8_t> prev(gsSize), curr(gsSize);
    for (size_t i = 0; i < gsSize; ++i) prev[i] = static_cast<uint8_t>(128 + 64 * std::sin(i * 0.01));
    curr = prev;
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> posDist(0, gsSize - 1);
    std::uniform_int_distribution<> byteDist(0, 255);
    size_t changes = static_cast<size_t>(gsSize * pct);
    for (size_t i = 0; i < changes; ++i) curr[posDist(gen)] = byteDist(gen);
        
    std::vector<uint8_t> xored(gsSize);
    for (auto _ : state) {
        for (size_t i = 0; i < gsSize; ++i) xored[i] = curr[i] ^ prev[i];
        auto out = CompressZstd(xored.data(), gsSize);
        benchmark::DoNotOptimize(out);
        state.counters["size_B"] = out.size();
    }
}
BENCHMARK(BM_TTD_Compress_XOR_Zstd_Random)->Arg(1)->Arg(10)->Unit(benchmark::kMicrosecond);

static void BM_TTD_Compress_Delta_Zstd_Random(benchmark::State& state) {
    const size_t gsSize = 512 * 1024;
    const double pct = state.range(0) / 1000.0;
    
    std::vector<uint8_t> prev(gsSize), curr(gsSize);
    for (size_t i = 0; i < gsSize; ++i) prev[i] = static_cast<uint8_t>(128 + 64 * std::sin(i * 0.01));
    curr = prev;
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> posDist(0, gsSize - 1);
    std::uniform_int_distribution<> byteDist(0, 255);
    size_t changes = static_cast<size_t>(gsSize * pct);
    for (size_t i = 0; i < changes; ++i) curr[posDist(gen)] = byteDist(gen);
        
    for (auto _ : state) {
        auto delta = EncodeDelta(curr.data(), gsSize);
        auto out = CompressZstd(delta.data(), delta.size());
        benchmark::DoNotOptimize(out);
        state.counters["size_B"] = out.size();
    }
}
BENCHMARK(BM_TTD_Compress_Delta_Zstd_Random)->Arg(1)->Arg(10)->Unit(benchmark::kMicrosecond);

static void BM_TTD_Compress_XOR_Delta_Zstd_Random(benchmark::State& state) {
    const size_t gsSize = 512 * 1024;
    const double pct = state.range(0) / 1000.0;
    
    std::vector<uint8_t> prev(gsSize), curr(gsSize);
    for (size_t i = 0; i < gsSize; ++i) prev[i] = static_cast<uint8_t>(128 + 64 * std::sin(i * 0.01));
    curr = prev;
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> posDist(0, gsSize - 1);
    std::uniform_int_distribution<> byteDist(0, 255);
    size_t changes = static_cast<size_t>(gsSize * pct);
    for (size_t i = 0; i < changes; ++i) curr[posDist(gen)] = byteDist(gen);
        
    std::vector<uint8_t> xored(gsSize);
    for (auto _ : state) {
        for (size_t i = 0; i < gsSize; ++i) xored[i] = curr[i] ^ prev[i];
        auto delta = EncodeDelta(xored.data(), gsSize);
        auto out = CompressZstd(delta.data(), delta.size());
        benchmark::DoNotOptimize(out);
        state.counters["size_B"] = out.size();
    }
}
BENCHMARK(BM_TTD_Compress_XOR_Delta_Zstd_Random)->Arg(1)->Arg(10)->Unit(benchmark::kMicrosecond);

static void BM_TTD_Compress_XOR_Zstd_Localized(benchmark::State& state) {
    const size_t gsSize = 512 * 1024;
    const double pct = state.range(0) / 100.0;
    const size_t regions = state.range(1);
    
    std::vector<uint8_t> prev(gsSize), curr(gsSize);
    for (size_t i = 0; i < gsSize; ++i) prev[i] = static_cast<uint8_t>(128 + 64 * std::sin(i * 0.01));
    curr = prev;
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> regionDist(0, gsSize - 256);
    std::uniform_int_distribution<> byteDist(0, 255);
    size_t changesPerRegion = static_cast<size_t>(gsSize * pct) / regions;
    
    for (size_t r = 0; r < regions; ++r) {
        size_t base = regionDist(gen);
        std::uniform_int_distribution<size_t> offsetDist(0, 255);
        for (size_t i = 0; i < changesPerRegion; ++i) curr[base + offsetDist(gen)] = byteDist(gen);
    }
    
    std::vector<uint8_t> xored(gsSize);
    for (auto _ : state) {
        for (size_t i = 0; i < gsSize; ++i) xored[i] = curr[i] ^ prev[i];
        auto out = CompressZstd(xored.data(), gsSize);
        benchmark::DoNotOptimize(out);
        state.counters["size_B"] = out.size();
    }
}
BENCHMARK(BM_TTD_Compress_XOR_Zstd_Localized)->Args({1, 1})->Args({5, 1})->Unit(benchmark::kMicrosecond);

static void BM_TTD_Compress_Delta_Zstd_Localized(benchmark::State& state) {
    const size_t gsSize = 512 * 1024;
    const double pct = state.range(0) / 100.0;
    const size_t regions = state.range(1);
    
    std::vector<uint8_t> prev(gsSize), curr(gsSize);
    for (size_t i = 0; i < gsSize; ++i) prev[i] = static_cast<uint8_t>(128 + 64 * std::sin(i * 0.01));
    curr = prev;
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> regionDist(0, gsSize - 256);
    std::uniform_int_distribution<> byteDist(0, 255);
    size_t changesPerRegion = static_cast<size_t>(gsSize * pct) / regions;
    
    for (size_t r = 0; r < regions; ++r) {
        size_t base = regionDist(gen);
        std::uniform_int_distribution<size_t> offsetDist(0, 255);
        for (size_t i = 0; i < changesPerRegion; ++i) curr[base + offsetDist(gen)] = byteDist(gen);
    }
    
    for (auto _ : state) {
        auto delta = EncodeDelta(curr.data(), gsSize);
        auto out = CompressZstd(delta.data(), delta.size());
        benchmark::DoNotOptimize(out);
        state.counters["size_B"] = out.size();
    }
}
BENCHMARK(BM_TTD_Compress_Delta_Zstd_Localized)->Args({1, 1})->Args({5, 1})->Unit(benchmark::kMicrosecond);

static void BM_TTD_Compress_XOR_Delta_Zstd_Localized(benchmark::State& state) {
    const size_t gsSize = 512 * 1024;
    const double pct = state.range(0) / 100.0;
    const size_t regions = state.range(1);
    
    std::vector<uint8_t> prev(gsSize), curr(gsSize);
    for (size_t i = 0; i < gsSize; ++i) prev[i] = static_cast<uint8_t>(128 + 64 * std::sin(i * 0.01));
    curr = prev;
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> regionDist(0, gsSize - 256);
    std::uniform_int_distribution<> byteDist(0, 255);
    size_t changesPerRegion = static_cast<size_t>(gsSize * pct) / regions;
    
    for (size_t r = 0; r < regions; ++r) {
        size_t base = regionDist(gen);
        std::uniform_int_distribution<size_t> offsetDist(0, 255);
        for (size_t i = 0; i < changesPerRegion; ++i) curr[base + offsetDist(gen)] = byteDist(gen);
    }
    
    std::vector<uint8_t> xored(gsSize);
    for (auto _ : state) {
        for (size_t i = 0; i < gsSize; ++i) xored[i] = curr[i] ^ prev[i];
        auto delta = EncodeDelta(xored.data(), gsSize);
        auto out = CompressZstd(delta.data(), delta.size());
        benchmark::DoNotOptimize(out);
        state.counters["size_B"] = out.size();
    }
}
BENCHMARK(BM_TTD_Compress_XOR_Delta_Zstd_Localized)->Args({1, 1})->Args({5, 1})->Unit(benchmark::kMicrosecond);

struct TTDCpuState {
    uint16_t pc, sp, af, bc, de, hl, ix, iy;
    uint16_t alt_af, alt_bc, alt_de, alt_hl;
    uint8_t  i, r_low, r_hi, iff1, iff2, im, halted, reserved0;
    uint16_t memptr;
    uint8_t  q;
    uint16_t eipos, haltpos;
    uint8_t  nmi_in_progress, int_pending, int_gate, reserved1;
    uint32_t halt_cycle;
};

static void BM_TTD_Compress_CPU_Struct(benchmark::State& state) {
    TTDCpuState prev{}, curr{};
    prev.pc = 0x8000; prev.sp = 0xFFFF; prev.af = 0x0044; prev.bc = 0x1234;
    prev.halt_cycle = 100000;
    curr = prev;
    
    switch (state.range(0)) {
        case 0: curr.pc = 0x8003; break;
        case 1: curr.pc = 0x8050; curr.sp = 0xFFF8; curr.af = 0x0145; curr.r_low++; break;
        case 2: curr.pc = 0x0038; curr.sp = 0xFFF0; curr.af = 0xFF00; curr.iff1 = 0; curr.halted = 0; curr.int_pending = 1; break;
    }
    
    std::vector<uint8_t> xored(sizeof(TTDCpuState));
    auto* prevPtr = reinterpret_cast<uint8_t*>(&prev);
    auto* currPtr = reinterpret_cast<uint8_t*>(&curr);
    for (size_t i = 0; i < sizeof(TTDCpuState); ++i)
        xored[i] = currPtr[i] ^ prevPtr[i];
        
    for (auto _ : state) {
        auto m1Out = CompressZstd(xored.data(), sizeof(TTDCpuState));
        benchmark::DoNotOptimize(m1Out);
        state.counters["xor_zstd_bytes"] = m1Out.size();
    }
}
BENCHMARK(BM_TTD_Compress_CPU_Struct)->Arg(0)->Arg(1)->Arg(2)->Unit(benchmark::kMicrosecond);

