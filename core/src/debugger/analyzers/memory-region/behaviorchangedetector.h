#pragma once

#include <cstdint>

class EmulatorContext;

/// @brief Detects when program execution phase changes (e.g., loading → running).
/// @note Stub implementation - full implementation deferred to Phase 8.
class BehaviorChangeDetector
{
public:
    explicit BehaviorChangeDetector([[maybe_unused]] EmulatorContext* context) {}
    ~BehaviorChangeDetector() = default;

    // Placeholder API
    bool hasPhaseChanged() const { return false; }
    void onFrameEnd() {}
    void reset() {}
};
