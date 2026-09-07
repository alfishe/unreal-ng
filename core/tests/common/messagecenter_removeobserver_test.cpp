#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "3rdparty/message-center/eventqueue.h"

/// Regression tests for observer removal in EventQueue.
///
/// History: the former RemoveObserver(topic, ObserverCallbackFunc) identified an observer by
/// reading the first machine word of the std::function object. For a closure that fits libc++'s
/// inline buffer (3 words) that word is the type-erasure vtable pointer, so copies of one lambda
/// compared equal by accident; for a larger closure it is a per-copy heap pointer and removal
/// silently did nothing. The leaked observer kept references into the caller's dead stack frame
/// and the dispatcher thread later wrote through them (VideoModeChange_Test's stale
/// std::atomic<int>::fetch_add incremented a saved frame pointer -> byte-shifted UnitTestImpl*
/// -> Z80TestVerification.RunAllVectors segfault, only under parallel/loaded runs).
/// Removal is now ID-based; these tests pin that contract.

namespace
{
void plainCallbackA(int, Message*) {}
void plainCallbackB(int, Message*) {}

size_t ObserverCount(EventQueueCUT& queue, const std::string& topic)
{
    ObserverVectorPtr observers = queue.GetObservers(queue.ResolveTopic(topic));
    return observers ? observers->size() : 0;
}

void DispatchTopic(EventQueueCUT& queue, const std::string& topic)
{
    // Dispatch() owns and deletes the message
    queue.Dispatch(queue.ResolveTopic(topic), new Message(0, nullptr, false));
}
}  // namespace

TEST(EventQueueObserverRemoval_Test, AddObserverReturnsUniqueNonZeroIds)
{
    EventQueueCUT queue;
    const std::string topic = "observer_removal_ids";

    int counter = 0;
    auto handler = [&counter](int, Message*) { counter++; };

    uint64_t first = queue.AddObserver(topic, handler);
    uint64_t second = queue.AddObserver(topic, handler);
    uint64_t third = queue.AddObserver(topic, ObserverCallbackFunc(&plainCallbackA));

    EXPECT_NE(first, 0u);
    EXPECT_NE(second, 0u);
    EXPECT_NE(third, 0u);
    EXPECT_NE(first, second) << "the same lambda registered twice must yield two distinct observers";
    EXPECT_NE(second, third);
    EXPECT_EQ(ObserverCount(queue, topic), 3u);
}

TEST(EventQueueObserverRemoval_Test, LargeClosureLambdaIsRemovedById)
{
    EventQueueCUT queue;
    const std::string topic = "observer_removal_large_closure";

    int counter = 0;
    std::array<uint8_t, 64> ballast{};  // closure larger than the std::function inline buffer -> heap-stored
    auto handler = [&counter, ballast](int, Message*) { counter += ballast[0] + 1; };
    static_assert(sizeof(handler) > 3 * sizeof(void*), "closure must exceed the std::function inline buffer");

    uint64_t id = queue.AddObserver(topic, handler);
    ASSERT_EQ(ObserverCount(queue, topic), 1u);

    queue.RemoveObserverById(topic, id);
    EXPECT_EQ(ObserverCount(queue, topic), 0u);

    DispatchTopic(queue, topic);
    EXPECT_EQ(counter, 0) << "removed observer must not be invoked";
}

TEST(EventQueueObserverRemoval_Test, RemovingOneIdKeepsTheOthers)
{
    EventQueueCUT queue;
    const std::string topic = "observer_removal_selective";

    int first = 0;
    int second = 0;
    int third = 0;
    // Two observers of the SAME closure type plus a different one: ID removal must hit exactly one
    auto handlerA = [&first](int, Message*) { first++; };
    auto handlerB = [&second](int, Message*) { second++; };
    auto handlerC = [&third](int, Message*) { third++; };

    uint64_t idA = queue.AddObserver(topic, handlerA);
    queue.AddObserver(topic, handlerB);
    queue.AddObserver(topic, handlerC);
    ASSERT_EQ(ObserverCount(queue, topic), 3u);

    queue.RemoveObserverById(topic, idA);
    ASSERT_EQ(ObserverCount(queue, topic), 2u);

    DispatchTopic(queue, topic);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(third, 1);
}

TEST(EventQueueObserverRemoval_Test, UnknownOrZeroIdIsANoOp)
{
    EventQueueCUT queue;
    const std::string topic = "observer_removal_noop";

    int counter = 0;
    uint64_t id = queue.AddObserver(topic, [&counter](int, Message*) { counter++; });
    ASSERT_EQ(ObserverCount(queue, topic), 1u);

    queue.RemoveObserverById(topic, 0);
    queue.RemoveObserverById(topic, id + 1000);
    queue.RemoveObserverById("observer_removal_other_topic", id);
    EXPECT_EQ(ObserverCount(queue, topic), 1u);

    DispatchTopic(queue, topic);
    EXPECT_EQ(counter, 1);
}

TEST(EventQueueObserverRemoval_Test, PlainFunctionPointersStillRemovedByAddress)
{
    EventQueueCUT queue;
    const std::string topic = "observer_removal_function_pointers";

    queue.AddObserver(topic, plainCallbackA);
    queue.AddObserver(topic, plainCallbackB);
    ASSERT_EQ(ObserverCount(queue, topic), 2u);

    queue.RemoveObserver(topic, plainCallbackA);
    ASSERT_EQ(ObserverCount(queue, topic), 1u);

    ObserverVectorPtr observers = queue.GetObservers(queue.ResolveTopic(topic));
    ASSERT_NE(observers, nullptr);
    EXPECT_EQ(observers->front()->callback, &plainCallbackB);
}
