#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
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

// LAYER_CAPTURE_BASELINE: built against a tree without the layer record (ScreenNext::SetLayerCapture), for the A/B of the renderer itself

/// The gate of docs/inprogress/2026-10-07-zx-next/design-layer-capture.md section 3.2: what recording the layers of a Next frame
/// costs. Two variants, both as INTERLEAVED A/B rounds (capture off, capture on, the order flipped every round) so that the load of
/// a busy machine hits both alike; the result is the MEDIAN of the per-round overhead and its spread, not one run:
///   (a) BM_NextFrame_LayerCapture/0  a whole emulated Next frame (CPU parked in RAM, all layers on, 64 sprites, a copper list with
///       16 mid-line writes) - the cost of a frame as the user sees it
///   (b) BM_NextFrame_LayerCapture/1  the bare ScreenNext::RenderLinesUpTo of the same scene (InitFrame + RenderFrameBatch)
///
///   core-benchmarks --benchmark_filter=BM_NextFrame_LayerCapture
/// The times are thread CPU time per frame. The counters: off_us / on_us (median per frame), overhead_pct (median of the per-round ratios), ci95_pct (95 % half-width of that median), p10_pct / p90_pct, rounds.
namespace
{
std::shared_ptr<Emulator> BootNext(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-next-layers", "NEXT", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return nullptr;
    }
    EmulatorContext* context = emulator->GetContext();
    context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    context->pFeatureManager->setFeature(Features::kSoundHQ, false);
    context->pMemory->UpdateFeatureCache();
    auto* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    if (!ports)
    {
        state.SkipWithError("not a Next decoder");
        return nullptr;
    }
    auto nr = [&](uint8_t reg, uint8_t value) {
        ports->DecodePortOut(0x243B, reg, 0);
        ports->DecodePortOut(0x253B, value, 0);
    };
    std::mt19937 random(1);
    for (unsigned page : {5u, 7u, 8u, 9u, 10u, 11u, 12u})
        for (unsigned i = 0; i < 0x4000; i++)
            context->pMemory->RAMPageAddress(page)[i] = static_cast<uint8_t>(random());
    // the CPU parks in RAM: DI; JR $
    context->pMemory->DirectWriteToZ80Memory(0xC000, 0xF3);
    context->pMemory->DirectWriteToZ80Memory(0xC001, 0x18);
    context->pMemory->DirectWriteToZ80Memory(0xC002, 0xFE);
    context->pCore->GetZ80()->pc = 0xC000;
    ports->DecodePortOut(0x123B, 0x02, 0);  // Layer 2 on
    nr(0x70, 0x10);                         // 320 x 256
    nr(0x12, 8);
    nr(0x6B, 0x80);  // tilemap on
    nr(0x6E, 0x20);
    nr(0x6F, 0x30);
    nr(0x4C, 0x0F);
    nr(0x15, 0x03 | (1 << 2));  // sprites on over the border, layer order LSU
    // 64 sprites with random patterns
    ports->DecodePortOut(0x303B, 0, 0);
    for (unsigned s = 0; s < 64; s++)
    {
        ports->DecodePortOut(0x303B, static_cast<uint8_t>(s), 0);
        const unsigned x = random() % 300, y = random() % 240;
        ports->DecodePortOut(0x57, static_cast<uint8_t>(x), 0);
        ports->DecodePortOut(0x57, static_cast<uint8_t>(y), 0);
        ports->DecodePortOut(0x57, static_cast<uint8_t>(x >> 8), 0);
        ports->DecodePortOut(0x57, static_cast<uint8_t>(0x80 | (random() & 0x3F)), 0);
    }
    ports->DecodePortOut(0x303B, 0, 0);
    for (unsigned i = 0; i < 16384; i++)
        ports->DecodePortOut(0x5B, static_cast<uint8_t>(random()), 0);
    // the copper: 16 mid-line writes of the global transparency colour (a harmless register that still splits the line)
    nr(0x61, 0);
    nr(0x62, 0);
    for (unsigned k = 0; k < 16; k++)
    {
        const uint16_t wait = static_cast<uint16_t>(0x8000 | ((10 + (k & 7) * 4) << 9) | (8 + k * 12));
        const uint16_t move = static_cast<uint16_t>(0x1400 | (0xE3 ^ (k & 1)));
        for (uint16_t w : {wait, move})
        {
            nr(0x60, static_cast<uint8_t>(w >> 8));
            nr(0x60, static_cast<uint8_t>(w & 0xFF));
        }
    }
    nr(0x60, 0xFF);
    nr(0x60, 0xFF);
    nr(0x62, 0xC0);
    return emulator;
}

/// CPU time of this thread in microseconds: a busy machine preempts the process, and wall time would charge that to whichever phase
/// it hit; the CPU time only grows while this thread runs (cache and SMT contention still show, which the interleaving averages out)
double ThreadCpuMicroseconds()
{
#ifdef _MSC_VER  // no POSIX thread clock: wall time (an interleaved A/B still cancels most of a loaded machine)
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) * 1e6 + static_cast<double>(t.tv_nsec) * 1e-3;
#endif
}

