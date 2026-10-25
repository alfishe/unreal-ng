#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "common/timehelper.h"

/// How late TimeHelper::WaitUntilPrecise wakes: the emulation thread's frame clock, whose lateness is part of the
/// audio latency budget (SoundAdaptivity.AVLatencyBudget). On Windows std::condition_variable::wait_until wakes 1 ms
/// (MSVC) to 10-17 ms (MinGW winpthreads) late; the helper must stay within FRAME_PACING_JITTER_BUDGET_MS.
/// This is a speed of the host and of the OS scheduler, so it is a benchmark and not a unit test (a test must not
/// touch the wall clock). Counters: p50 / p90 / max lateness in ms; `over_budget` = 1 when p90 is above the budget
namespace
{
void BM_TimeHelper_WakeLateness(benchmark::State& state)
{
    using clock = std::chrono::steady_clock;
    const auto frame = std::chrono::microseconds(5000);
    std::vector<double> lateMs;
    for (auto _ : state)
    {
        auto next = clock::now();
        for (int i = 0; i < 24; i++)
        {
            next += frame;
            TimeHelper::WaitUntilPrecise(next, [] { return false; });
            const auto now = clock::now();
            lateMs.push_back(std::chrono::duration<double, std::milli>(now - next).count());
            if (now > next + frame)
                next = now;
        }
    }
    std::sort(lateMs.begin(), lateMs.end());
    state.counters["p50_ms"] = lateMs[lateMs.size() / 2];
    state.counters["p90_ms"] = lateMs[(lateMs.size() * 9) / 10];
    state.counters["max_ms"] = lateMs.back();
    state.counters["over_budget"] = lateMs[(lateMs.size() * 9) / 10] >= TimeHelper::FRAME_PACING_JITTER_BUDGET_MS ? 1 : 0;
}
BENCHMARK(BM_TimeHelper_WakeLateness)->Iterations(5)->UseRealTime()->Unit(benchmark::kMillisecond);

/// From the notification to the return: a WakeSignal ends the wait at once, not at the end of the 4 ms poll chunk
void BM_TimeHelper_WakeSignalLatency(benchmark::State& state)
{
    using clock = std::chrono::steady_clock;
    double bestMs = 1e9;
    for (auto _ : state)
    {
        WakeSignal wake;
        std::atomic<bool> waiting{false};
        std::atomic<bool> abort{false};
        clock::time_point notifiedAt;
        std::thread notifier([&] {
            while (!waiting.load(std::memory_order_acquire))
                std::this_thread::yield();
            notifiedAt = clock::now();
            abort.store(true, std::memory_order_release);
            wake.Notify();
        });
        TimeHelper::WaitUntilPrecise(
            clock::now() + std::chrono::seconds(2),
            [&] {
                waiting.store(true, std::memory_order_release);
                return abort.load(std::memory_order_acquire);
            },
            &wake);
        const auto returnedAt = clock::now();
        notifier.join();
        bestMs = std::min(bestMs, std::chrono::duration<double, std::milli>(returnedAt - notifiedAt).count());
    }
    state.counters["best_ms"] = bestMs;
}
BENCHMARK(BM_TimeHelper_WakeSignalLatency)->Iterations(12)->UseRealTime()->Unit(benchmark::kMillisecond);
}  // namespace
