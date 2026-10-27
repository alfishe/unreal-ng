// What a ZX Next frame costs by feature, to find the hot paths worth working on (docs/inprogress/2026-10-16-next-runtime-profile):
// every scene is a booted NEXT with a small Z80 program (or none: the CPU parked) and one thing switched on; the result is the median
// thread-CPU microseconds of a whole emulated frame (MainLoop::RunFrame), and the share of the 20 ms a 50 Hz frame has.
//
//   core-benchmarks --benchmark_filter='BM_NextRuntime/'            all scenes
//   core-benchmarks --benchmark_filter='BM_NextRuntime/cpu28_busy' --benchmark_min_time=20x     one scene, to sample the process with
//
// The scenes are named group_what; the groups: base (nothing but the frame), cpu (a busy Z80 program at each speed), screen (the ULA modes),
// layer (Layer 2, tilemap, sprites), audio (3 AYs, the DACs, sound synthesis on), divmmc, dma, copper.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/next/screennext.h"

namespace
{
struct Machine
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    PortDecoder_Next* ports = nullptr;
    std::mt19937 random{1};

    void Nr(uint8_t reg, uint8_t value)
    {
        ports->DecodePortOut(0x243B, reg, 0);
        ports->DecodePortOut(0x253B, value, 0);
    }
    void Out(uint16_t port, uint8_t value) { ports->DecodePortOut(port, value, 0); }
    void Poke(uint16_t address, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            context->pMemory->DirectWriteToZ80Memory(address++, b);
    }
    void Run(uint16_t pc) { context->pCore->GetZ80()->pc = pc; }
    void RandomPages(std::initializer_list<unsigned> pages)
    {
        for (unsigned page : pages)
            for (unsigned i = 0; i < 0x4000; i++)
                context->pMemory->RAMPageAddress(page)[i] = static_cast<uint8_t>(random());
    }
    void Park() { Poke(0xC000, {0xF3, 0x18, 0xFE}), Run(0xC000); }  // DI; JR $
    /// A loop that reads, increments and writes back every byte of #8000-#BFFF, forever: memory traffic, flags, a taken branch
    void Busy()
    {
        Poke(0xC000, {0xF3, 0x21, 0x00, 0x80, 0x7E, 0x3C, 0x77, 0x23, 0x7C, 0xFE, 0xC0, 0x20, 0xF7, 0x21, 0x00, 0x80, 0x18, 0xF2});
        Run(0xC000);
    }
    void Sprites(unsigned count, bool big)
    {
        Out(0x303B, 0);
        for (unsigned s = 0; s < count; s++)
        {
            Out(0x303B, static_cast<uint8_t>(s));
            const unsigned x = random() % 300, y = random() % 240;
            Out(0x57, static_cast<uint8_t>(x));
            Out(0x57, static_cast<uint8_t>(y));
            Out(0x57, static_cast<uint8_t>(x >> 8));
            Out(0x57, static_cast<uint8_t>(0x80 | (random() & 0x3F)));
            if (big)
            {
                // an extended sprite scaled x2 would need the fifth byte; the plain 16 x 16 ones are the 128-sprite case
            }
        }
        Out(0x303B, 0);
        for (unsigned i = 0; i < 16384; i++)
            Out(0x5B, static_cast<uint8_t>(random()));
        Nr(0x15, 0x03 | (1 << 2));
    }
    void Ay(unsigned chip, uint8_t tone)
    {
        Out(0xFFFD, static_cast<uint8_t>(0xFF - chip));  // the turbosound chip select
        auto reg = [&](uint8_t r, uint8_t v) {
            Out(0xFFFD, r);
            Out(0xBFFD, v);
        };
        reg(0, tone);
        reg(1, 0);
        reg(2, static_cast<uint8_t>(tone + 7));
        reg(3, 0);
        reg(4, static_cast<uint8_t>(tone + 13));
        reg(5, 0);
        reg(6, 9);
        reg(7, 0x38);  // tones on, noise off
        reg(8, 15);
        reg(9, 15);
        reg(10, 15);
    }
};

std::unique_ptr<Machine> Boot(benchmark::State& state, bool soundHq)
{
    auto m = std::make_unique<Machine>();
    m->emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("bench-next-runtime", "NEXT", LoggerLevel::LogNone);
    if (!m->emulator)
    {
        state.SkipWithError("emulator creation failed");
        return nullptr;
    }
    m->context = m->emulator->GetContext();
    m->context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    m->context->pFeatureManager->setFeature(Features::kSoundHQ, soundHq);
    m->context->pMemory->UpdateFeatureCache();
    m->ports = dynamic_cast<PortDecoder_Next*>(m->context->pPortDecoder);
    if (!m->ports)
    {
        state.SkipWithError("not a Next decoder");
        return nullptr;
    }
    m->RandomPages({5, 7, 8, 9, 10, 11, 12});
    m->Park();
    return m;
}

double ThreadCpuMicroseconds()
{
    timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) * 1e6 + static_cast<double>(t.tv_nsec) * 1e-3;
}