double Median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : (v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]));
}
/// 95 % half-width of the median (normal approximation: 1.2533 sigma / sqrt(n) * 1.96)
double MedianCi95(const std::vector<double>& v)
{
    if (v.size() < 2)
        return 0;
    double mean = 0, var = 0;
    for (double x : v)
        mean += x;
    mean /= static_cast<double>(v.size());
    for (double x : v)
        var += (x - mean) * (x - mean);
    var /= static_cast<double>(v.size() - 1);
    return 1.2533 * std::sqrt(var / static_cast<double>(v.size())) * 1.96;
}
double Percentile(std::vector<double> v, double p)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[static_cast<size_t>(p * (v.size() - 1))];
}
}  // namespace

#ifndef LAYER_CAPTURE_BASELINE
static void BM_NextFrame_LayerCapture(benchmark::State& state)
{
    const bool bareRender = state.range(0) == 1;
    auto emulator = BootNext(state);
    if (!emulator)
        return;
    EmulatorContext* context = emulator->GetContext();
    ScreenNext* screen = dynamic_cast<ScreenNext*>(context->pScreen);
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    if (!screen)
    {
        state.SkipWithError("not a ScreenNext");
        return;
    }
    constexpr unsigned kFramesPerPhase = 10;
    auto oneFrame = [&]() {
        if (bareRender)
        {
            screen->InitFrame();
            screen->RenderFrameBatch();
        }
        else
            mainLoop->RunFramePublic();
    };
    auto phase = [&](bool capture) {
        screen->SetLayerCapture(capture);
        oneFrame();  // the switch applies at a frame start; the first frame allocates / frees the sets
        oneFrame();
        const double t0 = ThreadCpuMicroseconds();
        for (unsigned f = 0; f < kFramesPerPhase; f++)
            oneFrame();
        return (ThreadCpuMicroseconds() - t0) / kFramesPerPhase;
    };
    for (unsigned warm = 0; warm < 30; warm++)
        oneFrame();

    std::vector<double> off, on, ratio;
    bool flip = false;
    for (auto _ : state)
    {
        double a, b;
        if (flip)
        {
            b = phase(true);
            a = phase(false);
        }
        else
        {
            a = phase(false);
            b = phase(true);
        }
        flip = !flip;
        off.push_back(a);
        on.push_back(b);
        ratio.push_back((b / a - 1.0) * 100.0);
        state.SetIterationTime((a + b) * 1e-6 * kFramesPerPhase);
    }
    state.counters["off_us"] = Median(off);
    state.counters["on_us"] = Median(on);
    state.counters["overhead_pct"] = Median(ratio);
    state.counters["ci95_pct"] = MedianCi95(ratio);
    state.counters["p10_pct"] = Percentile(ratio, 0.10);
    state.counters["p90_pct"] = Percentile(ratio, 0.90);
    state.counters["rounds"] = static_cast<double>(ratio.size());
    state.counters["layer_frames"] = static_cast<double>(screen->LayerFramesCaptured());
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_NextFrame_LayerCapture)->Arg(0)->Arg(1)->Iterations(200)->UseManualTime()->Unit(benchmark::kMicrosecond);
#endif

/// One configuration over many rounds, for comparing two BUILDS of the renderer (the tree before the layer record and after it):
///   BM_NextFrame_Plain/<0 full frame, 1 bare render>/<0 capture off, 1 capture on>
/// The same scene and the same timing as BM_NextFrame_LayerCapture; run the two binaries alternately and compare the medians.
static void BM_NextFrame_Plain(benchmark::State& state)
{
    const bool bareRender = state.range(0) == 1;
    auto emulator = BootNext(state);
    if (!emulator)
        return;
    EmulatorContext* context = emulator->GetContext();
    ScreenNext* screen = dynamic_cast<ScreenNext*>(context->pScreen);
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    if (!screen)
    {
        state.SkipWithError("not a ScreenNext");
        return;
    }
#ifndef LAYER_CAPTURE_BASELINE
    screen->SetLayerCapture(state.range(1) == 1);
#else
    if (state.range(1) == 1)
    {
        state.SkipWithError("no layer record in this tree");
        return;
    }
#endif
    constexpr unsigned kFramesPerRound = 10;
    auto oneFrame = [&]() {
        if (bareRender)
        {
            screen->InitFrame();
            screen->RenderFrameBatch();
        }
        else
            mainLoop->RunFramePublic();
    };
    for (unsigned warm = 0; warm < 30; warm++)
        oneFrame();
    std::vector<double> rounds;
    for (auto _ : state)
    {
        const double t0 = ThreadCpuMicroseconds();
        for (unsigned f = 0; f < kFramesPerRound; f++)
            oneFrame();
        const double us = (ThreadCpuMicroseconds() - t0) / kFramesPerRound;
        rounds.push_back(us);
        state.SetIterationTime(us * 1e-6 * kFramesPerRound);
    }
    state.counters["median_us"] = Median(rounds);
    state.counters["p10_us"] = Percentile(rounds, 0.10);
    state.counters["p90_us"] = Percentile(rounds, 0.90);
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_NextFrame_Plain)->Args({0, 0})->Args({0, 1})->Args({1, 0})->Args({1, 1})->Iterations(200)->UseManualTime()->Unit(benchmark::kMicrosecond);
