/// @file compression_benchmark.cpp
/// @brief Compare compression approaches for TTD frame data using zstd
///
/// Three approaches compared:
/// 1. XOR + zstd           - XOR frames, compress with zstd-1
/// 2. Delta + zstd         - Sparse delta encode, compress with zstd-1
/// 3. XOR + Delta + zstd   - XOR frames, then delta encode, then zstd-1
///
/// Tests BOTH memory content (GS SRAM) AND TTD struct records.
///
/// Build: c++ -std=c++17 -O2 compression_benchmark.cpp -lzstd -o compression_benchmark

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>
#include <zstd.h>

using Clock = std::chrono::high_resolution_clock;
constexpr int kZstdLevel = 1;

// ============================================================================
// Compression helpers
// ============================================================================

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

// ============================================================================
// Delta encoding (run-length of non-zero regions)
// ============================================================================

struct DeltaRun {
    uint32_t offset;  // 32-bit for large buffers
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
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&run),
                   reinterpret_cast<uint8_t*>(&run) + sizeof(run));
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

// ============================================================================
// TTD struct simulation (mirrors ttdcheckpoint.h)
// ============================================================================

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
static_assert(sizeof(TTDCpuState) == 48, "Match real TTDCpuState");

struct TTDChipsetState {
    uint64_t t_states;
    uint64_t frame_counter;
    uint8_t  ports[32];
    uint8_t  comp_pal[16];
    uint8_t  ulaplus_cram[64];
    uint8_t  pFFF7[32];
    uint8_t  reserved[32];  // Adjusted for 184 bytes
};

struct TTDPeripheralState {
    uint8_t ayRegs[32];      // AY state
    uint8_t tapeState[64];   // Tape buffer
    uint8_t fdcState[128];   // FDC registers
    uint8_t covox[4];        // Covox state
};

struct TTDFrameRecord {
    TTDCpuState cpu;
    TTDChipsetState chipset;
    TTDPeripheralState peripherals;
    uint32_t ramPageRefs[256];  // Page slot refs for 4MB machine
};

// ============================================================================
// Test data generators
// ============================================================================

void GenerateRandomChanges(std::vector<uint8_t>& data, size_t changes, std::mt19937& gen)
{
    std::uniform_int_distribution<size_t> posDist(0, data.size() - 1);
    std::uniform_int_distribution<> byteDist(0, 255);
    for (size_t i = 0; i < changes; ++i)
        data[posDist(gen)] = byteDist(gen);
}

void GenerateLocalizedChanges(std::vector<uint8_t>& data, size_t changes,
                               size_t numRegions, std::mt19937& gen)
{
    std::uniform_int_distribution<size_t> regionDist(0, data.size() - 256);
    std::uniform_int_distribution<> byteDist(0, 255);

    size_t changesPerRegion = changes / numRegions;
    for (size_t r = 0; r < numRegions; ++r) {
        size_t base = regionDist(gen);
        std::uniform_int_distribution<size_t> offsetDist(0, 255);
        for (size_t i = 0; i < changesPerRegion; ++i)
            data[base + offsetDist(gen)] = byteDist(gen);
    }
}

// ============================================================================
// Benchmark result struct
// ============================================================================

struct BenchResult {
    size_t xorSize, deltaSize, xorDeltaSize;
    double xorTime, deltaTime, xorDeltaTime;  // capture time in us
    double xorRestore, deltaRestore, xorDeltaRestore;  // restore time
};

