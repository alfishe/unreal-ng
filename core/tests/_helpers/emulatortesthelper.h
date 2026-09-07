#pragma once

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/spectrumconstants.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class Z80;
class AnalyzerManager;
class Memory;

/// Breakpoint callback function type
/// @param cpu Z80 CPU state at breakpoint
/// @param memory Memory access for stack/value reads
/// @return true to bypass the waiting routine (pop return and skip), false to continue normally
using BreakpointCallback = std::function<bool(Z80* cpu, Memory* memory)>;

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

    /// Enable debug features on an existing emulator (debugmode + breakpoints)
    /// @param emulator Target emulator
    /// @return true if features enabled successfully
    static bool EnableDebugFeatures(Emulator* emulator);

    /// Setup an execution breakpoint with a callback
    /// Automatically enables debug features if not already enabled
    /// @param emulator Target emulator
    /// @param address Breakpoint address (e.g., 0x1052 for TR-DOS keyboard wait)
    /// @param callback Function to call when breakpoint hits
    /// @return Breakpoint ID (0 = failure)
    static uint32_t SetupExecutionBreakpoint(
        Emulator* emulator,
        uint16_t address,
        BreakpointCallback callback);

    /// Remove a previously set breakpoint
    /// @param emulator Target emulator
    /// @param breakpointId ID returned from SetupExecutionBreakpoint
    static void RemoveBreakpoint(Emulator* emulator, uint32_t breakpointId);

    /// Cleanup and release emulator instance
    /// Also removes all test breakpoints
    /// @param emulator Emulator to cleanup
    static void CleanupEmulator(Emulator* emulator);

    /// Run N frames as fast as possible (synchronous, no throttling)
    static void RunFramesFast(Emulator* emulator, int frameCount);

    /// Run frames until condition is met or maxFrames reached
    /// @return Number of frames actually run
    static int RunUntil(Emulator* emulator,
                        std::function<bool()> condition,
                        int maxFrames,
                        int checkInterval = 10);

    /// Check if BASIC is ready (ERR_NR == 0x00, meaning error code 1 = "OK")
    static bool IsBASICReady(Emulator* emulator);

    /// Run until BASIC is ready (no error state)
    /// @param framesRun Optional output: actual frames run before ready (for quality checks)
    static bool RunUntilBASICReady(Emulator* emulator, int maxFrames, int* framesRun = nullptr);

    /// Read system variable from memory
    static uint8_t ReadSysVar(Emulator* emulator, uint16_t address);

    /// Read 16-bit system variable
    static uint16_t ReadSysVar16(Emulator* emulator, uint16_t address);

private:
    /// Stored callbacks for test breakpoints (keyed by breakpoint ID)
    static std::unordered_map<uint32_t, BreakpointCallback> _breakpointCallbacks;

    /// Internal handler dispatched from AnalyzerManager
    static void OnBreakpointHit(uint32_t bpId, Z80* cpu, Memory* memory);
};
