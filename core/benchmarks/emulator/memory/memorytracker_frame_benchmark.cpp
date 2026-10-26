#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "loaders/benchmark_path_helper.h"

/// Host cost of one turbo frame of a real game per MemoryAccessTracker level: the P0 baseline of the analysis
/// layers (docs/inprogress/2026-10-24-analysis-layers/design.md, "L0 Access facts", budget table). Each run loads
/// the game snapshot into a fresh machine, runs the warm-up frames with the tracker off, switches the level and
/// then measures the same emulated frames for every level: the workload does not depend on the level.
///
///   BM_MemoryTrackerFrame/machine:<0..2>/level:<0..3>
///
/// Levels (the argument is the enum value; a new level, such as L0's `facts`, is one more enum value and one more
/// Args row per machine):
///   0 off              debug mode off: the fast memory interface, what a user runs
///   1 debug            debug mode on, memory tracking off: the debug memory interface alone (counters need it)
///   2 counters-z80     tracking on, TrackingMode::Z80AddressSpace: Z80 64K counters plus the physical ones
///   3 counters-phys    tracking on, TrackingMode::PhysicalMemory: the physical counters only
///
/// Workloads (testdata/, all tracked in git):
///   0 48K       loaders/rzx/oracle/garfield-300.z80     Garfield, SkoolKit state after 300 recorded frames
///   1 128K      loaders/rzx/oracle/greenberet-300.z80   Green Beret (128K), the same
///   2 PENTAGON  loaders/z80/dizzyx.z80                  Dizzy X, a Pentagon .z80 (hardware mode 9)
///
/// Compare levels with interleaved repetitions, not one run (the machine is often loaded):
///   core-benchmarks --benchmark_filter=BM_MemoryTrackerFrame --benchmark_repetitions=20
///                   --benchmark_enable_random_interleaving=true --benchmark_format=json
/// Results and method: docs/inprogress/2026-10-24-analysis-layers/baselines/memorytracker-p0.md
namespace
{
enum class TrackerLevel : int
{
    Off = 0,
    Debug = 1,
    CountersZ80 = 2,
    CountersPhysical = 3,
};

struct Workload
{
    const char* model;
    const char* snapshot;  ///< relative to testdata/
    const char* game;
};

constexpr Workload kWorkloads[] = {
    {"48K", "loaders/rzx/oracle/garfield-300.z80", "garfield"},
    {"128K", "loaders/rzx/oracle/greenberet-300.z80", "greenberet"},
    {"PENTAGON", "loaders/z80/dizzyx.z80", "dizzyx"},
};
constexpr int kWorkloadCount = static_cast<int>(sizeof(kWorkloads) / sizeof(kWorkloads[0]));

constexpr int kWarmupFrames = 50;

const char* LevelName(TrackerLevel level)
{
    switch (level)
    {
        case TrackerLevel::Off:
            return "off";
        case TrackerLevel::Debug:
            return "debug";
        case TrackerLevel::CountersZ80:
            return "counters-z80";
        case TrackerLevel::CountersPhysical:
            return "counters-phys";
    }
    return "?";
}

/// The features of a level, through FeatureManager as the REST / MCP start routes do (setFeature refreshes the
/// memory interface and the Memory / Z80 / tracker caches). The other debug features stay off, whatever the
/// unreal.ini next to the binary says, so that only the tracker differs between levels
void ApplyLevel(EmulatorContext* context, TrackerLevel level)
{
    FeatureManager* features = context->pFeatureManager;
    const bool debug = level != TrackerLevel::Off;
    const bool tracking = level == TrackerLevel::CountersZ80 || level == TrackerLevel::CountersPhysical;
    features->setFeature(Features::kBreakpoints, false);
    features->setFeature(Features::kCallTrace, false);
    features->setFeature(Features::kOpcodeProfiler, false);
    features->setFeature(Features::kMemoryTracking, tracking);
    features->setFeature(Features::kDebugMode, debug);

    if (!tracking)
        return;
    MemoryAccessTracker& tracker = context->pMemory->GetAccessTracker();
    tracker.SetTrackingMode(level == TrackerLevel::CountersPhysical ? TrackingMode::PhysicalMemory
                                                                    : TrackingMode::Z80AddressSpace);
    tracker.StartMemorySession();
}

/// Sums of the tracker's counters: proof that the level counted, and the accesses per frame it costs
struct CounterSums
{
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t executes = 0;
    uint64_t ramExecutes = 0;  ///< Z80 mode: executes at #4000-#FFFF (the game, not the ROM)
};

CounterSums SumCounters(const MemoryAccessTracker& tracker, bool physical)
{
    CounterSums sums;
    const MemoryAccessTracker::CountersView view = physical ? tracker.PhysCountersView() : tracker.Z80CountersView();
    if (!view.reads || !view.writes || !view.executes)
        return sums;
    for (size_t i = 0; i < view.size; i++)
    {
        sums.reads += view.reads[i];
        sums.writes += view.writes[i];
        sums.executes += view.executes[i];
        if (!physical && i >= 0x4000)
            sums.ramExecutes += view.executes[i];
    }
    return sums;
}
}  // namespace