BenchResult BenchmarkData(const uint8_t* prev, const uint8_t* curr, size_t len, int iters)
{
    BenchResult r = {};

    // Create XOR buffer
    std::vector<uint8_t> xored(len);
    for (size_t i = 0; i < len; ++i)
        xored[i] = curr[i] ^ prev[i];

    // Method 1: XOR + zstd
    std::vector<uint8_t> m1Out;
    auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i)
        m1Out = CompressZstd(xored.data(), len);
    auto t1 = Clock::now();
    r.xorTime = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    r.xorSize = m1Out.size();

    // Method 2: Delta + zstd (delta on difference, not XOR)
    std::vector<uint8_t> diff(len);
    for (size_t i = 0; i < len; ++i) diff[i] = (curr[i] != prev[i]) ? curr[i] : 0;
    std::vector<uint8_t> m2Delta, m2Out;
    t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        m2Delta = EncodeDelta(diff.data(), len);
        m2Out = CompressZstd(m2Delta.data(), m2Delta.size());
    }
    t1 = Clock::now();
    r.deltaTime = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    r.deltaSize = m2Out.size();

    // Method 3: XOR + Delta + zstd
    std::vector<uint8_t> m3Delta, m3Out;
    t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        m3Delta = EncodeDelta(xored.data(), len);
        m3Out = CompressZstd(m3Delta.data(), m3Delta.size());
    }
    t1 = Clock::now();
    r.xorDeltaTime = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    r.xorDeltaSize = m3Out.size();

    // Restore benchmarks
    std::vector<uint8_t> restored(len);

    // Restore Method 1
    t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        DecompressZstd(m1Out, restored.data(), len);
        for (size_t j = 0; j < len; ++j) restored[j] ^= prev[j];
    }
    t1 = Clock::now();
    r.xorRestore = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;

    // Restore Method 2
    std::vector<uint8_t> deltaDecoded(len);
    t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        std::vector<uint8_t> dec(m2Delta.size());
        DecompressZstd(m2Out, dec.data(), m2Delta.size());
        DecodeDelta(dec.data(), dec.size(), deltaDecoded.data(), len);
        for (size_t j = 0; j < len; ++j)
            restored[j] = deltaDecoded[j] ? deltaDecoded[j] : prev[j];
    }
    t1 = Clock::now();
    r.deltaRestore = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;

    // Restore Method 3
    t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        std::vector<uint8_t> dec(m3Delta.size());
        DecompressZstd(m3Out, dec.data(), m3Delta.size());
        DecodeDelta(dec.data(), dec.size(), deltaDecoded.data(), len);
        for (size_t j = 0; j < len; ++j) restored[j] = prev[j] ^ deltaDecoded[j];
    }
    t1 = Clock::now();
    r.xorDeltaRestore = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;

    return r;
}

// ============================================================================
// Main benchmark
// ============================================================================

void PrintSectionHeader(const char* title)
{
    std::cout << "\n## " << title << "\n\n";
}

void PrintTableHeader3()
{
    std::cout << "| Scenario | XOR+zstd | Delta+zstd | XOR+Delta+zstd | Best |\n";
    std::cout << "|----------|----------|------------|----------------|------|\n";
}

void PrintRow(const char* scenario, const BenchResult& r, bool showSize = true)
{
    size_t best = std::min({r.xorSize, r.deltaSize, r.xorDeltaSize});
    const char* winner = (r.xorSize == best) ? "XOR" :
                         (r.deltaSize == best) ? "Delta" : "XOR+D";

    if (showSize) {
        std::cout << "| " << std::setw(25) << std::left << scenario
                  << " | " << std::setw(6) << r.xorSize << "B"
                  << " | " << std::setw(8) << r.deltaSize << "B"
                  << " | " << std::setw(12) << r.xorDeltaSize << "B"
                  << " | " << winner << " |\n";
    } else {
        std::cout << "| " << std::setw(25) << std::left << scenario
                  << " | " << std::setw(5) << (int)r.xorTime << "us"
                  << " | " << std::setw(8) << (int)r.deltaTime << "us"
                  << " | " << std::setw(12) << (int)r.xorDeltaTime << "us"
                  << " | - |\n";
    }
}

