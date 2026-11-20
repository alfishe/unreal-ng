#include <benchmark/benchmark.h>

#include <filesystem>
#include <string>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/mainloop.h"
#include "emulator/slots/cards/plusd/plusdautostart.h"

/// The MGT +D's cost per frame (docs/inprogress/2026-11-15-mgt-plus-d/test-plan.md §7): a 48K without the card, a
/// 48K-PLUSD idle in BASIC (the card traps KEY-SCAN at #028E every interrupt, so it pages in and out each frame),
/// and a 48K-PLUSD while G+DOS reads the system disk (the autostart's `LOAD d1"CONFIG"`: the WD1772 busy, the motor
/// on). Frames through MainLoop::RunFramePublic() at 1x (no turbo), after 300 frames of boot.
///
/// Compare 48K with 48K-PLUSD idle in one build (acceptance: within the A/A noise); the 48K row across the commit
/// before the card and after it (acceptance: no measurable change without the card).

namespace
{

/// The system disk, copied next to the binary (the autostart may write to the disk it mounts)
std::string SystemDiskCopy()
{
    const std::filesystem::path source =
        std::filesystem::path(__FILE__).parent_path() / "../../../data/boot/plusd/gdos-2a-system.mgt";
    const std::filesystem::path copy = std::filesystem::current_path() / "plusd-benchmark.mgt";
    std::error_code error;
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, error);
    return error ? std::string() : copy.string();
}

enum class Scenario
{
    Idle,
    Read,
};

void RunPlusDFrames(benchmark::State& state, const char* model, Scenario scenario)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-plusd", model, LoggerLevel::LogNone);
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

    for (int i = 0; i < 300; i++)
        mainLoop->RunFramePublic();

    if (scenario == Scenario::Read)
    {
        const std::string disk = SystemDiskCopy();
        const Emulator::DiskAutostartResult started = disk.empty() ? Emulator::DiskAutostartResult{}
                                                                   : emulator->AutostartDisk(disk, 0);
        if (!started.started)
        {
            state.SkipWithError(("autostart failed: " + started.message).c_str());
            manager->RemoveEmulator(emulator->GetUUID());
            return;
        }
        // Up to the start command: the reset, RUN and +SYS (the DOS); then CONFIG loads while the rows are timed
        for (int i = 0; i < 2000 && context->pDiskAutostart->PlusD().GetStatus().active; i++)
            mainLoop->RunFramePublic();
        if (!context->pDiskAutostart->PlusD().GetStatus().ok)
        {
            state.SkipWithError("the autostart did not reach its start command");
            manager->RemoveEmulator(emulator->GetUUID());
            return;
        }
    }

    for (auto _ : state)
        mainLoop->RunFramePublic();

    state.SetItemsProcessed(state.iterations());
    manager->RemoveEmulator(emulator->GetUUID());
}

}  // namespace

static void BM_PlusD_48K_Idle(benchmark::State& state)
{
    RunPlusDFrames(state, "48K", Scenario::Idle);
}

static void BM_PlusD_48KPLUSD_Idle(benchmark::State& state)
{
    RunPlusDFrames(state, "48K-PLUSD", Scenario::Idle);
}

static void BM_PlusD_48KPLUSD_Read(benchmark::State& state)
{
    RunPlusDFrames(state, "48K-PLUSD", Scenario::Read);
}

BENCHMARK(BM_PlusD_48K_Idle)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_PlusD_48KPLUSD_Idle)->Iterations(1000)->Unit(benchmark::kMicrosecond);
// CONFIG loads in a few hundred frames: the rows stay inside the load
BENCHMARK(BM_PlusD_48KPLUSD_Read)->Iterations(150)->Unit(benchmark::kMicrosecond);