double Median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : (v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]));
}

struct Scene
{
    const char* name;
    bool soundHq;
    std::function<void(Machine&)> setup;
};

// The 1 KB program of a zxnDMA transfer: reset, WR0 (A -> B, source, length), WR1 (A memory, increment), WR2 (B memory, increment), WR4
// (continuous, destination), load, enable
const uint8_t kDmaProgram[] = {0xC3, 0x7D, 0x00, 0x80, 0x00, 0x18, 0x14, 0x10, 0xCD, 0x00, 0xA0, 0xCF, 0x87};

std::vector<Scene> Scenes()
{
    std::vector<Scene> s;
    s.push_back({"base_parked_std", false, [](Machine&) {}});
    for (auto [name, speed] : {std::pair{"cpu3_busy", 0}, {"cpu7_busy", 1}, {"cpu14_busy", 2}, {"cpu28_busy", 3}})
        s.push_back({name, false, [speed](Machine& m) {
                         m.Busy();
                         m.Nr(0x07, static_cast<uint8_t>(speed));
                     }});
    // screen modes, the CPU parked
    s.push_back({"screen_hires", false, [](Machine& m) { m.Out(0x00FF, 0x06); }});
    s.push_back({"screen_hicolour", false, [](Machine& m) { m.Out(0x00FF, 0x02); }});
    s.push_back({"screen_ulanext", false, [](Machine& m) { m.Nr(0x42, 0xFF), m.Nr(0x43, 0x01); }});
    s.push_back({"screen_ulaplus", false, [](Machine& m) { m.Nr(0x68, 0x08); }});
    s.push_back({"screen_lores", false, [](Machine& m) { m.Nr(0x15, 0x80); }});
    s.push_back({"screen_radastan", false, [](Machine& m) { m.Nr(0x15, 0x80), m.Nr(0x6A, 0x20); }});
    s.push_back({"screen_ula_scrolled", false, [](Machine& m) { m.Nr(0x26, 3), m.Nr(0x27, 5); }});
    // layers
    s.push_back({"layer_l2_256", false, [](Machine& m) { m.Out(0x123B, 0x02), m.Nr(0x70, 0x00), m.Nr(0x12, 8); }});
    s.push_back({"layer_l2_320", false, [](Machine& m) { m.Out(0x123B, 0x02), m.Nr(0x70, 0x10), m.Nr(0x12, 8); }});
    s.push_back({"layer_l2_640", false, [](Machine& m) { m.Out(0x123B, 0x02), m.Nr(0x70, 0x20), m.Nr(0x12, 8); }});
    s.push_back({"layer_tilemap40", false, [](Machine& m) { m.Nr(0x6B, 0x80), m.Nr(0x6E, 0x20), m.Nr(0x6F, 0x30), m.Nr(0x4C, 0x0F); }});
    s.push_back({"layer_tilemap80", false, [](Machine& m) { m.Nr(0x6B, 0xC0), m.Nr(0x6E, 0x20), m.Nr(0x6F, 0x30), m.Nr(0x4C, 0x0F); }});
    s.push_back({"layer_sprites64", false, [](Machine& m) { m.Sprites(64, false); }});
    s.push_back({"layer_sprites128", false, [](Machine& m) { m.Sprites(128, false); }});
    s.push_back({"layer_all", false, [](Machine& m) {
                     m.Out(0x123B, 0x02), m.Nr(0x70, 0x10), m.Nr(0x12, 8);
                     m.Nr(0x6B, 0x80), m.Nr(0x6E, 0x20), m.Nr(0x6F, 0x30), m.Nr(0x4C, 0x0F);
                     m.Sprites(128, false);
                 }});
    // audio: the CPU parked, the chips programmed; the sound synthesis off and on
    s.push_back({"audio_ay3_nosynth", false, [](Machine& m) {
                     m.Nr(0x08, 0x12);
                     for (unsigned c = 0; c < 3; c++)
                         m.Ay(c, static_cast<uint8_t>(20 + c * 30));
                 }});
    s.push_back({"audio_ay3_synth", true, [](Machine& m) {
                     m.Nr(0x08, 0x12);
                     for (unsigned c = 0; c < 3; c++)
                         m.Ay(c, static_cast<uint8_t>(20 + c * 30));
                 }});
    s.push_back({"audio_ay1_synth", true, [](Machine& m) { m.Ay(0, 40); }});
    s.push_back({"audio_synth_silent", true, [](Machine&) {}});
    s.push_back({"audio_dac_stream", true, [](Machine& m) {
                     m.Nr(0x08, 0x08);  // DAC on
                     m.Nr(0x84, 0x20);  // #FB mono AD
                     // loop: LD A,(HL); OUT (#FB),A; INC HL; JR loop
                     m.Poke(0xC000, {0xF3, 0x21, 0x00, 0x80, 0x7E, 0xD3, 0xFB, 0x23, 0x18, 0xF9});
                     m.Run(0xC000);
                     m.Nr(0x07, 3);
                 }});
    // DivMMC: the automap hook on every opcode fetch; the CPU busy in RAM, the automap off / on, and running inside the mapped window
    s.push_back({"divmmc_busy_automap_off", false, [](Machine& m) { m.Busy(), m.Nr(0x0A, 0x00); }});
    s.push_back({"divmmc_busy_automap_on", false, [](Machine& m) { m.Busy(), m.Nr(0x0A, 0x10); }});
    s.push_back({"divmmc_busy_conmem", false, [](Machine& m) {
                     m.Nr(0x0A, 0x10);
                     m.Out(0x00E3, 0x80);  // CONMEM: #0000-#1FFF the DivMMC ROM, #2000-#3FFF RAM bank 0
                     m.Poke(0x2000, {0xF3, 0x21, 0x00, 0x80, 0x7E, 0x3C, 0x77, 0x23, 0x7C, 0xFE, 0xC0, 0x20, 0xF7, 0x21, 0x00, 0x80, 0x18, 0xF2});
                     m.Run(0x2000);
                 }});
    // DMA: a continuous transfer of 6 KB over and over
    s.push_back({"dma_ram_to_ram", false, [](Machine& m) {
                     for (unsigned i = 0; i < sizeof kDmaProgram; i++)
                         m.context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xC100 + i), kDmaProgram[i]);
                     // LD HL,#C100; LD B,13; LD C,#6B; OTIR; JR start
                     m.Poke(0xC000, {0xF3, 0x21, 0x00, 0xC1, 0x06, sizeof kDmaProgram, 0x0E, 0x6B, 0xED, 0xB3, 0x18, 0xF4});
                     m.Run(0xC000);
                     m.Nr(0x07, 3);
                 }});
    // copper: 200 waits and moves a frame
    s.push_back({"copper_busy", false, [](Machine& m) {
                     m.Nr(0x61, 0);
                     m.Nr(0x62, 0);
                     for (unsigned k = 0; k < 200; k++)
                     {
                         const uint16_t wait = static_cast<uint16_t>(0x8000 | (((k * 7) % 192) << 9) | (k % 50));
                         const uint16_t move = static_cast<uint16_t>(0x1400 | (0xE3 ^ (k & 1)));
                         for (uint16_t w : {wait, move})
                         {
                             m.Nr(0x60, static_cast<uint8_t>(w >> 8));
                             m.Nr(0x60, static_cast<uint8_t>(w & 0xFF));
                         }
                     }
                     m.Nr(0x60, 0xFF);
                     m.Nr(0x60, 0xFF);
                     m.Nr(0x62, 0xC0);
                 }});
    // a plausible game: busy CPU at 28 MHz, Layer 2, tilemap, 64 sprites, 3 AYs with synthesis, a DAC, the copper
    s.push_back({"mix_game28", true, [](Machine& m) {
                     m.Busy();
                     m.Nr(0x07, 3);
                     m.Out(0x123B, 0x02), m.Nr(0x70, 0x10), m.Nr(0x12, 8);
                     m.Nr(0x6B, 0x80), m.Nr(0x6E, 0x20), m.Nr(0x6F, 0x30), m.Nr(0x4C, 0x0F);
                     m.Sprites(64, false);
                     m.Nr(0x08, 0x12);
                     for (unsigned c = 0; c < 3; c++)
                         m.Ay(c, static_cast<uint8_t>(20 + c * 30));
                 }});
    return s;
}