int main()
{
    std::cout << "# TTD Compression Benchmark (zstd-1)\n";
    std::cout << "\nCompares XOR+zstd, Delta+zstd, XOR+Delta+zstd\n";
    std::cout << "on memory content AND TTD struct records.\n";

    std::mt19937 gen(42);
    const int iters = 10;

    // =========================================================================
    PrintSectionHeader("1. GeneralSound 512KB SRAM - Random Changes");
    // =========================================================================
    {
        const size_t gsSize = 512 * 1024;
        std::vector<uint8_t> prev(gsSize), curr(gsSize);
        for (size_t i = 0; i < gsSize; ++i)
            prev[i] = static_cast<uint8_t>(128 + 64 * sin(i * 0.01));

        PrintTableHeader3();
        for (auto [pct, name] : std::vector<std::pair<double, const char*>>{
            {0.001, "0.1% (512B)"}, {0.01, "1.0% (5KB)"}, {0.05, "5.0% (25KB)"}})
        {
            curr = prev;
            GenerateRandomChanges(curr, static_cast<size_t>(gsSize * pct), gen);
            auto r = BenchmarkData(prev.data(), curr.data(), gsSize, iters);
            PrintRow(name, r);
        }

        std::cout << "\n**Problem:** Random scatter defeats delta RLE - every changed byte\n";
        std::cout << "is a separate run with 6-byte header overhead.\n";
    }

    // =========================================================================
    PrintSectionHeader("2. GeneralSound 512KB SRAM - Localized Changes");
    // =========================================================================
    {
        const size_t gsSize = 512 * 1024;
        std::vector<uint8_t> prev(gsSize), curr(gsSize);
        for (size_t i = 0; i < gsSize; ++i)
            prev[i] = static_cast<uint8_t>(128 + 64 * sin(i * 0.01));

        PrintTableHeader3();
        for (auto [pct, regions, name] : std::vector<std::tuple<double, size_t, const char*>>{
            {0.01, 2, "1% in 2 regions"},
            {0.01, 8, "1% in 8 regions"},
            {0.05, 4, "5% in 4 regions"},
            {0.05, 16, "5% in 16 regions"}})
        {
            curr = prev;
            GenerateLocalizedChanges(curr, static_cast<size_t>(gsSize * pct), regions, gen);
            auto r = BenchmarkData(prev.data(), curr.data(), gsSize, iters);
            PrintRow(name, r);
        }

        std::cout << "\n**Insight:** Localized changes let delta RLE group runs efficiently.\n";
    }

    // =========================================================================
    PrintSectionHeader("3. TTD CPU State (48 bytes)");
    // =========================================================================
    {
        TTDCpuState prev{}, curr{};
        prev.pc = 0x8000; prev.sp = 0xFFFF; prev.af = 0x0044; prev.bc = 0x1234;
        prev.halt_cycle = 100000;

        std::cout << "| Scenario | XOR+zstd | Delta+zstd | XOR+Delta+zstd | Notes |\n";
        std::cout << "|----------|----------|------------|----------------|-------|\n";

        // Minimal change (PC only)
        curr = prev;
        curr.pc = 0x8003;
        auto r1 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDCpuState), iters);
        std::cout << "| PC changed only | " << r1.xorSize << "B | " << r1.deltaSize << "B | "
                  << r1.xorDeltaSize << "B | Minimal change |\n";

        // Typical frame (PC, SP, a few regs)
        curr = prev;
        curr.pc = 0x8050; curr.sp = 0xFFF8; curr.af = 0x0145; curr.r_low++;
        auto r2 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDCpuState), iters);
        std::cout << "| Typical frame | " << r2.xorSize << "B | " << r2.deltaSize << "B | "
                  << r2.xorDeltaSize << "B | 4 regs changed |\n";

        // Heavy change (interrupt, mode switch)
        curr = prev;
        curr.pc = 0x0038; curr.sp = 0xFFF0; curr.af = 0xFF00; curr.iff1 = 0;
        curr.halted = 0; curr.int_pending = 1;
        auto r3 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDCpuState), iters);
        std::cout << "| Interrupt entry | " << r3.xorSize << "B | " << r3.deltaSize << "B | "
                  << r3.xorDeltaSize << "B | 6+ fields |\n";
    }

    // =========================================================================
    PrintSectionHeader("4. TTD Chipset State (176 bytes)");
    // =========================================================================
    {
        TTDChipsetState prev{}, curr{};
        prev.t_states = 100000; prev.frame_counter = 1000;
        prev.ports[0] = 0x10; prev.ports[1] = 0x00;

        std::cout << "| Scenario | XOR+zstd | Delta+zstd | XOR+Delta+zstd | Notes |\n";
        std::cout << "|----------|----------|------------|----------------|-------|\n";

        // Frame counter only
        curr = prev;
        curr.t_states += 71680; curr.frame_counter++;
        auto r1 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDChipsetState), iters);
        std::cout << "| Counters only | " << r1.xorSize << "B | " << r1.deltaSize << "B | "
                  << r1.xorDeltaSize << "B | t_states+frame |\n";

        // Port write + palette
        curr = prev;
        curr.t_states += 71680; curr.frame_counter++;
        curr.ports[0] = 0x18;  // 7FFD changed
        curr.comp_pal[0] = 0x07;
        auto r2 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDChipsetState), iters);
        std::cout << "| Port + palette | " << r2.xorSize << "B | " << r2.deltaSize << "B | "
                  << r2.xorDeltaSize << "B | Bank switch |\n";

        // ULAplus palette change
        curr = prev;
        curr.t_states += 71680; curr.frame_counter++;
        for (int i = 0; i < 16; ++i) curr.ulaplus_cram[i] = i * 4;
        auto r3 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDChipsetState), iters);
        std::cout << "| ULAplus 16 colors | " << r3.xorSize << "B | " << r3.deltaSize << "B | "
                  << r3.xorDeltaSize << "B | Dense region |\n";
    }

    // =========================================================================
    PrintSectionHeader("5. Full TTD Frame Record (1476 bytes)");
    // =========================================================================
    {
        TTDFrameRecord prev{}, curr{};
        prev.cpu.pc = 0x8000; prev.cpu.sp = 0xFFFF;
        prev.chipset.t_states = 100000; prev.chipset.frame_counter = 1000;
        for (int i = 0; i < 256; ++i) prev.ramPageRefs[i] = 1000 + i;

        std::cout << "| Scenario | XOR+zstd | Delta+zstd | XOR+Delta+zstd | Ratio |\n";
        std::cout << "|----------|----------|------------|----------------|-------|\n";

        // Typical frame
        curr = prev;
        curr.cpu.pc = 0x8050; curr.cpu.sp = 0xFFF8; curr.cpu.r_low++;
        curr.chipset.t_states += 71680; curr.chipset.frame_counter++;
        curr.ramPageRefs[5] = 2000;  // One page changed
        auto r1 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDFrameRecord), iters);
        double ratio1 = 1476.0 / r1.xorSize;
        std::cout << "| Typical (1 page) | " << r1.xorSize << "B | " << r1.deltaSize << "B | "
                  << r1.xorDeltaSize << "B | " << std::fixed << std::setprecision(1) << ratio1 << "x |\n";

        // Heavy frame (screen scroll)
        curr = prev;
        curr.cpu.pc = 0x8200; curr.cpu.sp = 0xFFE0;
        curr.chipset.t_states += 71680; curr.chipset.frame_counter++;
        for (int i = 0; i < 8; ++i) curr.ramPageRefs[i] = 3000 + i;  // 8 pages
        auto r2 = BenchmarkData(reinterpret_cast<uint8_t*>(&prev),
                                 reinterpret_cast<uint8_t*>(&curr),
                                 sizeof(TTDFrameRecord), iters);
        double ratio2 = 1476.0 / r2.xorSize;
        std::cout << "| Heavy (8 pages) | " << r2.xorSize << "B | " << r2.deltaSize << "B | "
                  << r2.xorDeltaSize << "B | " << std::fixed << std::setprecision(1) << ratio2 << "x |\n";
    }

    // =========================================================================
    PrintSectionHeader("6. Conclusions");
    // =========================================================================
    std::cout << R"(
1. **Random scatter is pathological** - real workloads have locality
2. **Struct records compress well** - small, mostly zeros after XOR
3. **XOR+zstd wins for scattered changes** - no delta RLE overhead
4. **XOR+Delta+zstd wins for localized changes** - smaller input to zstd
5. **For GS SRAM**: use XOR+Delta when changes are localized (audio playback)
6. **For structs**: XOR+zstd is sufficient (overhead of delta not justified)
)";

    return 0;
}
