#include <3rdparty/message-center/eventqueue.h>
#include <3rdparty/message-center/messagecenter.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "pch.h"
#include "stdafx.h"

class EmulatorManager_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        // Clean up any existing emulators before each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        // Clean up after each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }
};

TEST_F(EmulatorManager_Test, CreateEmulator)
{
    // Test creating a basic emulator with default parameters
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);

    // Verify the emulator has a valid ID
    std::string emulatorId = emulator->GetUUID();
    ASSERT_FALSE(emulatorId.empty());

    // Verify the emulator can be retrieved
    auto retrieved = _manager->GetEmulator(emulatorId);
    ASSERT_NE(retrieved, nullptr);
    ASSERT_EQ(retrieved->GetUUID(), emulatorId);
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithId)
{
    std::string symbolicId = "test-symbolic-id";

    // Test creating an emulator with a symbolic ID
    auto emulator = _manager->CreateEmulator(symbolicId);
    ASSERT_NE(emulator, nullptr);

    // Get the dynamically generated UUID
    std::string emulatorId = emulator->GetUUID();

    // The emulator should be retrievable using its generated ID
    auto retrieved = _manager->GetEmulator(emulatorId);
    ASSERT_NE(retrieved, nullptr);
    ASSERT_EQ(retrieved->GetUUID(), emulatorId);

    // Verify the symbolic ID was set correctly
    ASSERT_EQ(emulator->GetSymbolicId(), symbolicId);
}

/// @brief Tests the full lifecycle of an emulator instance: create, start, pause, resume, stop, remove.
///
/// DESIGN NOTES:
/// 1. Uses EXPECT_* instead of ASSERT_* for most checks to ensure test cleanup always runs.
///    ASSERT_* causes immediate test exit on failure, which can leave resources dangling.
///
/// 2. AVOIDS MessageCenter observers with local captures. The previous implementation used:
///    ```
///    std::mutex mtx;  // Local variable
///    auto callback = [&mtx](...) { ... };  // Captures local by reference
///    messageCenter.AddObserver(..., callback);
///    ```
///    This is DANGEROUS because:
///    - If ASSERT_* fails, the test exits but the observer remains registered
///    - The callback still holds references to destroyed local variables (mtx, cv)
///    - When the next test runs and emits state changes, the dangling callback
///      tries to lock the destroyed mutex, causing "mutex lock failed: Invalid argument"
///
/// 3. Uses simple sleep-based synchronization instead of condition variables.
///    While less precise, this approach is robust and doesn't require observer cleanup.
///
/// 4. Wraps conditional test sections in if-blocks rather than using ASSERT for early exit.
///    This ensures cleanup code (Stop, RemoveEmulator) always executes.
TEST_F(EmulatorManager_Test, EmulatorInstanceLifecycle)
{
    // Test creating an emulator with default parameters
    auto emulator = _manager->CreateEmulator("test-emulator");
    ASSERT_NE(emulator, nullptr);

    // Get the generated ID
    std::string emulatorId = emulator->GetUUID();
    ASSERT_FALSE(emulatorId.empty());

    // Verify the emulator is in the correct initial state
    EXPECT_EQ(emulator->GetState(), StateInitialized);

    // Start the emulator asynchronously using the built-in method
    emulator->StartAsync();

    // Give the emulator time to start (simple synchronization - see design notes above)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify the emulator transitioned to a running state
    int currentState = emulator->GetState();
    std::cout << "Emulator state after StartAsync: " << currentState << std::endl;

    // The emulator should be in StateRun (2) after starting
    bool startedOk = (currentState == StateRun);
    EXPECT_TRUE(startedOk) << "Emulator did not enter RUN state. Current state: " << currentState;

    if (startedOk)
    {
        // Verify we can get the emulator context
        auto context = emulator->GetContext();
        EXPECT_NE(context, nullptr);

        // Pause the emulator
        emulator->Pause();

        // Give time for pause to take effect
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        EXPECT_TRUE(emulator->IsPaused()) << "Emulator should be paused";

        // Resume the emulator
        emulator->Resume();

        // Give time for resume to take effect
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        EXPECT_FALSE(emulator->IsPaused()) << "Emulator should not be paused after resume";
    }

    // Stop the emulator
    emulator->Stop();

    // Give time for stop
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify the emulator is now stopped
    int stopState = emulator->GetState();
    std::cout << "Emulator state after stop: " << stopState << std::endl;
    EXPECT_NE(stopState, StateRun) << "Emulator did not stop";

    // Clean up by removing the emulator
    bool removed = _manager->RemoveEmulator(emulatorId);
    EXPECT_TRUE(removed);

    // Verify the emulator is no longer accessible
    EXPECT_FALSE(_manager->HasEmulator(emulatorId));
    EXPECT_EQ(_manager->GetEmulator(emulatorId), nullptr);
}

TEST_F(EmulatorManager_Test, RemoveEmulator)
{
    // Create a test emulator
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);
    std::string emulatorId = emulator->GetUUID();

    // Verify it exists
    ASSERT_TRUE(_manager->HasEmulator(emulatorId));

    // Remove it
    bool removed = _manager->RemoveEmulator(emulatorId);
    ASSERT_TRUE(removed);

    // Verify it no longer exists
    ASSERT_FALSE(_manager->HasEmulator(emulatorId));
    ASSERT_EQ(_manager->GetEmulator(emulatorId), nullptr);
}

