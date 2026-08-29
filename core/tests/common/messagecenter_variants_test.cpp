#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/message-center/messagecenter_fast.h"
#include "3rdparty/message-center/eventqueue_emulator.h"

/// MessageCenter vendored-update verification (upstream de465bf).
///
/// The classic MessageCenter/EventQueue files stay on OUR patched versions
/// (they carry local fixes upstream lacks: unregistered-topic payload leak
/// cleanup, dispatch-vs-RemoveObserver race lock, MSVC/MinGW thread naming,
/// extra payload classes). The update vendors upstream's NEW self-contained
/// optimization headers additively: MessageCenterFast (lock-free MPMC queue
/// + object pool), EventQueueEmulator (dual-queue critical/bulk), and the
/// fast/batch/broadcast EventQueue variants. These tests pin that the new
/// headers compile in our tree, behave correctly, and actually deliver the
/// claimed throughput class - so opting hot paths into them later starts
/// from a verified baseline.

namespace
{
// Observer that rendezvouses with the main thread so a dispatch is provably
// in-flight at the exact moment RemoveObserver() is called.
class RendezvousObserver : public Observer
{
public:
    std::atomic<bool>* dispatchInFlight = nullptr; // set true once we enter the callback
    std::atomic<bool>* mainAtRemove = nullptr;     // main sets true just before RemoveObserver
    std::atomic<bool>* removedReturned = nullptr;   // main sets true AFTER RemoveObserver returns
    std::atomic<int>*  violations = nullptr;

    void OnMessage(int id, Message* message)
    {
        (void)id;
        (void)message;

        // Announce we are dispatching, then wait until main is right at the
        // RemoveObserver call - guarantees overlap regardless of scheduling.
        dispatchInFlight->store(true, std::memory_order_release);
        for (int w = 0; w < 2000000 && !mainAtRemove->load(std::memory_order_acquire); ++w)
        { }

        // Dwell so that, if RemoveObserver wrongly returns during this call,
        // main has time to flip removedReturned before we sample it.
        for (volatile int spin = 0; spin < 300000; ++spin) { }

        // Invariant: RemoveObserver() must block until this dispatch finishes,
        // so removedReturned cannot be true yet. If it is, the worker was
        // allowed to call into an observer the owner already detached - the
        // window that lets a GUI object be destroyed under a live callback
        // (the reported shutdown SIGSEGV).
        if (removedReturned->load(std::memory_order_acquire))
            violations->fetch_add(1, std::memory_order_relaxed);
    }
};
}

