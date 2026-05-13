#include "stdafx.h"
#include "pch.h"

#include "emulator_test.h"

#include "common/modulelogger.h"
#include "common/timehelper.h"
#include "emulator/emulator.h"

/// region <SetUp / TearDown>

void Emulator_Test::SetUp()
{

}

void Emulator_Test::TearDown()
{
}

/// endregion </Setup / TearDown>

/// region <Helper methods>
void Emulator_Test::DestroyEmulator()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
/// endregion </Helper methods>

/// region <Emulator re-entrability tests>
TEST_F(Emulator_Test, MultiInstance)
{
    char message[256];
    constexpr int iterations = 100;

    int successCounter = 0;
    for (int i = 0; i < iterations; i++)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator)
        {
            if (emulator->Init())
            {
                emulator->Stop();
                emulator->Release();

                successCounter++;
            }

            delete emulator;
        }
    }

    if (successCounter != iterations)
    {
        FAIL() << "Iterations made:" << iterations << " successful: " << successCounter << std::endl;
    }
}

TEST_F(Emulator_Test, MultiInstanceRun)
{
    int successCount = 0;
    const int numInstances = 5;

    for (int i = 0; i < numInstances; ++i) {
        std::cout << "Creating emulator instance " << i << std::endl;
        auto emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        
        try {
            std::cout << "Initializing emulator " << i << std::endl;
            if (!emulator->Init()) {
                std::cout << "Failed to initialize emulator " << i << std::endl;
                continue;
            }
            
            std::cout << "Starting emulator " << i << std::endl;
            emulator->StartAsync();  // Use StartAsync instead of Start to avoid blocking
            
            // Give the thread time to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (!emulator->IsRunning()) {
                std::cout << "Emulator " << i << " failed to start" << std::endl;
                continue;
            }
            
            std::cout << "Emulator " << i << " is running" << std::endl;
            
            // Let it run for a short time
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::cout << "Stopping emulator " << i << std::endl;
            emulator->Stop();
            
            // Give it time to stop
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Verify it stopped
            if (emulator->IsRunning()) {
                std::cout << "Emulator " << i << " failed to stop" << std::endl;
                continue;
            }
            
            std::cout << "Emulator " << i << " stopped successfully" << std::endl;
            emulator->Release();  // Clean up resources
            successCount++;
            
        } catch (const std::exception& e) {
            std::cout << "Exception in emulator " << i << ": " << e.what() << std::endl;
        } catch (...) {
            std::cout << "Unknown exception in emulator " << i << std::endl;
        }
    }
    
    std::cout << "Test completed. Success count: " << successCount << std::endl;
    EXPECT_GE(successCount, 3) << "At least 3 instances should run successfully";
}
/// endregion </Emulator re-entrability tests>

