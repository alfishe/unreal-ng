#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "3rdparty/message-center/messagecenter_fast.h"

/// End-to-end rate of the lock-free MessageCenterFast (post on one thread, dispatch on its own): upstream claims
/// multi-M/s. A speed of the host, so a benchmark and not a unit test (core/tests/common/messagecenter_test.cpp keeps
/// the lossless-delivery check). Counter: msg_per_s
namespace
{
void BM_MessageCenterFast_EndToEnd(benchmark::State& state)
{
    constexpr int kMessages = 100000;
    for (auto _ : state)
    {
        // Heap: the center is ~4 MB (its MPMC ring is a direct member)
        auto mc = std::make_unique<MessageCenterFast>();
        mc->start();
        std::atomic<int> received{0};
        const uint16_t topic = mc->registerTopic("bench");
        mc->addObserver(topic, [&](uint16_t, const void*, size_t) { received.fetch_add(1, std::memory_order_relaxed); });

        const uint32_t payload = 42;
        int posted = 0;
        for (int i = 0; i < kMessages; i++)
        {
            if (mc->post(topic, &payload, sizeof(payload)))
                posted++;
            else
                std::this_thread::yield();
        }
        while (received.load() < posted)
            std::this_thread::yield();
        mc->stop();
        state.counters["msg_per_s"] = benchmark::Counter(static_cast<double>(posted), benchmark::Counter::kIsRate);
    }
}
BENCHMARK(BM_MessageCenterFast_EndToEnd)->Iterations(3)->UseRealTime()->Unit(benchmark::kMillisecond);
}  // namespace
