#pragma once

#include <cstdint>

class EmulatorContext;
class MemoryAccessTracker;
class PortTracker;
class CallTraceBuffer;
class TRDOSAnalyzer;

/// @brief Context passed to classifiers containing all data sources.
struct AnalysisContext
{
    EmulatorContext* emulatorContext = nullptr;
    const MemoryAccessTracker* tracker = nullptr;
    const PortTracker* portTracker = nullptr;
    const CallTraceBuffer* callTrace = nullptr;
    const uint8_t* memory = nullptr;
    const TRDOSAnalyzer* trdosAnalyzer = nullptr;
    uint64_t currentFrame = 0;
    uint64_t totalTStates = 0;
};
