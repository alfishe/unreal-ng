#pragma once

/// @file accesstrace.h
/// @brief The CPU access trace of a window of a recording (docs/emulator/analysis/analysis-layers.md, "Access
/// trace"): the window is replayed on a muted analysis instance, as an L1 build is (replaytracer.h), and every
/// instruction start (its registers, frame and T-state) and every bus cycle that follows it (an opcode or operand
/// fetch, a memory read or write, a port read or write) is written to a file, with the memory the window starts
/// from. A routine animation, a "which instruction drew this pixel" map or a beam timeline is drawn from the file
/// alone: no memory_at per picture, no seek.
///
/// The file (little-endian):
///
///     header, 64 bytes:
///       char magic[4] "UATR"; u16 version (1); u16 header bytes (64)
///       u64 from frame; u32 from t (TTD units); u32 TTD units per CPU T-state
///       u64 to frame;   u32 to t (TTD units);   u32 CPU T-states per frame
///       u64 instructions; u64 cycles; u8 truncated; 7 bytes zero
///     u16 page[4]       the page mapped in each 16 K bank at the start
///     u8 memory[65536]  the Z80 view at the start
///     records, each starting with its tag:
///       'X' instruction: u32 frame, u32 t (TTD units in the frame), u16 pc, sp, af, bc, de, hl, ix, iy, u8 flags
///                        (1: the first instruction of an interrupt, 2: halted)            26 bytes
///       'F' fetch, 'R' read, 'W' write, 'I' port in, 'O' port out, 'E' a tool edit's write (the fast tape load):
///                        u16 address (port), u8 value, u16 CPU T-states since the instruction's start   6 bytes
///
/// A cycle belongs to the instruction record before it. The fetches are the instruction's own bytes (its length,
/// prefixes and displacement included); the interrupt's pushes and vector reads come before the handler's first
/// instruction, under the instruction they interrupted.
///
/// @code
///   analysis::AccessTraceOptions options;
///   options.fromFrame = 1001; options.toFrame = 1003; options.path = "scratch/pass.uatr";
///   analysis::AccessTraceSummary summary;
///   if (analysis::ReplayAccessTrace(bundle, options, summary, error)) ...;   // any thread, the clone only
/// @endcode

#include <cstdint>
#include <string>

namespace analysis
{

struct TraceBundle;

struct AccessTraceOptions
{
    uint64_t fromFrame = 0;     ///< the window's start (0, 0: the first keyframe)
    uint32_t fromTInFrame = 0;  ///< TTD units
    uint64_t toFrame = 0;       ///< the window's end (exclusive); 0 with toTInFrame 0: fromFrame + 1
    uint32_t toTInFrame = 0;
    uint64_t maxCycles = 8000000;  ///< the trace stops (truncated) after this many bus cycles
    std::string path;           ///< the file to write
};

struct AccessTraceSummary
{
    uint64_t fromFrame = 0;
    uint32_t fromTInFrame = 0;
    uint64_t toFrame = 0;
    uint32_t toTInFrame = 0;
    uint64_t instructions = 0;
    uint64_t cycles = 0;
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t fetches = 0;
    uint64_t ports = 0;
    uint64_t bytes = 0;  ///< the file's size
    bool truncated = false;
};

/// The most frames one trace may span (a pass of a game is a few; a level set-up some hundred)
constexpr uint64_t kAccessTraceMaxFrames = 3000;

/// Replay @p bundle's window on a fresh analysis instance and write its access trace to options.path. False with
/// @p error when the window is outside the recording or empty, the clone cannot be made or the file not written
bool ReplayAccessTrace(const TraceBundle& bundle, const AccessTraceOptions& options, AccessTraceSummary& out,
                       std::string& error);

}  // namespace analysis
