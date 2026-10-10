#include <benchmark/benchmark.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/storage/fat/fatinplace.h"
#include "emulator/mainloop.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/sound/soundmanager.h"

/// Host frame cost of the Sprinter under a real heavy load: a demo from the DSS system disk (the MAME pack's
/// sp_hdd_sys.img, raw, path in UNREAL_SPRINTER_HDD; not in the repo). The session copy of SYSTEM.BAT starts the
/// demo; DSS boots and the demo loads in turbo, then each iteration is one MainLoop frame at the real speed.
///
/// "Bare" runs without the sound cards (no GS, no MoonSound, one AY), as the core tests do; "Shipped" with the
/// sound cards the shipped config fits (NeoGS, AY): the difference is their cost. The fast paths (idle cycles in
/// one go, the kept INT answer, the fused bus, the lazy screen, the NeoGS idle sleep) are on, as in the GUI.
///
///   UNREAL_SPRINTER_HDD=.../sp_hdd_sys.img core-benchmarks --benchmark_filter=BM_SprinterDemo
///
/// Compare builds with an interleaved A/B run of two worktrees (docs/emulator/design/core/sprinter-cpu-and-peripherals.md §8)
namespace
{
enum class Sound
{
    Bare,
    Shipped
};

void RunSprinterDemo(benchmark::State& state, const std::string& demo, int loadFrames, Sound sound)
{
    const char* path = std::getenv("UNREAL_SPRINTER_HDD");
    if (!path || !FileHelper::FileExists(path))
    {
        state.SkipWithError("UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) not set");
        return;
    }

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModelAndRAM(
        "bench-sprinter-demo", "SPRINTER", 4096, LoggerLevel::LogNone, nullptr, [sound](CONFIG& config) {
            if (sound == Sound::Bare)
            {
                config.sound.gsTypeKind = GSTypeKind::NONE;
                config.sound.moonsound = 0;
                config.sound.turboSoundKind = TurboSoundKind::None;
            }
        });
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    if (!decoder)
    {
        state.SkipWithError("not a Sprinter port decoder");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }
    decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC: the same boot every run
    context->config.sprinter.fast_start = 1;

    // The disk in session mode (the image file is never written), SYSTEM.BAT rewritten to start the demo
    MediaSource source;
    source.path = path;
    InsertOptions options;
    options.immediate = true;
    options.access = AccessMode::Session;
    Medium* medium = nullptr;
    if (context->pMediaManager->Insert("ide0.master", source, options).Ok())
        medium = context->pMediaManager->GetMedium("ide0.master");
    if (!medium || !medium->Block())
    {
        state.SkipWithError("the disk cannot be inserted");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }
    FatInPlace disk(*medium->Block());
    std::vector<uint8_t> bat;
    if (!disk.Open() || !disk.Read("/SYSTEM.BAT", bat))
    {
        state.SkipWithError("SYSTEM.BAT cannot be read");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }
    std::string text = "@echo off\r\ncd \\demos\\" + demo + "\r\n" + demo + "\r\nrem ";
    if (text.size() + 2 < bat.size())
        text.append(bat.size() - text.size() - 2, ' ');
    text += "\r\n";
    if (text.size() > bat.size() || !disk.Overwrite("/SYSTEM.BAT", std::vector<uint8_t>(text.begin(), text.end())))
    {
        state.SkipWithError("SYSTEM.BAT cannot be rewritten");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // DSS boots and the demo loads (turbo), then the real speed
    emulator->Reset();
    emulator->EnableTurboMode();
    emulator->RunNFrames(static_cast<unsigned>(loadFrames), true);
    emulator->DisableTurboMode();

    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    for (auto _ : state)
        mainLoop->RunFramePublic();

    const bool gs = context->pSoundManager->getGeneralSound() != nullptr;
    state.SetLabel(demo + (sound == Sound::Bare ? " bare" : " shipped sound") + (gs ? " (NeoGS)" : ""));
    state.SetItemsProcessed(state.iterations());
    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_SprinterDemo_DontBlink_Bare(benchmark::State& s) { RunSprinterDemo(s, "dntblink", 1200, Sound::Bare); }
static void BM_SprinterDemo_DontBlink_Shipped(benchmark::State& s) { RunSprinterDemo(s, "dntblink", 1200, Sound::Shipped); }
static void BM_SprinterDemo_Rotozoom_Bare(benchmark::State& s) { RunSprinterDemo(s, "rotozoom", 700, Sound::Bare); }
static void BM_SprinterDemo_Rotozoom_Shipped(benchmark::State& s) { RunSprinterDemo(s, "rotozoom", 700, Sound::Shipped); }
static void BM_SprinterDemo_Plasma2_Bare(benchmark::State& s) { RunSprinterDemo(s, "plasma2", 700, Sound::Bare); }
static void BM_SprinterDemo_Plasma2_Shipped(benchmark::State& s) { RunSprinterDemo(s, "plasma2", 700, Sound::Shipped); }
static void BM_SprinterDemo_BadApple_Bare(benchmark::State& s) { RunSprinterDemo(s, "badapple", 700, Sound::Bare); }
static void BM_SprinterDemo_BadApple_Shipped(benchmark::State& s) { RunSprinterDemo(s, "badapple", 700, Sound::Shipped); }

BENCHMARK(BM_SprinterDemo_DontBlink_Bare)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_DontBlink_Shipped)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_Rotozoom_Bare)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_Rotozoom_Shipped)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_Plasma2_Bare)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_Plasma2_Shipped)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_BadApple_Bare)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterDemo_BadApple_Shipped)->Iterations(500)->Unit(benchmark::kMicrosecond);