static void BM_MemoryTrackerFrame(benchmark::State& state)
{
    const int workloadIndex = static_cast<int>(state.range(0));
    const auto level = static_cast<TrackerLevel>(state.range(1));
    if (workloadIndex < 0 || workloadIndex >= kWorkloadCount)
    {
        state.SkipWithError("no such workload");
        return;
    }
    const Workload& workload = kWorkloads[workloadIndex];
    const std::string snapshot = BenchmarkPathHelper::RequireTestDataFile(workload.snapshot);

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator =
        manager->CreateEmulatorWithModel("bench-memorytracker-frame", workload.model, LoggerLevel::LogNone);
    if (!emulator)
        BenchmarkPathHelper::FailSetup(std::string("cannot create ") + workload.model);
    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);

    // A failed load would measure the ROM idle loop and look like a valid result: fail the run instead
    ApplyLevel(context, TrackerLevel::Off);
    if (!emulator->LoadSnapshot(snapshot))
        BenchmarkPathHelper::FailSetup("cannot load " + snapshot + " on " + workload.model);

    emulator->EnableTurboMode();
    for (int i = 0; i < kWarmupFrames; i++)
        mainLoop->RunFramePublic();

    ApplyLevel(context, level);

    for (auto _ : state)
        mainLoop->RunFramePublic();

    const bool tracking = level == TrackerLevel::CountersZ80 || level == TrackerLevel::CountersPhysical;
    if (tracking)
    {
        const CounterSums sums =
            SumCounters(context->pMemory->GetAccessTracker(), level == TrackerLevel::CountersPhysical);
        const double frames = static_cast<double>(state.iterations());
        state.counters["reads_per_frame"] = static_cast<double>(sums.reads) / frames;
        state.counters["writes_per_frame"] = static_cast<double>(sums.writes) / frames;
        state.counters["executes_per_frame"] = static_cast<double>(sums.executes) / frames;
        if (level == TrackerLevel::CountersZ80 && sums.executes > 0)
            state.counters["ram_exec_share"] =
                static_cast<double>(sums.ramExecutes) / static_cast<double>(sums.executes);
        if (sums.reads + sums.writes + sums.executes == 0)
            state.SkipWithError("the tracker counted nothing");
    }

    state.SetLabel(std::string(workload.model) + " " + workload.game + " " + LevelName(level));
    state.SetItemsProcessed(state.iterations());

    emulator->DisableTurboMode();
    ApplyLevel(context, TrackerLevel::Off);
    manager->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_MemoryTrackerFrame)
    ->ArgNames({"machine", "level"})
    ->ArgsProduct({{0, 1, 2}, {0, 1, 2, 3}})
    ->Iterations(500)
    ->Unit(benchmark::kMicrosecond);
