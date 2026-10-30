#include <benchmark/benchmark.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

#if __has_include("debugger/breakpoints/breakpointcondition.h")
#define BENCH_HAS_CONDITIONS 1
#endif

/// The resolve of one write that a breakpoint covers (breakpoint-taps conditions-and-callbacks.md, phase 1): the cost
/// of a match, without a stop, for an A/B of the counting change, and on a branch with conditions the cost of the
/// condition itself:
///   Miss         a write breakpoint armed, the write elsewhere: the filter only
///   Hit          a write breakpoint whose hit policy never stops (the 4-billionth hit): the walk, the count, the policy
///   CondFast/*   the same with a fast-shape condition (VAL == #42): true / false
///   CondVm/*     a condition the stack machine runs (VAL & 0F == 2 && M(5C3A) != WR)
/// Only the API master has, outside the conditions, so the same file measures master and a branch.
namespace
{
struct Bench
{
    EmulatorContext* context = nullptr;
    Memory* memory = nullptr;
    BreakpointManager* bpm = nullptr;

    Bench()
    {
        MessageCenter::DisposeDefaultMessageCenter();
        context = new EmulatorContext(LoggerLevel::LogNone);
        context->config.ramsize = 128;
        memory = new Memory(context);
        context->pMemory = memory;
        memory->Reset();
        bpm = new BreakpointManager(context);
    }
    ~Bench()
    {
        delete bpm;
        delete memory;
        context->pMemory = nullptr;
        delete context;
        MessageCenter::DisposeDefaultMessageCenter();
    }
    void AddNeverStopping(const std::string& condition)
    {
        BreakpointSpec spec;
        spec.type = BRK_MEMORY;
        spec.access = BRK_MEM_WRITE;
        spec.address = 0x9000;
        spec.hitMode = BRK_HIT_EQUAL;
        spec.hitTarget = 0xFFFF'FFFFu;
#ifdef BENCH_HAS_CONDITIONS
        spec.condition = condition;
#else
        (void)condition;
#endif
        std::string error;
        if (bpm->AddBreakpoint(spec, error) == BRK_INVALID)
            throw std::runtime_error(error);
    }
};

void RunWrites(benchmark::State& state, const std::string& condition, uint16_t address, int value)
{
    Bench bench;
    bench.AddNeverStopping(condition);
    uint16_t sink = 0;
    for (auto _ : state)
    {
        for (int i = 0; i < 1000; i++)
#ifdef BENCH_HAS_CONDITIONS
            sink ^= bench.bpm->HandleMemoryWrite(address, value);
#else
            sink ^= bench.bpm->HandleMemoryWrite(address);
#endif
        benchmark::DoNotOptimize(sink);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
}  // namespace

static void BM_BreakpointResolve_Miss(benchmark::State& state) { RunWrites(state, "", 0x9001, 0x42); }
static void BM_BreakpointResolve_Hit(benchmark::State& state) { RunWrites(state, "", 0x9000, 0x42); }
BENCHMARK(BM_BreakpointResolve_Miss)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BreakpointResolve_Hit)->Unit(benchmark::kMicrosecond);

#ifdef BENCH_HAS_CONDITIONS
static void BM_BreakpointResolve_CondFast_True(benchmark::State& state) { RunWrites(state, "VAL == 42", 0x9000, 0x42); }
static void BM_BreakpointResolve_CondFast_False(benchmark::State& state) { RunWrites(state, "VAL == 42", 0x9000, 0x41); }
static void BM_BreakpointResolve_CondVm_True(benchmark::State& state)
{
    RunWrites(state, "(VAL & 0F) == 2 && M(5C3A) != WR", 0x9000, 0x42);
}
static void BM_BreakpointResolve_CondVm_False(benchmark::State& state)
{
    RunWrites(state, "(VAL & 0F) == 2 && M(5C3A) != WR", 0x9000, 0x41);
}
BENCHMARK(BM_BreakpointResolve_CondFast_True)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BreakpointResolve_CondFast_False)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BreakpointResolve_CondVm_True)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BreakpointResolve_CondVm_False)->Unit(benchmark::kMicrosecond);
#endif
