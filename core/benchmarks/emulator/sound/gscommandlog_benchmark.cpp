#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gscommandlog.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

/// The General Sound command log's cost on the host port path (gs-debugger design §6.3, P2).
///
/// One iteration is one frame of the LLE card (gs105a) inside a module upload: 256 host data writes (#B3), the
/// card's frame, a status read and a reply read. Three variants, run interleaved:
///  - None: no log attached (a null test)
///  - Off: the machine's log attached, history off (a null test and a relaxed load)
///  - Ring: the log recording (the decoder counts the upload's bytes)
namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;

enum class LogMode
{
    None,
    Off,
    Ring
};

struct Card
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;
    GSCommandLog log{&ctx, nullptr, "gs.commands"};

    Card()
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM("rom/gs105a.rom");
    }
    ~Card() { chip->attachCommandLog(nullptr); }

    void frame()
    {
        ctx.emulatorState.frame_counter++;
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
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
};

void RunHostTraffic(benchmark::State& state, LogMode mode)
{
    Card card;
    if (!card.chip->isROMLoaded() || !card.boot())
    {
        state.SkipWithError("firmware did not boot");
        return;
    }
    if (mode != LogMode::None)
        card.chip->attachCommandLog(&card.log);
    if (mode == LogMode::Ring)
    {
        std::string error;
        card.log.History().Configure({.mode = history::Mode::Ring}, error);
    }
    card.chip->portDeviceOutMethod(kPortCommand, 0x30);  // an upload the iterations feed
    card.frame();
    (void)card.chip->portDeviceInMethod(kPortData);

    uint8_t byte = 0;
    for (auto _ : state)
    {
        for (int i = 0; i < 256; i++)
            card.chip->portDeviceOutMethod(kPortData, byte++);
        card.frame();
        benchmark::DoNotOptimize(card.chip->portDeviceInMethod(kPortCommand));
        benchmark::DoNotOptimize(card.chip->portDeviceInMethod(kPortData));
    }
    state.counters["kept"] = static_cast<double>(card.log.History().GetStats().kept);
}

void BM_GSCommandLog_HostTraffic_None(benchmark::State& state) { RunHostTraffic(state, LogMode::None); }
void BM_GSCommandLog_HostTraffic_Off(benchmark::State& state) { RunHostTraffic(state, LogMode::Off); }
void BM_GSCommandLog_HostTraffic_Ring(benchmark::State& state) { RunHostTraffic(state, LogMode::Ring); }
} // namespace

BENCHMARK(BM_GSCommandLog_HostTraffic_None)->Iterations(2000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_GSCommandLog_HostTraffic_Off)->Iterations(2000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_GSCommandLog_HostTraffic_Ring)->Iterations(2000)->Unit(benchmark::kMicrosecond);
