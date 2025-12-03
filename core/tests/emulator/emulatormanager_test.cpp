#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <3rdparty/message-center/messagecenter.h>
#include <3rdparty/message-center/eventqueue.h>
#include <emulator/platform.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

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

TEST_F(EmulatorManager_Test, EmulatorInstanceLifecycle)
{
    // Test creating an emulator with default parameters
    auto emulator = _manager->CreateEmulator("test-emulator");
    ASSERT_NE(emulator, nullptr);
    
    // Get the generated ID
    std::string emulatorId = emulator->GetUUID();
    ASSERT_FALSE(emulatorId.empty());
    
    // Verify the emulator is in the correct initial state
    ASSERT_EQ(emulator->GetState(), StateInitialized);
    
    // Get the message center instance
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    
    // Flag to track if we received the start message
    std::atomic<bool> startMessageReceived(false);
    std::mutex mtx;
    std::condition_variable cv;
    
    // Track the state we saw in the message and whether we've seen RUN state
    std::atomic<int> lastSeenState{StateInitialized};
    std::atomic<bool> isInRunState{false};
    
    // Create a callback function for the message
    auto messageCallback = [&](int id, Message* message) {
        if (message && message->obj)
        {
            SimpleNumberPayload* payload = static_cast<SimpleNumberPayload*>(message->obj);
            std::cout << "Received state change message. New state: " << payload->_payloadNumber << std::endl;
            
            // Update the last seen state
            int newState = payload->_payloadNumber;
            lastSeenState = newState;
            
            // If we see the RUN state, verify the emulator is in RUN state and notify
            if (newState == StateRun)
            {
                std::cout << "Detected RUN state transition" << std::endl;
                // Verify the emulator is in RUN state right after the transition
                int currentState = emulator->GetState();
                std::cout << "Verifying emulator state is RUN. Current state: " << currentState << std::endl;
                isInRunState = (currentState == StateRun);
                
                std::unique_lock<std::mutex> lock(mtx);
                startMessageReceived = true;
                cv.notify_one();
            }
        }
    };
    
    // Subscribe to state change messages
    std::cout << "Subscribing to state change messages" << std::endl;
    messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, messageCallback);
    
    // Start the emulator asynchronously
    std::cout << "Starting emulator asynchronously" << std::endl;
    std::thread emulatorThread([&emulator]() {
        try {
            std::cout << "Emulator thread starting..." << std::endl;
            emulator->StartAsync();
            std::cout << "Emulator StartAsync() completed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Exception in emulator thread: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in emulator thread" << std::endl;
        }
    });
    emulatorThread.detach();
    
    // Wait for the start message with a timeout
    std::cout << "Waiting for state change to RUN (timeout: 500ms)" << std::endl;
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool status = cv.wait_for(lock, std::chrono::milliseconds(500), [&] { 
            std::cout << "Waiting... Current state: " << emulator->GetState() << std::endl;
            return startMessageReceived.load(); 
        });
        
        // Clean up the observer
        std::cout << "Cleaning up observer" << std::endl;
        messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, messageCallback);
        
        // Check if we successfully detected the RUN state transition
        std::cout << "Wait completed. Status: " << (status ? "success" : "timeout") << std::endl;
        std::cout << "Last seen state: " << lastSeenState 
                  << ", Current state: " << emulator->GetState() 
                  << ", Is in run state: " << (isInRunState ? "true" : "false") << std::endl;
        
        // Verify we saw the RUN state at the right time
        ASSERT_TRUE(isInRunState) << "Emulator did not enter RUN state when expected. "
                                << "Last seen state: " << lastSeenState 
                                << ", Current state: " << emulator->GetState();
    }
    
    // Additional verification that the emulator is in a valid state
    // (It might have transitioned past RUN state by now, which is fine)
    ASSERT_NE(emulator->GetState(), StateInitialized) << "Emulator did not start properly";
    
    // Verify we can get the emulator context
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    
    // Check if the emulator is in a valid running state
    // It might be in StateRun (2) or another valid running state
    int currentState = emulator->GetState();
    std::cout << "Emulator current state after start: " << currentState << std::endl;
    ASSERT_NE(currentState, StateInitialized) << "Emulator did not start properly";
    
    // Only try to pause if the emulator is in a running state
    if (currentState == StateRun) {
        // Pause the emulator
        emulator->Pause();
        ASSERT_EQ(emulator->GetState(), StatePaused) << "Failed to pause emulator";
        ASSERT_TRUE(emulator->IsPaused());
        
        // Resume the emulator
        emulator->Resume();
        
        // After resume, the emulator could be in StateRun (2) or StateResumed (3)
        int resumedState = emulator->GetState();
        std::cout << "Emulator state after resume: " << resumedState << std::endl;
        bool isInValidRunningState = (resumedState == StateRun || resumedState == 3); // 3 is StateResumed
        ASSERT_TRUE(isInValidRunningState) << "Failed to resume emulator. State: " << resumedState;
        ASSERT_FALSE(emulator->IsPaused()) << "Emulator should not be paused after resume";
    } else {
        std::cout << "Skipping pause/resume test - emulator not in running state" << std::endl;
    }
    
    // Stop the emulator
    emulator->Stop();
    
    // Verify the emulator is now stopped or stopping
    int stopState = emulator->GetState();
    std::cout << "Emulator state after stop: " << stopState << std::endl;
    ASSERT_NE(stopState, StateRun) << "Emulator did not stop";
    
    // Clean up by removing the emulator
    bool removed = _manager->RemoveEmulator(emulatorId);
    ASSERT_TRUE(removed);
    
    // Verify the emulator is no longer accessible
    ASSERT_FALSE(_manager->HasEmulator(emulatorId));
    ASSERT_EQ(_manager->GetEmulator(emulatorId), nullptr);
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