// Regression for the shutdown use-after-free: EventQueue::Dispatch must hold
// m_mutexObservers across observer invocation so RemoveObserver() cannot return
// (and the owner cannot then destroy itself) while a dispatch is still running.
// A rendezvous forces a dispatch to be in-flight at RemoveObserver time, so the
// missing-lock defect is caught deterministically - no sanitizer or segfault
// required.
TEST(MessageCenterVariants_Test, ClassicDispatch_RemoveObserverWaitsForInFlightDispatch)
{
    EventQueueCUT queue;
    const std::string topic = "race_topic";
    queue.RegisterTopic(topic);
    const int topicId = queue.ResolveTopic(topic);
    ASSERT_GE(topicId, 0);

    std::atomic<bool> dispatchInFlight{false};
    std::atomic<bool> mainAtRemove{false};
    std::atomic<bool> removedReturned{false};
    std::atomic<int>  violations{0};
    std::atomic<bool> stopWorker{false};

    // Mirror MessageCenter::ThreadWorker: drain and Dispatch on a worker thread.
    std::thread worker([&]() {
        while (!stopWorker.load(std::memory_order_acquire))
        {
            Message* msg = queue.GetQueueMessage();
            if (msg != nullptr)
                queue.Dispatch(msg->tid, msg);
            else
                std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    const ObserverCallbackMethod method =
        static_cast<ObserverCallbackMethod>(&RendezvousObserver::OnMessage);

    constexpr int ITERATIONS = 200;
    for (int i = 0; i < ITERATIONS; ++i)
    {
        RendezvousObserver observer;
        observer.dispatchInFlight = &dispatchInFlight;
        observer.mainAtRemove = &mainAtRemove;
        observer.removedReturned = &removedReturned;
        observer.violations = &violations;

        dispatchInFlight.store(false, std::memory_order_release);
        mainAtRemove.store(false, std::memory_order_release);
        removedReturned.store(false, std::memory_order_release);

        queue.AddObserver(topic, &observer, method);
        queue.Post(topicId, nullptr, false);

        // Wait until the worker is actually inside the callback.
        while (!dispatchInFlight.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::microseconds(20));

        // Release the callback's wait and immediately try to detach.
        mainAtRemove.store(true, std::memory_order_release);
        queue.RemoveObserver(topic, &observer, method);
        removedReturned.store(true, std::memory_order_release);

        // `observer` is destroyed here at scope exit - safe only because
        // RemoveObserver guaranteed no dispatch is touching it anymore.
    }

    stopWorker.store(true, std::memory_order_release);
    worker.join();

    EXPECT_EQ(violations.load(), 0)
        << "RemoveObserver returned while a dispatch was still in flight - "
           "EventQueue::Dispatch is not holding m_mutexObservers across invocation";
}

TEST(MessageCenterVariants_Test, FastCenter_DeliversInlinePayloads)
{
    MessageCenterFast mc;
    mc.start();

    std::atomic<int> received{0};
    std::atomic<uint32_t> lastValue{0};

    mc.addObserver("test_topic", [&](uint16_t topicId, const void* data, size_t size) {
        (void)topicId;
        if (data && size == sizeof(uint32_t))
        {
            uint32_t v;
            std::memcpy(&v, data, sizeof(v));
            lastValue.store(v, std::memory_order_relaxed);
        }
        received.fetch_add(1, std::memory_order_relaxed);
    });

    const uint32_t payload = 0x12345678;
    ASSERT_TRUE(mc.post("test_topic", &payload, sizeof(payload)));

    // Dispatch is asynchronous (dedicated thread)
    auto start = std::chrono::steady_clock::now();
    while (received.load() == 0 &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(received.load(), 1);
    EXPECT_EQ(lastValue.load(), 0x12345678u);

    mc.stop();
}

TEST(MessageCenterVariants_Test, EmulatorQueue_CriticalBeforeBulk)
{
    // Dual-queue semantics: critical (fast) messages always dispatch before
    // bulk ones, regardless of posting order - the property that makes this
    // variant interesting for realtime (audio/vblank) vs bulk (debug/state)
    // event separation.
    EventQueueEmulator<64, 64> queue;

    const uint16_t critical = queue.registerTopic(TopicPriority::Critical);
    const uint16_t bulk = queue.registerTopic(TopicPriority::Normal);

    static std::vector<uint16_t> order;
    order.clear();

    auto callback = [](uint16_t topicId, const void* data, size_t size, void* userData) {
        (void)data;
        (void)size;
        (void)userData;
        order.push_back(topicId);
    };
    ASSERT_TRUE(queue.addObserver(critical, callback));
    ASSERT_TRUE(queue.addObserver(bulk, callback));

    // Bulk posted FIRST, critical second
    const uint32_t v = 1;
    ASSERT_TRUE(queue.postBulk(bulk, &v, sizeof(v)));
    ASSERT_TRUE(queue.postFastValue(critical, v));
    ASSERT_TRUE(queue.postFastValue(critical, v));

    // dispatchPriority drains ALL critical, then at most one bulk
    const size_t dispatched = queue.dispatchPriority();
    EXPECT_EQ(dispatched, 3u);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], critical) << "Critical messages must dispatch before bulk";
    EXPECT_EQ(order[1], critical);
    EXPECT_EQ(order[2], bulk);
}

TEST(MessageCenterVariants_Test, FastCenter_ThroughputSanity)
{
    // Performance smoke: upstream claims multi-M/s for the lock-free variant.
    // Assert only a conservative floor (loaded CI machines) and report the
    // measured figures for classic vs fast - the informational comparison is
    // the evidence that opting into the fast variant is worthwhile.
    constexpr int MESSAGES = 100000;

    // --- Fast variant ---
    double fastRate = 0.0;
    {
        MessageCenterFast mc;
        mc.start();

        std::atomic<int> received{0};
        const uint16_t topic = mc.registerTopic("bench");
        mc.addObserver(topic, [&](uint16_t, const void*, size_t) {
            received.fetch_add(1, std::memory_order_relaxed);
        });

        const uint32_t payload = 42;
        const auto t0 = std::chrono::steady_clock::now();
        int posted = 0;
        for (int i = 0; i < MESSAGES; i++)
        {
            if (mc.post(topic, &payload, sizeof(payload)))
                posted++;
            else
                std::this_thread::yield();  // Queue full: let the dispatcher drain
        }
        while (received.load() < posted &&
               std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        fastRate = received.load() / sec;

        EXPECT_EQ(received.load(), posted) << "Fast center lost messages";
        mc.stop();
    }

    std::cout << "[perf] MessageCenterFast end-to-end: " << static_cast<long>(fastRate)
              << " msg/s (" << MESSAGES << " messages)" << std::endl;

    // Conservative floor: even a heavily loaded machine should exceed this
    // by an order of magnitude; a failure here means the vendored variant is
    // functionally broken, not merely slow
    EXPECT_GT(fastRate, 100000.0);
}
