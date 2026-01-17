#include "emulatortesthelper.h"
#include "emulator/emulatormanager.h"
#include "emulator/platform.h"
#include "base/featuremanager.h"

Emulator* EmulatorTestHelper::CreateStandardEmulator(
    const std::string& modelName,
    LoggerLevel logLevel)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    
    // Create emulator with model name if specified
    std::shared_ptr<Emulator> emulator;
    if (!modelName.empty()) {
        emulator = manager->CreateEmulatorWithModel("test-emulator", modelName, logLevel);
    } else {
        emulator = manager->CreateEmulator("test-emulator", logLevel);
    }
    
    if (!emulator) {
        return nullptr;
    }
    
    // EmulatorManager already calls Init() during creation
    // Return raw pointer (caller must use CleanupEmulator to properly release)
    return emulator.get();
}

Emulator* EmulatorTestHelper::CreateDebugEmulator(
    const std::vector<std::string>& features,
    const std::string& modelName,
    LoggerLevel logLevel)
{
    // Start with standard emulator
    Emulator* emulator = CreateStandardEmulator(modelName, logLevel);
    if (!emulator) {
        return nullptr;
    }
    
    // Enable debug mode
    emulator->DebugOn();
    
    // Enable requested features
    EmulatorContext* context = emulator->GetContext();
    if (context && context->pFeatureManager) {
        for (const auto& feature : features) {
            if (feature == "breakpoints") {
                context->pFeatureManager->setFeature(Features::kBreakpoints, true);
            } else if (feature == "debugmode") {
                context->pFeatureManager->setFeature(Features::kDebugMode, true);
            }
            // Add more feature mappings as needed
        }
    }
    
    return emulator;
}

void EmulatorTestHelper::CleanupEmulator(Emulator* emulator)
{
    if (emulator) {
        std::string uuid = emulator->GetUUID();
        EmulatorManager* manager = EmulatorManager::GetInstance();
        manager->RemoveEmulator(uuid);
    }
}
