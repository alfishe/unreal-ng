#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/controllers/ihapticfeed.h"
#include "emulator/mainloop.h"
#include "loaders/benchmark_path_helper.h"

/// What the haptic feed's breakpoint probes cost a whole frame (haptics.md §12), one frame per iteration through
/// MainLoop::RunFramePublic(), with a feed attached that arms N probes. A probe never stops the CPU, so the frames run
/// as without it.
/// BM_HapticProbeFrame: a 48K idling in BASIC, N `write` probes on the system variables from FRAMES (#5C78) up, which
/// the ROM's frame interrupt writes every frame (so probe 0 hits each frame).
///   /0   the feed with no probe: nothing armed, the fast memory interface (the cost of the feed itself)
///   /1   one write probe: the debug memory path held on, one filter bit, one hit a frame
///   /16  sixteen write probes, a few of them hit
/// BM_HapticExecProbeFrame: the same 48K, N `exec` probes from #0038 (the frame interrupt's entry, one hit a frame) up.
/// BM_HapticProbeDizzy: a game, Dizzy X on a Pentagon (testdata/loaders/sna), standing in its first room
///   /-1  no feed at all (the baseline)
///   /0   the feed with no probe
///   /4   the four exec probes of builtin/game/dizzy-x (jump, landing, energy taken, life lost)
namespace
{
class ProbeFeed : public controllers::IHapticFeed
{
public:
    ProbeFeed(controllers::HapticProbeKind kind, const std::vector<uint16_t>& addresses)
    {
        for (uint16_t address : addresses)
        {
            controllers::HapticProbe probe;
            probe.kind = kind;
            probe.address = probe.end = address;
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

std::vector<uint16_t> Consecutive(uint16_t first, int count)
{
    std::vector<uint16_t> addresses;
    for (int i = 0; i < count; i++)
        addresses.push_back(static_cast<uint16_t>(first + i));
    return addresses;
}

/// feed: nullptr runs the frames with no feed attached
void RunProbeFrame(benchmark::State& state, const char* model, const char* snapshot,
                   const std::shared_ptr<ProbeFeed>& feed)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-haptic-probes", model, LoggerLevel::LogNone);
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
    if (snapshot != nullptr && !emulator->LoadSnapshot(BenchmarkPathHelper::RequireTestDataFile(snapshot)))
    {
        state.SkipWithError("snapshot load failed");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    if (feed)
        context->pControllerManager->SetHapticFeed(feed);
    for (int i = 0; i < 5; i++)
        mainLoop->RunFramePublic();  // the first frame attaches the feed and arms its probes

    for (auto _ : state)
        mainLoop->RunFramePublic();

    state.SetItemsProcessed(state.iterations());
    state.counters["probes"] = feed ? static_cast<double>(feed->Probes().size()) : -1.0;
    state.counters["hits_per_frame"] = benchmark::Counter(
        feed ? static_cast<double>(feed->hits) / static_cast<double>(state.iterations() + 5) : 0.0);
    if (feed)
        context->pControllerManager->SetHapticFeed(nullptr);
    mainLoop->RunFramePublic();
    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_HapticProbeFrame(benchmark::State& state)
{
    const int count = static_cast<int>(state.range(0));
    RunProbeFrame(state, "48K", nullptr,
                  std::make_shared<ProbeFeed>(controllers::HapticProbeKind::Write, Consecutive(0x5C78, count)));
}

static void BM_HapticExecProbeFrame(benchmark::State& state)
{
    const int count = static_cast<int>(state.range(0));
    RunProbeFrame(state, "48K", nullptr,
                  std::make_shared<ProbeFeed>(controllers::HapticProbeKind::Exec, Consecutive(0x0038, count)));
}

static void BM_HapticProbeDizzy(benchmark::State& state)
{
    const int count = static_cast<int>(state.range(0));
    std::shared_ptr<ProbeFeed> feed;
    if (count >= 0)
    {
        const std::vector<uint16_t> profile = { 0x795A, 0x7A97, 0x847A, 0x8B80 };
        feed = std::make_shared<ProbeFeed>(controllers::HapticProbeKind::Exec,
                                           std::vector<uint16_t>(profile.begin(), profile.begin() + count));
    }
    RunProbeFrame(state, "PENTAGON", "loaders/sna/Dizzy X.sna", feed);
}

BENCHMARK(BM_HapticProbeFrame)->Arg(0)->Arg(1)->Arg(16)->Iterations(600)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HapticExecProbeFrame)->Arg(1)->Arg(16)->Iterations(600)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HapticProbeDizzy)->Arg(-1)->Arg(0)->Arg(4)->Iterations(600)->Unit(benchmark::kMicrosecond);