void RunScene(benchmark::State& state, const Scene* scene)
{
    auto machine = Boot(state, scene->soundHq);
    if (!machine)
        return;
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(machine->context->pMainLoop);
    scene->setup(*machine);
    for (unsigned warm = 0; warm < 30; warm++)
        mainLoop->RunFramePublic();
    constexpr unsigned kFramesPerRound = 10;
    std::vector<double> rounds;
    for (auto _ : state)
    {
        const double t0 = ThreadCpuMicroseconds();
        for (unsigned f = 0; f < kFramesPerRound; f++)
            mainLoop->RunFramePublic();
        const double us = (ThreadCpuMicroseconds() - t0) / kFramesPerRound;
        rounds.push_back(us);
        state.SetIterationTime(us * 1e-6 * kFramesPerRound);
    }
    const double median = Median(rounds);
    state.counters["frame_us"] = median;
    state.counters["of_20ms_pct"] = median / 200.0;
    EmulatorManager::GetInstance()->RemoveEmulator(machine->emulator->GetUUID());
}

const std::vector<Scene>& AllScenes()
{
    static const std::vector<Scene> scenes = Scenes();
    return scenes;
}

const bool registered = []() {
    for (const Scene& scene : AllScenes())
        benchmark::RegisterBenchmark((std::string("BM_NextRuntime/") + scene.name).c_str(), RunScene, &scene)
            ->Iterations(40)
            ->UseManualTime()
            ->Unit(benchmark::kMicrosecond);
    return true;
}();
}  // namespace
