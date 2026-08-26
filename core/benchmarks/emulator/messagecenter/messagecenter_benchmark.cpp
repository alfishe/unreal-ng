#include "messagecenter_benchmark.h"

#include <benchmark/benchmark.h>
#include <atomic>

#include "3rdparty/message-center/eventqueue_fast.h"

static std::atomic<uint64_t> g_callbackCount{0};

static void FastCallback(int id, FastMessage* msg)
{
    g_callbackCount++;
    (void)id;
    (void)msg;
}

// Topic constants
static const char* TOPIC_VIDEO = "video_frame";
static const char* TOPIC_AUDIO = "audio_frame";
static const char* TOPIC_STATE = "state_change";
static const char* TOPIC_STEP = "cpu_step";
static const char* TOPIC_BREAK = "breakpoint";
static const char* TOPIC_RESET = "system_reset";

/// Frame refresh - hot path
static void BM_FrameRefreshPattern(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_VIDEO, FastCallback);

    uint32_t frameCounter = 0;
    for (auto _ : state)
    {
        auto* payload = new FastFramePayload("test-emulator", frameCounter++);
        eq.Post(TOPIC_VIDEO, payload, true);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FrameRefreshPattern)->Iterations(10000);

/// Multi-instance pattern
static void BM_MultiInstancePattern(benchmark::State& state)
{
    const int numInstances = state.range(0);
    FastEventQueue eq;
    eq.AddObserver(TOPIC_VIDEO, FastCallback);

    uint32_t frameCounter = 0;
    for (auto _ : state)
    {
        for (int i = 0; i < numInstances; i++)
        {
            auto* payload = new FastFramePayload("emulator-" + std::to_string(i), frameCounter);
            eq.Post(TOPIC_VIDEO, payload, true);
        }
        frameCounter++;
    }

    state.SetItemsProcessed(state.iterations() * numInstances);
}
BENCHMARK(BM_MultiInstancePattern)->Arg(1)->Arg(4)->Arg(8)->Arg(16);

/// Observer fanout callbacks
static void Fanout0(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout1(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout2(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout3(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout4(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout5(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout6(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout7(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout8(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout9(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout10(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout11(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout12(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout13(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout14(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }
static void Fanout15(int id, FastMessage* m) { g_callbackCount++; (void)id; (void)m; }

static FastObserverCallback g_fanout[] = {
    Fanout0, Fanout1, Fanout2, Fanout3, Fanout4, Fanout5, Fanout6, Fanout7,
    Fanout8, Fanout9, Fanout10, Fanout11, Fanout12, Fanout13, Fanout14, Fanout15
};

/// Observer fanout - dispatch to multiple observers
static void BM_ObserverFanout(benchmark::State& state)
{
    const int numObservers = state.range(0);
    FastEventQueue eq;

    for (int i = 0; i < numObservers; i++)
    {
        eq.AddObserver(TOPIC_STATE, g_fanout[i]);
    }

    for (auto _ : state)
    {
        auto* payload = new FastNumberPayload(1);
        eq.Post(TOPIC_STATE, payload, true);
    }

    state.SetItemsProcessed(state.iterations() * numObservers);
}
BENCHMARK(BM_ObserverFanout)->Arg(1)->Arg(4)->Arg(8)->Arg(16);

/// Debug stepping burst
static void BM_DebugStepBurst(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_STEP, FastCallback);
    eq.AddObserver(TOPIC_BREAK, FastCallback);

    int stepCount = 0;
    for (auto _ : state)
    {
        eq.Post(TOPIC_STEP);
        if (++stepCount % 1000 == 0)
        {
            auto* payload = new FastNumberPayload(stepCount);
            eq.Post(TOPIC_BREAK, payload, true);
        }
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DebugStepBurst)->Iterations(100000);

/// No payload
static void BM_PayloadAllocation_None(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_RESET, FastCallback);

    for (auto _ : state)
    {
        eq.Post(TOPIC_RESET);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PayloadAllocation_None)->Iterations(50000);

/// Number payload
static void BM_PayloadAllocation_Number(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_BREAK, FastCallback);

    for (auto _ : state)
    {
        auto* payload = new FastNumberPayload(12345);
        eq.Post(TOPIC_BREAK, payload, true);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PayloadAllocation_Number)->Iterations(50000);

/// Text payload
static void BM_PayloadAllocation_Text(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_STATE, FastCallback);

    for (auto _ : state)
    {
        auto* payload = new FastTextPayload("test-emulator-uuid-12345678");
        eq.Post(TOPIC_STATE, payload, true);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PayloadAllocation_Text)->Iterations(50000);

/// Frame payload
static void BM_PayloadAllocation_Frame(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_VIDEO, FastCallback);

    uint32_t counter = 0;
    for (auto _ : state)
    {
        auto* payload = new FastFramePayload("test-emulator-uuid-12345678", counter++);
        eq.Post(TOPIC_VIDEO, payload, true);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PayloadAllocation_Frame)->Iterations(50000);

/// Topic registration
static void BM_TopicRegistration(benchmark::State& state)
{
    FastEventQueue eq;

    for (auto _ : state)
    {
        eq.RegisterTopic(TOPIC_STATE);
        eq.RegisterTopic(TOPIC_VIDEO);
        eq.RegisterTopic(TOPIC_AUDIO);
        eq.RegisterTopic(TOPIC_STEP);
        eq.RegisterTopic(TOPIC_BREAK);
        eq.RegisterTopic(TOPIC_RESET);
    }

    state.SetItemsProcessed(state.iterations() * 6);
}
BENCHMARK(BM_TopicRegistration)->Iterations(10000);

/// Full frame cycle
static void BM_FullFrameCycle(benchmark::State& state)
{
    FastEventQueue eq;
    eq.AddObserver(TOPIC_VIDEO, FastCallback);
    eq.AddObserver(TOPIC_AUDIO, FastCallback);

    uint32_t frameCounter = 0;
    for (auto _ : state)
    {
        auto* videoPayload = new FastFramePayload("test-emulator", frameCounter);
        eq.Post(TOPIC_VIDEO, videoPayload, true);
        eq.Post(TOPIC_AUDIO);
        frameCounter++;
    }

    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_FullFrameCycle)->Iterations(10000);
