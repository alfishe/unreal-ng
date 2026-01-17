#pragma once

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include <string>
#include <vector>

/// Helper class for setting up emulator instances in unit tests
class EmulatorTestHelper
{
public:
    /// Create a standard emulator instance with debug features off
    /// @param modelName Model short name (e.g., "PENTAGON", "48K", "128K")
    /// @param logLevel Logging level
    /// @return Initialized emulator instance (caller owns)
    static Emulator* CreateStandardEmulator(
        const std::string& modelName = "PENTAGON",
        LoggerLevel logLevel = LoggerLevel::LogError);
    
    /// Create a debug-enabled emulator with specific features enabled
    /// @param features List of feature names to enable (e.g., "breakpoints", "analyzers")
    /// @param modelName Model short name
    /// @param logLevel Logging level
    /// @return Initialized emulator instance with debug mode on (caller owns)
    static Emulator* CreateDebugEmulator(
        const std::vector<std::string>& features = {},
        const std::string& modelName = "PENTAGON",
        LoggerLevel logLevel = LoggerLevel::LogError);
    
    /// Cleanup and release emulator instance
    /// @param emulator Emulator to cleanup
    static void CleanupEmulator(Emulator* emulator);
};
