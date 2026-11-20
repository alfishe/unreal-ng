#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "debugger/analyzers/profile/profilecontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "loaders/benchmark_path_helper.h"

/// What the CPU time profile (docs/emulator/analysis/cpu-profile.md) costs a whole frame, one frame per iteration
/// through MainLoop::RunFramePublic(): off (no step subscriber) and on with the default settings (call stack, the
/// per-frame series) or the cells alone (stack=false).
/// BM_CpuProfileFrame_48K: a 48K idling in BASIC (the ROM's key scan, an interrupt a frame)
/// BM_CpuProfileFrame_Dizzy: a game, Dizzy X on a Pentagon (testdata/loaders/sna), standing in its first room
///   /0  the profile off
///   /1  on, defaults (stack, frames)
///   /2  on, stack=false (the cells only)
namespace
{
void RunProfileFrame(benchmark::State& state, const char* model, const char* snapshot)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-cpuprofile", model, LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    if (!mainLoop)
    {
        state.SkipWithError("main loop unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }
    context->pFeatureManager->setFeature(Features::kScreenHQ, false);
    context->pFeatureManager->setFeature(Features::kSoundHQ, false);
    context->pMemory->UpdateFeatureCache();
    for (int i = 0; i < 150; i++)
        mainLoop->RunFramePublic();
    if (snapshot != nullptr && !emulator->LoadSnapshot(BenchmarkPathHelper::RequireTestDataFile(snapshot)))
    {
        state.SkipWithError("snapshot load failed");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    const int mode = static_cast<int>(state.range(0));
    if (mode > 0)
    {
        profile::Options options;
        if (mode == 2)
            options["stack"] = "false";
        const profile::Reply reply = profile::Execute(*emulator, "start", options);
        if (!reply.Ok())
        {
            state.SkipWithError(("cpuprofile start failed: " + reply.message).c_str());
            manager->RemoveEmulator(emulator->GetUUID());
            return;
        }
    }
    for (int i = 0; i < 5; i++)
        mainLoop->RunFramePublic();

    for (auto _ : state)
        mainLoop->RunFramePublic();

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(std::string(model) + (mode == 0 ? " profile off" : mode == 1 ? " profile on" : " profile cells"));
    if (mode > 0)
        (void)profile::Execute(*emulator, "stop", {});
    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_CpuProfileFrame_48K(benchmark::State& state)
{
    RunProfileFrame(state, "48K", nullptr);
}

static void BM_CpuProfileFrame_Dizzy(benchmark::State& state)
{
    RunProfileFrame(state, "PENTAGON", "loaders/sna/Dizzy X.sna");
}

BENCHMARK(BM_CpuProfileFrame_48K)->Arg(0)->Arg(1)->Arg(2)->Iterations(600)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_CpuProfileFrame_Dizzy)->Arg(0)->Arg(1)->Arg(2)->Iterations(600)->Unit(benchmark::kMicrosecond);
