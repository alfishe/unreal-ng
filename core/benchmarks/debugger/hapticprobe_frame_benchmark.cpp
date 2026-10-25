#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/controllers/ihapticfeed.h"
#include "emulator/mainloop.h"

/// What the haptic feed's breakpoint probes cost a whole frame (haptics.md §12): a 48K idling in BASIC, one frame per
/// iteration through MainLoop::RunFramePublic(), with a feed attached that arms N `write` probes on the system
/// variables from FRAMES (#5C78) up, which the ROM's frame interrupt writes every frame (so probe 0 hits each frame).
///   /0   the feed with no probe: nothing armed, the fast memory interface (the cost of the feed itself)
///   /1   one write probe: the debug memory path held on, one filter bit, one hit a frame
///   /16  sixteen write probes, a few of them hit
/// A probe never stops the CPU, so the frames run as without it.
namespace
{
class ProbeFeed : public controllers::IHapticFeed
{
public:
    explicit ProbeFeed(int count)
    {
        for (int i = 0; i < count; i++)
        {
            controllers::HapticProbe probe;
            probe.kind = controllers::HapticProbeKind::Write;
            probe.address = probe.end = static_cast<uint16_t>(0x5C78 + i);
            _probes.push_back(probe);
        }
    }
    void OnHapticFrame(const controllers::HapticFrame& frame) override
    {
        hits += frame.probeCount > 0 ? frame.probeHits[0] : 0;
    }
    std::vector<controllers::HapticProbe> Probes() const override
    {
        return _probes;
    }
    uint64_t hits = 0;

private:
    std::vector<controllers::HapticProbe> _probes;
};

void RunProbeFrame(benchmark::State& state, int count)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-haptic-probes", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    if (!mainLoop || context->pControllerManager == nullptr)
    {
        state.SkipWithError("main loop or controller manager unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }
    for (int i = 0; i < 150; i++)
        mainLoop->RunFramePublic();

    auto feed = std::make_shared<ProbeFeed>(count);
    context->pControllerManager->SetHapticFeed(feed);
    for (int i = 0; i < 5; i++)
        mainLoop->RunFramePublic();  // the first frame attaches the feed and arms its probes

    for (auto _ : state)
        mainLoop->RunFramePublic();

    state.SetItemsProcessed(state.iterations());
    state.counters["probes"] = count;
    state.counters["hits_per_frame"] =
        benchmark::Counter(static_cast<double>(feed->hits) / static_cast<double>(state.iterations() + 5));
    context->pControllerManager->SetHapticFeed(nullptr);
    mainLoop->RunFramePublic();
    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_HapticProbeFrame(benchmark::State& state)
{
    RunProbeFrame(state, static_cast<int>(state.range(0)));
}

BENCHMARK(BM_HapticProbeFrame)->Arg(0)->Arg(1)->Arg(16)->Iterations(600)->Unit(benchmark::kMicrosecond);
