#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "loaders/benchmark_path_helper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/zxpoly/zxpolygroup.h"

/// ZX-Poly group frame cost: Summer Santa 2022 (.zxp, locked, gameplay idle)
/// on 4 x Pentagon at unlimited speed (turbo on every member: rendering
/// decimated, audio muted), 10 frames per iteration, time per frame.
///
/// The variants split the cost of one group frame:
///   Group/0 sequential  master frame, then the slaves one after another
///   Group/1 parallel    master frame, then the three slaves at once
///   Group/2 pipelined   the slaves' frame overlaps the master's next one
///                       (the default at unlimited speed)
///   MasterOnly/1        the master frame alone, with the group's per-instruction
///                       hook (line capture) - the floor a pipelined schedule nears
///   MasterOnly/0        the same without the hook: a stock machine frame
/// The MasterOnly variants let the master run ahead of the slaves (lockstep is
/// not maintained there); only the time is of interest.
namespace
{
std::unique_ptr<ZXPolyGroup> CreateSummerSanta(benchmark::State& state)
{
    auto group = std::make_unique<ZXPolyGroup>("bench-zxpoly");
    std::string error;
    if (!group->Create("PENTAGON", &error) ||
        !group->LoadZXP(BenchmarkPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
    {
        state.SkipWithError(error.c_str());
        return nullptr;
    }

    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        group->GetInstance(m)->EnableTurboMode();

    // Warm-up: past the first frames' one-off work
    group->RunFrames(50);
    return group;
}

void RunMasterFrame(ZXPolyGroup& group)
{
    EmulatorContext* master = group.GetContext(0);
    const uint64_t startFrame = master->emulatorState.frame_counter;
    group.GetInstance(0)->RunUntilCondition(
        [master, startFrame](const Z80State&) { return master->emulatorState.frame_counter != startFrame; }, 0,
        false);
}
}    // namespace

constexpr unsigned FRAMES_PER_ITERATION = 10;

static void BM_ZXPolyGroupFrame(benchmark::State& state)
{
    std::unique_ptr<ZXPolyGroup> group = CreateSummerSanta(state);
    if (!group)
        return;
    const int64_t mode = state.range(0);
    group->SetParallelSlaves(mode >= 1);
    group->SetPipelinedSlaves(mode >= 2);

    for (auto _ : state)
        group->RunFrames(FRAMES_PER_ITERATION);

    static const char* const labels[] = {"sequential slaves", "parallel slaves", "pipelined slaves"};
    state.SetLabel(labels[mode]);
    state.SetItemsProcessed(state.iterations() * FRAMES_PER_ITERATION);
}

static void BM_ZXPolyMasterOnlyFrame(benchmark::State& state)
{
    std::unique_ptr<ZXPolyGroup> group = CreateSummerSanta(state);
    if (!group)
        return;
    const bool hooked = state.range(0) != 0;
    Z80* master = group->GetContext(0)->pCore->GetZ80();
    if (!hooked)
        master->m1TraceHook = nullptr;

    for (auto _ : state)
    {
        for (unsigned f = 0; f < FRAMES_PER_ITERATION; f++)
            RunMasterFrame(*group);
    }

    state.SetLabel(hooked ? "master, group hook" : "master, no hook");
    state.SetItemsProcessed(state.iterations() * FRAMES_PER_ITERATION);
}

BENCHMARK(BM_ZXPolyGroupFrame)->Arg(0)->Arg(1)->Arg(2)->Iterations(100)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ZXPolyMasterOnlyFrame)->Arg(1)->Arg(0)->Iterations(100)->Unit(benchmark::kMillisecond);

/// The host speed control (x2..x16) at normal pacing: one host frame holds N
/// machine frames and is rendered, so the group runs it synchronously (parallel
/// slaves). The time per host frame against the 20.48 ms budget shows up to
/// which multiplier a ZX-Poly machine keeps its speed
static void BM_ZXPolySpeedMultiplierFrame(benchmark::State& state)
{
    auto group = std::make_unique<ZXPolyGroup>("bench-zxpoly");
    std::string error;
    if (!group->Create("PENTAGON", &error) ||
        !group->LoadZXP(BenchmarkPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
    {
        state.SkipWithError(error.c_str());
        return;
    }
    group->RunFrames(20);
    group->GetContext(0)->pCore->SetSpeedMultiplier(static_cast<uint8_t>(state.range(0)));
    group->RunFrames(3);    // the multiplier applies at the next frame start

    for (auto _ : state)
        group->RunFrames(1);

    state.SetLabel("x" + std::to_string(state.range(0)));
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ZXPolySpeedMultiplierFrame)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Iterations(100)
    ->Unit(benchmark::kMillisecond);
