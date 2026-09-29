#pragma once

/// @file loaderszx.h
/// @brief The SZX snapshot loader and writer (design §6-§9), the same public
/// shape as LoaderSNA / LoaderZ80: load() and save() on a path, plus
/// Commit() / Capture() on a parsed stage for callers that hold one.
///
/// Phase S1-S2 scope: header, CRTR, Z80R (with the exact frame position,
/// MEMPTR, Q, HALT, the interrupt shadow), SPCR, RAMP, AY, and the B128
/// "TR-DOS paged" flag. Other blocks are listed in the report as ignored or
/// unknown. The snapshot's machine must be the running one (the model switch
/// comes with the load orchestrator, design §6).
///
/// Frame position: SZX counts dwCyclesStart from the INT; our Z80::t counts
/// from the frame start and the INT is first seen at intstart + 1. Worked
/// example (Pentagon, frame 71680, intstart 71635): dwCyclesStart 100 loads
/// as t = (71636 + 100) mod 71680 = 56.

#include <string>

#include "loaders/snapshot/szx/szxformat.h"

class EmulatorContext;

class LoaderSZX
{
public:
    LoaderSZX(EmulatorContext* context, const std::string& path);

    bool load();
    bool save();

    const szx::Report& GetReport() const { return _report; }
    const std::string& GetError() const { return _error; }

    /// Apply a parsed stage to the running machine
    static bool Commit(EmulatorContext* context, const szx::Stage& stage, szx::Report& report, std::string& error);
    /// The running machine as a stage
    static bool Capture(EmulatorContext* context, szx::Stage& stage, std::string& error);

    /// Our frame position for a count from the INT, and back
    static uint32_t FramePositionFromIntCount(EmulatorContext* context, uint32_t cyclesFromInt);
    static uint32_t IntCountFromFramePosition(EmulatorContext* context, uint32_t framePosition);

private:
    static void ApplyPaging(EmulatorContext* context, const szx::Stage& stage, szx::Report& report);
    static void ApplyCpu(EmulatorContext* context, const szx::Stage& stage, szx::Report& report);

    EmulatorContext* _context;
    std::string _path;
    szx::Report _report;
    std::string _error;
};
