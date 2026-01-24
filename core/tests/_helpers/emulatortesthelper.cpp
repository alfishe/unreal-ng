#include "emulatortesthelper.h"

#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// Static member initialization
std::unordered_map<uint32_t, BreakpointCallback> EmulatorTestHelper::_breakpointCallbacks;

Emulator* EmulatorTestHelper::CreateStandardEmulator(const std::string& modelName, LoggerLevel logLevel)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();

    // Create emulator with model name if specified
    std::shared_ptr<Emulator> emulator;
    if (!modelName.empty())
    {
        emulator = manager->CreateEmulatorWithModel("test-emulator", modelName, logLevel);
    }
    else
    {
        emulator = manager->CreateEmulator("test-emulator", logLevel);
    }

    if (!emulator)
    {
        return nullptr;
    }

    // EmulatorManager already calls Init() during creation
    // Return raw pointer (caller must use CleanupEmulator to properly release)
    return emulator.get();
}

Emulator* EmulatorTestHelper::CreateDebugEmulator(const std::vector<std::string>& features,
                                                  const std::string& modelName, LoggerLevel logLevel)
{
    // Start with standard emulator
    Emulator* emulator = CreateStandardEmulator(modelName, logLevel);
    if (!emulator)
    {
        return nullptr;
    }

    // Enable debug mode
    emulator->DebugOn();

    // Enable requested features
    EmulatorContext* context = emulator->GetContext();
    if (context && context->pFeatureManager)
    {
        for (const auto& feature : features)
        {
            if (feature == "breakpoints")
            {
                context->pFeatureManager->setFeature(Features::kBreakpoints, true);
            }
            else if (feature == "debugmode")
            {
                context->pFeatureManager->setFeature(Features::kDebugMode, true);
            }
            // Add more feature mappings as needed
        }
    }

    return emulator;
}

bool EmulatorTestHelper::EnableDebugFeatures(Emulator* emulator)
{
    if (!emulator)
    {
        return false;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pFeatureManager)
    {
        return false;
    }

    // Enable master debug mode
    emulator->DebugOn();

    // Enable breakpoints and debug mode features
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    return true;
}

uint32_t EmulatorTestHelper::SetupExecutionBreakpoint(Emulator* emulator, uint16_t address, BreakpointCallback callback)
{
    if (!emulator)
    {
        return 0;
    }

    // Ensure debug features are enabled
    EnableDebugFeatures(emulator);

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pDebugManager->GetBreakpointsManager())
    {
        return 0;
    }

    // Create breakpoint via BreakpointManager
    // Use "test_helper" as owner ID for test-created breakpoints
    uint32_t bpId = context->pDebugManager->GetBreakpointsManager()->AddExecutionBreakpoint(address, "test_helper");

    if (bpId != 0)
    {
        // Store callback for this breakpoint
        _breakpointCallbacks[bpId] = std::move(callback);
    }

    return bpId;
}

void EmulatorTestHelper::RemoveBreakpoint(Emulator* emulator, uint32_t breakpointId)
{
    if (!emulator || breakpointId == 0)
    {
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (context && context->pDebugManager->GetBreakpointsManager())
    {
        context->pDebugManager->GetBreakpointsManager()->RemoveBreakpointByID(breakpointId);
    }

    // Remove stored callback
    _breakpointCallbacks.erase(breakpointId);
}

void EmulatorTestHelper::CleanupEmulator(Emulator* emulator)
{
    if (emulator)
    {
        // Clear any breakpoint callbacks for this emulator
        // (In a multi-emulator scenario, we'd need to track which callbacks belong to which emulator)
        _breakpointCallbacks.clear();

        std::string uuid = emulator->GetUUID();
        EmulatorManager* manager = EmulatorManager::GetInstance();
        manager->RemoveEmulator(uuid);
    }
}

void EmulatorTestHelper::OnBreakpointHit(uint32_t bpId, Z80* cpu, Memory* memory)
{
    auto it = _breakpointCallbacks.find(bpId);
    if (it != _breakpointCallbacks.end())
    {
        // Call the registered callback
        bool bypass = it->second(cpu, memory);

        if (bypass)
        {
            // Caller requested bypass - pop return address and set PC
            // This is the common pattern for keyboard wait bypass
            uint16_t retAddr = memory->DirectReadFromZ80Memory(cpu->sp);
            retAddr |= memory->DirectReadFromZ80Memory(cpu->sp + 1) << 8;
            cpu->sp += 2;
            cpu->pc = retAddr;
        }
    }
}
