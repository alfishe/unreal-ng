#pragma once

#include <cstdint>
#include <string>

// Forward declarations
class AnalyzerManager;
class Z80;

/// <summary>
/// Base interface for all analyzers.
/// Analyzers can subscribe to events via AnalyzerManager during onActivate().
/// </summary>
class IAnalyzer
{
public:
    virtual ~IAnalyzer() = default;

    /// <summary>
    /// Called when analyzer is activated.
    /// Use this to subscribe to events and request breakpoints.
    /// </summary>
    /// <param name="mgr">AnalyzerManager for subscribing to events</param>
    virtual void onActivate(AnalyzerManager* mgr) = 0;

    /// <summary>
    /// Called when analyzer is deactivated.
    /// All subscriptions and breakpoints are automatically cleaned up.
    /// </summary>
    virtual void onDeactivate() = 0;

    // Cold path events (virtual dispatch is acceptable for <1K events/sec)

    /// <summary>
    /// Called at the start of each video frame (50/sec)
    /// </summary>
    virtual void onFrameStart() {}

    /// <summary>
    /// Called at the end of each video frame (50/sec)
    /// </summary>
    virtual void onFrameEnd() {}

    /// <summary>
    /// Called when a breakpoint owned by this analyzer is hit
    /// @param address Breakpoint address
    /// @param cpu CPU state at breakpoint
    virtual void onBreakpointHit(uint16_t address, Z80* cpu)
    {
        (void)address;
        (void)cpu;
    }

    // Metadata

    /// <summary>
    /// Get the analyzer's unique name
    /// </summary>
    virtual std::string getName() const = 0;

    /// <summary>
    /// Get the analyzer's unique identifier (UUID)
    /// </summary>
    virtual std::string getUUID() const = 0;

public:
    /// <summary>
    /// Pointer to manager, set during registration
    /// </summary>
    AnalyzerManager* _manager = nullptr;
    
    /// <summary>
    /// Registration ID, set during registration. Use this for breakpoint ownership.
    /// </summary>
    std::string _registrationId;
};
