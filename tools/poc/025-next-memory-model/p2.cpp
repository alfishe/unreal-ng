// PoC 024 / P2: the Next's memory model and the CPU libraries' per-step cost.
//
// Workload: the 48K ROM idling in its BASIC editor loop (the same machine as BM_HostFrame_48K_*), one 69888 T frame per
// iteration with a maskable interrupt at its start. The memory is always the same bytes (ROM 0-3FFF read-only, RAM above);
// what changes is how the CPU library reaches it:
//   callback bus, four 16K windows   - the shape of Memory::MemoryReadFast today (one table lookup in the host's callback)
//   callback bus, eight 8K slots     - the Next's slot table (decision D6)
//   paged bus (unreal-z80 only)      - the library holds the table and reads direct pages inline; the page size is a build
//                                      option, so there is one executable per size
//   flat bus (unreal-z80 only)       - a 64K array, no ROM protection: the upper bound
// and which library runs it: unreal-z80 (the General Sound's CPU), z84c15 (the Sprinter's), unreal-next-z80 (callback only).
//
// Rebuild benchmarks time what a mapping change costs the host: the 128K machine's 7FFD write against the Next's MMU.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "z80cpu.h"
#include "z80ncpu.h"
#include "z84cpu.h"

#define POC_STR2(x) #x
#define POC_STR(x) POC_STR2(x)
#define POC_SHIFT_STR POC_STR(POC_PAGE_SHIFT)

namespace
{
constexpr int kFrameT = 69888;

struct Mem
{
    alignas(64) uint8_t rom[16384];
    alignas(64) uint8_t ram[49152];
    alignas(64) uint8_t sink[16384];     // a write to a read-only slot lands here
    alignas(64) uint8_t flat[65536];
    uint8_t* rd[8];
    uint8_t* wr[8];

    Mem()
    {
        std::memset(rom, 0xFF, sizeof rom);
        std::memset(ram, 0, sizeof ram);
        FILE* f = std::fopen("data/rom/48.rom", "rb");
        if (!f)
            f = std::fopen("../../data/rom/48.rom", "rb");
        if (f)
        {
            if (std::fread(rom, 1, sizeof rom, f) != sizeof rom)
                std::fprintf(stderr, "short ROM read\n");
            std::fclose(f);
        }
        else
            std::fprintf(stderr, "data/rom/48.rom not found: run from the repository root\n");
        std::memcpy(flat, rom, sizeof rom);
    }

