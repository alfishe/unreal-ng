#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

/// General Sound card per-frame cost (NeoGS design §5.9, §9 phase 0 gate).
///
/// Measures one ZX frame of card time on the classic LLE card with the real
/// gs105a firmware, card only (no host machine):
///  - Idle: after POST, the firmware waits in its command loop
///  - Playing: a looped ProTracker module is playing (interrupt handler,
///    DAC fetches, sample mixing every 37.5 kHz period)
///
/// Phase 0 (shared runner extraction) must keep both within 2% of the numbers
/// taken before it; the NeoGS card budgets are expressed relative to them.
namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;

std::vector<uint8_t> BuildModule()
{
    std::vector<uint8_t> m(1084 + 2 * 1024 + 64, 0x00);
    m[20 + 23] = 32;
    m[20 + 25] = 63;
    m[20 + 29] = 32;
    m[950] = 2;
    m[953] = 1;
    m[1080] = 'M';
    m[1081] = '.';
    m[1082] = 'K';
    m[1083] = '.';
    m[1084 + 0] = 0x01;
    m[1084 + 1] = 0xAC;
    m[1084 + 2] = 0x10;
    for (size_t i = 0; i < 64; i++)
        m[1084 + 2 * 1024 + i] = (i / 16) % 2 ? 0x30 : 0xB0;
    return m;
}

struct Card
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<GeneralSoundCard> chip;

    explicit Card(bool neoGS = false)
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        if (neoGS)
        {
            chip = std::make_unique<SoundChip_NeoGS>(&ctx, NeoGSConfig{}, 44100);
            chip->loadROM("rom/neogs/full_ngs.rom");
        }
        else
        {
            chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
            chip->loadROM("rom/gs105a.rom");
        }
    }

    void frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }

    bool waitFlagClear(uint8_t mask)
    {
        for (int i = 0; i < 1000; i++)
        {
            if (!(chip->readStatus() & mask))
                return true;
            frame();
        }
        return false;
    }

    bool boot()
    {
        for (int i = 0; i < 1000; i++)
        {
            frame();
            if (chip->isReadyForCommands())
            {
                frame();
                if (chip->readStatus() & 0x80)
                    (void)chip->portDeviceInMethod(kPortData);
                return true;
            }
        }
        return false;
    }

    bool play()
    {
        chip->portDeviceOutMethod(kPortData, 0x01);
        chip->portDeviceOutMethod(kPortCommand, 0x30);
        if (!waitFlagClear(0x01))
            return false;
        frame();
        (void)chip->portDeviceInMethod(kPortData);
        for (uint8_t b : BuildModule())
        {
            chip->portDeviceOutMethod(kPortData, b);
            if (!waitFlagClear(0x80))
                return false;
        }
        chip->portDeviceOutMethod(kPortCommand, 0xD2);
        if (!waitFlagClear(0x01))
            return false;
        chip->portDeviceOutMethod(kPortData, 0x00);
        chip->portDeviceOutMethod(kPortCommand, 0x31);
        return waitFlagClear(0x01);
    }
};

void RunGeneralSoundFrames(benchmark::State& state, bool playing, bool neoGS = false)
{
    Card card(neoGS);
    if (!card.chip->isROMLoaded() || !card.boot() || (playing && !card.play()))
    {
        state.SkipWithError("firmware did not boot / play");
        return;
    }

    const uint64_t stepsBefore = card.chip->getActivityCounters().cpuSteps;
    for (auto _ : state)
    {
        card.frame();
        benchmark::DoNotOptimize(card.chip->getBuffer()[0]);
    }
    state.counters["steps/frame"] = benchmark::Counter(
        static_cast<double>(card.chip->getActivityCounters().cpuSteps - stepsBefore) / static_cast<double>(state.iterations()),
        benchmark::Counter::kDefaults);
}
} // namespace

static void BM_GeneralSoundFrame_Idle(benchmark::State& state)
{
    RunGeneralSoundFrames(state, false);
}
BENCHMARK(BM_GeneralSoundFrame_Idle)->Iterations(2000)->Unit(benchmark::kMicrosecond);

static void BM_GeneralSoundFrame_Playing(benchmark::State& state)
{
    RunGeneralSoundFrames(state, true);
}
BENCHMARK(BM_GeneralSoundFrame_Playing)->Iterations(2000)->Unit(benchmark::kMicrosecond);

/// NeoGS with its v1.11 flash (main ROM at 20 MHz). Budgets (neogs-tdd.md
/// §5.9): idle within 2x and playback within 2.5x of the classic card.
static void BM_NeoGSFrame_Idle(benchmark::State& state)
{
    RunGeneralSoundFrames(state, false, true);
}
BENCHMARK(BM_NeoGSFrame_Idle)->Iterations(2000)->Unit(benchmark::kMicrosecond);

static void BM_NeoGSFrame_Playing(benchmark::State& state)
{
    RunGeneralSoundFrames(state, true, true);
}
BENCHMARK(BM_NeoGSFrame_Playing)->Iterations(2000)->Unit(benchmark::kMicrosecond);
