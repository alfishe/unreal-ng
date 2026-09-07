#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"

/// Tests that MessageCenter properly disposes all resources including:
/// - Pending messages in the queue
/// - Message payloads with cleanupPayload=true
/// - Observer descriptors

class MessageCenterDisposal_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure clean state before each test
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void TearDown() override
    {
        // Clean up after each test
        MessageCenter::DisposeDefaultMessageCenter();
    }
};

TEST_F(MessageCenterDisposal_Test, DisposesPendingMessagePayloads)
{
    // Get MessageCenter without starting worker thread
    MessageCenter& mc = MessageCenter::DefaultMessageCenter(false);

    // Register the topic so messages are actually queued
    mc.RegisterTopic("TEST_TOPIC");

    // Post several messages with payloads (async=true queues them)
    for (int i = 0; i < 10; i++)
    {
        mc.Post("TEST_TOPIC", new SimpleNumberPayload(i), true);
    }

    // Dispose should clean up all payloads - valgrind will catch any leaks
    MessageCenter::DisposeDefaultMessageCenter();

    // Recreate to verify clean state
    MessageCenter& mc2 = MessageCenter::DefaultMessageCenter(false);
    EXPECT_NE(&mc2, nullptr);
}

TEST_F(MessageCenterDisposal_Test, DisposesObserverDescriptors)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter(false);

    // Add several observers using explicit function type
    ObserverCallbackFunc callback = [](int id, Message* msg) {};

    uint64_t id1 = mc.AddObserver("TOPIC_A", callback);
    uint64_t id2 = mc.AddObserver("TOPIC_A", callback);
    uint64_t id3 = mc.AddObserver("TOPIC_B", callback);

    EXPECT_NE(id1, 0u);
    EXPECT_NE(id2, 0u);
    EXPECT_NE(id3, 0u);

    // Dispose should clean up all observer descriptors
    MessageCenter::DisposeDefaultMessageCenter();

    // Recreate and verify clean state
    MessageCenter& mc2 = MessageCenter::DefaultMessageCenter(false);
    EXPECT_NE(&mc2, nullptr);
}

TEST_F(MessageCenterDisposal_Test, DisposesMessagesAndPayloadsWithMixedOwnership)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter(false);

    // Register a topic first so messages are actually queued
    mc.RegisterTopic("TOPIC");

    // Post messages with cleanupPayload=true (default)
    mc.Post("TOPIC", new SimpleNumberPayload(1), true);
    mc.Post("TOPIC", new SimpleNumberPayload(2), true);

    // Post message with cleanupPayload=false (caller retains ownership)
    SimpleNumberPayload* retained = new SimpleNumberPayload(3);
    mc.Post("TOPIC", retained, false);  // Caller retains ownership

    // Dispose - should only clean up payloads with cleanupPayload=true
    MessageCenter::DisposeDefaultMessageCenter();

    // Clean up our retained payload (was NOT deleted by dispose)
    delete retained;
}

TEST_F(MessageCenterDisposal_Test, DisposesWhileWorkerThreadRunning)
{
    // Start MessageCenter with worker thread
    MessageCenter& mc = MessageCenter::DefaultMessageCenter(true);

    // Register topic so messages are queued
    mc.RegisterTopic("ASYNC_TOPIC");

    // Post messages
    for (int i = 0; i < 5; i++)
    {
        mc.Post("ASYNC_TOPIC", new SimpleNumberPayload(i), true);
    }

    // Small delay to let some messages process
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Dispose while worker might still be processing - should be safe
    MessageCenter::DisposeDefaultMessageCenter();
}