    /// The same 48K machine seen through 1 << (16 - shift) windows
    void Map(int shift)
    {
        const int n = 1 << (16 - shift), size = 1 << shift;
        for (int i = 0; i < n; ++i)
        {
            const int a = i * size;
            rd[i] = a < 0x4000 ? rom + a : ram + (a - 0x4000);
            wr[i] = a < 0x4000 ? sink + a : ram + (a - 0x4000);
        }
    }
};

template <int S>
inline uint8_t ReadAt(Mem* m, uint16_t a) { return m->rd[a >> S][a & ((1 << S) - 1)]; }
template <int S>
inline void WriteAt(Mem* m, uint16_t a, uint8_t v) { m->wr[a >> S][a & ((1 << S) - 1)] = v; }

// ---- the three libraries behind one shape -----------------------------------------------------------------------------
struct EngineZ80
{
    using Cpu = Z80CPU;
    static Cpu* Create() { return Z80CpuCreate(); }
    static void Destroy(Cpu* c) { Z80CpuDestroy(c); }
    static void Reset(Cpu* c) { Z80CpuReset(c); }
    static int Step(Cpu* c) { return Z80CpuStep(c); }
    static void Int(Cpu* c) { Z80CpuInt(c); }
    template <int S>
    static uint8_t Rd(Cpu*, uint16_t a, int, void* u) { return ReadAt<S>(static_cast<Mem*>(u), a); }
    template <int S>
    static void Wr(Cpu*, uint16_t a, uint8_t v, void* u) { WriteAt<S>(static_cast<Mem*>(u), a, v); }
    static uint8_t In(Cpu*, uint16_t, void*) { return 0xFF; }
    static void Out(Cpu*, uint16_t, uint8_t, void*) {}
    template <int S>
    static void Bus(Cpu* c, Mem* m)
    {
        Z80CpuSetMemoryBus(c, &Rd<S>, m, &Wr<S>, m);
        Z80CpuSetPortBus(c, &In, m, &Out, m);
    }
};

struct EngineZ84
{
    using Cpu = Z84CPU;
    static Cpu* Create() { return Z84CpuCreate(); }
    static void Destroy(Cpu* c) { Z84CpuDestroy(c); }
    static void Reset(Cpu* c) { Z84CpuReset(c); }
    static int Step(Cpu* c) { return Z84CpuStep(c); }
    static void Int(Cpu* c) { Z84CpuInt(c); }
    template <int S>
    static uint8_t Rd(Cpu*, uint16_t a, Z84CpuAccessKind, void* u) { return ReadAt<S>(static_cast<Mem*>(u), a); }
    template <int S>
    static void Wr(Cpu*, uint16_t a, uint8_t v, void* u) { WriteAt<S>(static_cast<Mem*>(u), a, v); }
    static uint8_t In(Cpu*, uint16_t, void*) { return 0xFF; }
    static void Out(Cpu*, uint16_t, uint8_t, void*) {}
    template <int S>
    static void Bus(Cpu* c, Mem* m)
    {
        Z84CpuSetMemoryBus(c, &Rd<S>, m, &Wr<S>, m);
        Z84CpuSetPortBus(c, &In, m, &Out, m);
    }
};

struct EngineNext
{
    using Cpu = Z80nCPU;
    static Cpu* Create() { return Z80nCpuCreate(); }
    static void Destroy(Cpu* c) { Z80nCpuDestroy(c); }
    static void Reset(Cpu* c) { Z80nCpuReset(c); }
    static int Step(Cpu* c) { return Z80nCpuStep(c); }
    static void Int(Cpu* c) { Z80nCpuInt(c); }
    template <int S>
    static uint8_t Rd(Cpu*, uint16_t a, Z80nCpuAccessKind, void* u) { return ReadAt<S>(static_cast<Mem*>(u), a); }
    template <int S>
    static void Wr(Cpu*, uint16_t a, uint8_t v, void* u) { WriteAt<S>(static_cast<Mem*>(u), a, v); }
    static uint8_t In(Cpu*, uint16_t, void*) { return 0xFF; }
    static void Out(Cpu*, uint16_t, uint8_t, void*) {}
    template <int S>
    static void Bus(Cpu* c, Mem* m)
    {
        Z80nCpuSetMemoryBus(c, &Rd<S>, m, &Wr<S>, m);
        Z80nCpuSetPortBus(c, &In, m, &Out, m);
    }
};

/// One frame: instructions until 69888 T are used, then the interrupt. Returns the instructions run
template <class E>
inline int Frame(typename E::Cpu* c)
{
    int t = 0, n = 0;
    while (t < kFrameT)
    {
        t += E::Step(c);
        ++n;
    }
    E::Int(c);
    return n;
}

template <class E, int S>
void BM_CallbackFrame(benchmark::State& state)
{
    static Mem mem;
    mem.Map(S);
    typename E::Cpu* c = E::Create();
    E::Reset(c);
    E::template Bus<S>(c, &mem);
    for (int i = 0; i < 150; ++i)
        Frame<E>(c);   // boot to the idle loop
    int n = 0;
    for (auto _ : state)
        n = Frame<E>(c);
    state.counters["instr/frame"] = n;
    E::Destroy(c);
}

void BM_Z80Paged(benchmark::State& state)
{
    static Mem mem;
    mem.Map(POC_PAGE_SHIFT);
    Z80CPU* c = Z80CpuCreate();
    Z80CpuReset(c);
    Z80CpuSetPortBus(c, &EngineZ80::In, &mem, &EngineZ80::Out, &mem);
    Z80CpuSetMemoryBus(c, &EngineZ80::Rd<POC_PAGE_SHIFT>, &mem, &EngineZ80::Wr<POC_PAGE_SHIFT>, &mem);
    Z80CpuAttachPageTables(c, mem.rd, mem.wr);
    Z80CpuSyncPageTables(c);
    for (int i = 0; i < 150; ++i)
        Frame<EngineZ80>(c);
    int n = 0;
    for (auto _ : state)
        n = Frame<EngineZ80>(c);
    state.counters["instr/frame"] = n;
    Z80CpuDestroy(c);
}

void BM_Z80Flat(benchmark::State& state)
{
    static Mem mem;
    Z80CPU* c = Z80CpuCreate();
    Z80CpuReset(c);
    Z80CpuSetPortBus(c, &EngineZ80::In, &mem, &EngineZ80::Out, &mem);
    Z80CpuAttachMemory(c, mem.flat);
    for (int i = 0; i < 150; ++i)
        Frame<EngineZ80>(c);
    int n = 0;
    for (auto _ : state)
        n = Frame<EngineZ80>(c);
    state.counters["instr/frame"] = n;
    Z80CpuDestroy(c);
}

// ---- what a mapping change costs the host -----------------------------------------------------------------------------
// The 128K machine: port 7FFD picks the ROM and the RAM bank of the top window (four windows of 16K, read and write pointers)
void BM_Rebuild128K(benchmark::State& state)
{
    static std::vector<uint8_t> ram(8 * 16384), rom(2 * 16384), sink(16384);
    static uint8_t* rd[4];
    static uint8_t* wr[4];
    uint8_t port = 0;
    for (auto _ : state)
    {
        port = static_cast<uint8_t>(port + 7);
        rd[0] = rom.data() + ((port >> 4) & 1) * 16384;
        wr[0] = sink.data();
        rd[1] = wr[1] = ram.data() + 5 * 16384;
        rd[2] = wr[2] = ram.data() + 2 * 16384;
        rd[3] = wr[3] = ram.data() + (port & 7) * 16384;
        benchmark::DoNotOptimize(rd);
        benchmark::DoNotOptimize(wr);
        benchmark::ClobberMemory();
    }
}

// The Next: eight MMU registers (NextREG #50-#57, 8K page numbers, #FF = the ROM), the ROM mapped at slots 0-1 when the
// registers hold #FF, write protection of the ROM slots, a 2 MB RAM array
void BM_RebuildNextMmu(benchmark::State& state)
{
    static std::vector<uint8_t> ram(2 * 1024 * 1024), rom(16384), sink(16384);
    static uint8_t* rd[8];
    static uint8_t* wr[8];
    uint8_t mmu[8] = {0xFF, 0xFF, 10, 11, 4, 5, 0, 1};
    uint8_t step = 0;
    for (auto _ : state)
    {
        step = static_cast<uint8_t>(step + 3);
        mmu[2 + (step & 3)] = static_cast<uint8_t>(step & 0x7F);
        for (int i = 0; i < 8; ++i)
        {
            if (mmu[i] == 0xFF)
            {
                rd[i] = rom.data() + (i & 1) * 8192;
                wr[i] = sink.data() + (i & 1) * 8192;
            }
            else
                rd[i] = wr[i] = ram.data() + static_cast<size_t>(mmu[i]) * 8192;
        }
        benchmark::DoNotOptimize(rd);
        benchmark::DoNotOptimize(wr);
        benchmark::ClobberMemory();
    }
}

// The same MMU change when the library keeps its own copy of the table (unreal-z80's paged bus): the host also calls Sync
void BM_RebuildNextMmuPagedSync(benchmark::State& state)
{
    static std::vector<uint8_t> ram(2 * 1024 * 1024);
    static uint8_t* rd[Z80CPU_PAGE_COUNT];
    static uint8_t* wr[Z80CPU_PAGE_COUNT];
    Z80CPU* c = Z80CpuCreate();
    Z80CpuReset(c);
    uint8_t step = 0;
    for (int i = 0; i < Z80CPU_PAGE_COUNT; ++i)
        rd[i] = wr[i] = ram.data() + static_cast<size_t>(i) * Z80CPU_PAGE_SIZE;
    Z80CpuAttachPageTables(c, rd, wr);
    Z80CpuSyncPageTables(c);
    for (auto _ : state)
    {
        step = static_cast<uint8_t>(step + 3);
        for (int i = 0; i < Z80CPU_PAGE_COUNT; ++i)
        {
            const uint8_t page = static_cast<uint8_t>((i + step) & 0x7F);
            rd[i] = wr[i] = ram.data() + static_cast<size_t>(page) * Z80CPU_PAGE_SIZE;
        }
        Z80CpuSyncPageTables(c);
        benchmark::ClobberMemory();
    }
    Z80CpuDestroy(c);
}
}  // namespace

