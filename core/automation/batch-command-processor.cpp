#include "batch-command-processor.h"

#include <algorithm>
#include <chrono>

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

// Batchable commands list
static const std::vector<std::string> BATCHABLE_COMMANDS = {"load-snapshot", "reset",  "pause", "resume",
                                                            "feature",       "create", "start", "stop"};

BatchCommandProcessor::BatchCommandProcessor(EmulatorManager* manager, int threadCount)
    : _manager(manager), _threadCount(threadCount)
{
}

BatchCommandProcessor::~BatchCommandProcessor() {}

bool BatchCommandProcessor::IsBatchable(const std::string& command)
{
    for (const auto& cmd : BATCHABLE_COMMANDS)
    {
        if (cmd == command)
            return true;
    }
    return false;
}

std::vector<std::string> BatchCommandProcessor::GetBatchableCommands()
{
    return BATCHABLE_COMMANDS;
}

BatchResult BatchCommandProcessor::Execute(const std::vector<BatchCommand>& commands)
{
    BatchResult result;
    result.total = static_cast<int>(commands.size());

    if (commands.empty())
    {
        result.success = true;
        return result;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Pre-allocate results
    result.results.resize(commands.size());

    // Parallel execution using thread pool pattern
    std::atomic<int> nextIndex{0};
    std::vector<std::thread> threads;
    threads.reserve(_threadCount);

    for (int t = 0; t < _threadCount; t++)
    {
        threads.emplace_back([this, &commands, &result, &nextIndex]() {
            while (true)
            {
                int idx = nextIndex.fetch_add(1);
                if (idx >= static_cast<int>(commands.size()))
                    break;

                result.results[idx] = ExecuteSingle(commands[idx]);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads)
        t.join();

    // Aggregate results
    for (const auto& r : result.results)
    {
        if (r.success)
            result.succeeded++;
        else
            result.failed++;
    }

    result.success = (result.failed == 0);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.durationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

BatchCommandResult BatchCommandProcessor::ExecuteSingle(const BatchCommand& cmd)
{
    BatchCommandResult result;
    result.emulatorId = cmd.emulatorId;
    result.command = cmd.command;

    // Validate command is batchable
    if (!IsBatchable(cmd.command))
    {
        result.success = false;
        result.error = "Command not batchable: " + cmd.command;
        return result;
    }

    // Get emulator by ID or index
    std::shared_ptr<Emulator> emulator;

    // Try as index first (if numeric)
    bool isNumeric = !cmd.emulatorId.empty() && std::all_of(cmd.emulatorId.begin(), cmd.emulatorId.end(), ::isdigit);
    if (isNumeric)
    {
        int index = std::stoi(cmd.emulatorId);
        emulator = _manager->GetEmulatorByIndex(index);
    }
    else
    {
        emulator = _manager->GetEmulator(cmd.emulatorId);
    }

    if (!emulator)
    {
        result.success = false;
        result.error = "Emulator not found: " + cmd.emulatorId;
        return result;
    }

    // Execute command
    try
    {
        if (cmd.command == "load-snapshot")
        {
            result.success = emulator->LoadSnapshot(cmd.arg1);
            if (!result.success)
                result.error = "Failed to load snapshot";
        }
        else if (cmd.command == "reset")
        {
            emulator->Reset();
            result.success = true;
        }
        else if (cmd.command == "pause")
        {
            emulator->Pause();
            result.success = true;
        }
        else if (cmd.command == "resume")
        {
            emulator->Resume();
            result.success = true;
        }
        else if (cmd.command == "feature")
        {
            auto* fm = emulator->GetFeatureManager();
            if (fm)
            {
                bool value = (cmd.arg2 == "on" || cmd.arg2 == "true" || cmd.arg2 == "1");
                result.success = fm->setFeature(cmd.arg1, value);
                if (!result.success)
                    result.error = "Unknown feature: " + cmd.arg1;
            }
            else
            {
                result.success = false;
                result.error = "FeatureManager not available";
            }
        }
        else
        {
            result.success = false;
            result.error = "Command not implemented: " + cmd.command;
        }
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.error = std::string("Exception: ") + e.what();
    }

    return result;
}