TEST_F(EmulatorManager_Test, GetEmulatorIds)
{
    // Create multiple emulators and collect their IDs
    std::vector<std::string> createdIds;
    std::vector<std::string> symbolicIds = {"test1", "test2", "test3"};

    for (const auto& symbolicId : symbolicIds)
    {
        auto emulator = _manager->CreateEmulator(symbolicId);
        ASSERT_NE(emulator, nullptr);
        createdIds.push_back(emulator->GetUUID());
    }

    // Get all emulator IDs
    auto emulatorIds = _manager->GetEmulatorIds();

    // Verify we got the correct number of IDs
    ASSERT_EQ(emulatorIds.size(), createdIds.size());

    // Verify all created IDs are present in the returned list
    for (const auto& id : createdIds)
    {
        auto it = std::find(emulatorIds.begin(), emulatorIds.end(), id);
        ASSERT_NE(it, emulatorIds.end()) << "Emulator ID " << id << " not found in emulator IDs";
    }
}

TEST_F(EmulatorManager_Test, GetAllEmulatorStatuses)
{
    // Create test emulators
    auto emulator1 = _manager->CreateEmulator("test1");
    auto emulator2 = _manager->CreateEmulator("test2");

    // Get the generated IDs
    std::string id1 = emulator1->GetUUID();
    std::string id2 = emulator2->GetUUID();

    // Get all statuses
    auto statuses = _manager->GetAllEmulatorStatuses();

    // Verify we have status for both emulators
    ASSERT_EQ(statuses.size(), 2);

    // Verify both emulators are in the status map
    ASSERT_NE(statuses.find(id1), statuses.end());
    ASSERT_NE(statuses.find(id2), statuses.end());

    // Verify default state is correct (should be StateInitialized after creation)
    ASSERT_EQ(statuses[id1], StateInitialized);
    ASSERT_EQ(statuses[id2], StateInitialized);
}

TEST_F(EmulatorManager_Test, FindEmulatorsBySymbolicId)
{
    // Create emulators with different symbolic IDs
    auto emulator1 = _manager->CreateEmulator("test1");
    auto emulator2 = _manager->CreateEmulator("test2");
    auto emulator3 = _manager->CreateEmulator("test3");

    // Get the generated IDs
    std::string id1 = emulator1->GetUUID();
    std::string id2 = emulator2->GetUUID();
    std::string id3 = emulator3->GetUUID();

    // Find emulators by symbolic ID - returns vector of emulator pointers
    auto test1Emulators = _manager->FindEmulatorsBySymbolicId("test1");
    auto test2Emulators = _manager->FindEmulatorsBySymbolicId("test2");
    auto test3Emulators = _manager->FindEmulatorsBySymbolicId("test3");
    auto nonexistentEmulators = _manager->FindEmulatorsBySymbolicId("nonexistent");

    // Verify results - each emulator has a unique symbolic ID, so we expect 1 for each
    ASSERT_EQ(test1Emulators.size(), 1);
    ASSERT_EQ(test2Emulators.size(), 1);
    ASSERT_EQ(test3Emulators.size(), 1);
    ASSERT_TRUE(nonexistentEmulators.empty());

    // Verify the correct emulators are returned for each symbolic ID
    ASSERT_EQ(test1Emulators[0]->GetUUID(), id1);
    ASSERT_EQ(test2Emulators[0]->GetUUID(), id2);
    ASSERT_EQ(test3Emulators[0]->GetUUID(), id3);
}

TEST_F(EmulatorManager_Test, GetEmulatorNonExistent)
{
    // Try to get a non-existent emulator
    auto emulator = _manager->GetEmulator("non-existent-id");
    ASSERT_EQ(emulator, nullptr);
}

TEST_F(EmulatorManager_Test, RemoveNonExistentEmulator)
{
    // Try to remove a non-existent emulator
    bool removed = _manager->RemoveEmulator("non-existent-id");
    ASSERT_FALSE(removed);
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithDuplicateId)
{
    // Create first emulator
    auto emulator1 = _manager->CreateEmulator("test-emulator");
    ASSERT_NE(emulator1, nullptr);
    std::string emulator1Id = emulator1->GetUUID();

    // Try to create another emulator with the same symbolic ID
    auto emulator2 = _manager->CreateEmulator("test-emulator");

    // The second creation should succeed since we're not using CreateEmulatorWithId
    ASSERT_NE(emulator2, nullptr);

    // The first emulator should still be accessible using its ID
    auto retrieved1 = _manager->GetEmulator(emulator1Id);
    ASSERT_NE(retrieved1, nullptr);
    ASSERT_EQ(retrieved1->GetUUID(), emulator1Id);

    // The second emulator should have a different ID
    std::string emulator2Id = emulator2->GetUUID();
    ASSERT_NE(emulator1Id, emulator2Id);

    // The second emulator should be accessible using its ID
    auto retrieved2 = _manager->GetEmulator(emulator2Id);
    ASSERT_NE(retrieved2, nullptr);
    ASSERT_EQ(retrieved2->GetUUID(), emulator2Id);
}