BENCHMARK_TEMPLATE2(BM_CallbackFrame, EngineZ80, 14)->Name("Frame/unreal-z80/callback-4x16K");
BENCHMARK_TEMPLATE2(BM_CallbackFrame, EngineZ80, 13)->Name("Frame/unreal-z80/callback-8x8K");
BENCHMARK_TEMPLATE2(BM_CallbackFrame, EngineZ84, 14)->Name("Frame/z84c15/callback-4x16K");
BENCHMARK_TEMPLATE2(BM_CallbackFrame, EngineZ84, 13)->Name("Frame/z84c15/callback-8x8K");
BENCHMARK_TEMPLATE2(BM_CallbackFrame, EngineNext, 14)->Name("Frame/unreal-next-z80/callback-4x16K");
BENCHMARK_TEMPLATE2(BM_CallbackFrame, EngineNext, 13)->Name("Frame/unreal-next-z80/callback-8x8K");
BENCHMARK(BM_Z80Paged)->Name("Frame/unreal-z80/paged-shift" POC_SHIFT_STR);
BENCHMARK(BM_Z80Flat)->Name("Frame/unreal-z80/flat");
BENCHMARK(BM_Rebuild128K)->Name("Rebuild/128K-7FFD-4-windows");
BENCHMARK(BM_RebuildNextMmu)->Name("Rebuild/Next-MMU-8-slots");
BENCHMARK(BM_RebuildNextMmuPagedSync)->Name("Rebuild/paged-table-plus-Sync-shift" POC_SHIFT_STR);

BENCHMARK_MAIN();
