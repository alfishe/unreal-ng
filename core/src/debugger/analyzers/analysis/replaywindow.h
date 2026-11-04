#pragma once

/// @file replaywindow.h
/// @brief One replay of a window of a TTD recording on an analysis instance, with the CPU's trace hooks set: the
/// part every replay consumer shares (docs/emulator/analysis/analysis-layers.md, "Replays").
///
/// The work is TTD's: the clone loads the recording (AnalysisInstance::Make), TimeTravelController::SeekTo restores
/// the machine at the window's start from the nearest keyframe, and under ReplayModeScope the clone runs on the
/// recorded inputs, so the replay is the recorded run, instruction for instruction. ReplayWindow adds the hooks
/// (Z80::m1TraceHook at every instruction start, Z80::busTraceHook at every bus cycle, the TTD edit observer for a
/// tool's writes past the bus such as the fast tape load), runs the clone frame by frame to the window's end and
/// takes the hooks off again whatever happens. A consumer folds what it hears: the L1 build into its model
/// (replaytracer.h), the access trace into a file (accesstrace.h).
///
/// The source instance is never touched: the hooks live on the clone only, so its hot path never sees a tracer.
///
/// @code
///   analysis::ReplayWindow window;
///   if (!window.Open(bundle, "analysis-trace", from, error))      // clone + seek
///       return false;
///   analysis::ReplayHooks hooks;
///   hooks.instruction = [&](uint16_t pc) { ... };                  // window.Cpu() holds the registers
///   hooks.bus = [&](char type, uint16_t address, uint8_t value) { ... };
///   hooks.chunk = [&](uint64_t frame) { return true; };            // false stops (a cancel)
///   const auto run = window.RunTo(endFrame, 25, hooks);            // Finished / Stopped / Stalled
/// @endcode

#include <cstdint>
#include <functional>
#include <string>

#include "debugger/analyzers/analysis/analysisinstance.h"
#include "debugger/ttd/ttdcheckpoint.h"

class Memory;
class Z80;
class EmulatorContext;
struct EmulatorState;

namespace ttd
{
class TimeTravelController;
}

namespace analysis
{

struct TraceBundle;

/// What a replay calls. Every member is optional
struct ReplayHooks
{
    /// An instruction starts at @p pc (the M1 cycle): the CPU's registers are the instruction's inputs, the
    /// interrupt it may be the first of is already accepted (Z80::interruptsAccepted moved)
    std::function<void(uint16_t pc)> instruction;
    /// A bus cycle of the instruction that started last: 'R' a read (the opcode and operand fetches included),
    /// 'W' a write, 'I' / 'O' a port read / write, 'N' an idle cycle
    std::function<void(char type, uint16_t address, uint8_t value)> bus;
    /// A write past the bus that the recording holds (a tool edit: the fast tape load puts a whole block at the
    /// ROM loader's entry): the bytes the replaced instructions would have written
    std::function<void(uint16_t address, uint8_t value)> editWrite;
    /// After each chunk of frames, at a frame boundary (@p frame is the frame now starting). False stops the run
    std::function<bool(uint64_t frame)> chunk;
};

class ReplayWindow
{
public:
    enum class Run
    {
        Finished,  ///< the end frame was reached
        Stopped,   ///< ReplayHooks::chunk said stop, or Stop() was called
        Stalled,   ///< the clone stopped advancing (a halted engine, a model that cannot run headless)
    };

    /// Make the analysis instance @p name from @p bundle and seek it to @p from (clamped to the first keyframe:
    /// nothing exists before it). A start at or past the recording's end is not sought: Start() says where it is
    /// and the consumer refuses its empty window in its own words.
    /// False with @p error when the clone cannot be made, the recording holds no keyframe or the seek fails
    bool Open(const TraceBundle& bundle, const char* name, ttd::TTDTimePoint from, std::string& error);

    /// The first keyframe and the end of the recording (valid after Open)
    ttd::TTDTimePoint First() const { return _first; }
    ttd::TTDTimePoint SessionEnd() const { return _end; }
    /// Where the seek put the clone: the window's start
    ttd::TTDTimePoint Start() const { return _start; }

    /// Run from the window's start until frame @p endFrame begins (exclusive), @p chunkFrames frames between calls
    /// of ReplayHooks::chunk, with @p hooks set for the run only
    Run RunTo(uint64_t endFrame, uint32_t chunkFrames, const ReplayHooks& hooks);

    /// Ask the run to stop at the next frame boundary (from a hook: the consumer has all it needs)
    void Stop() { _stop = true; }

    EmulatorContext* Context() const { return _context; }
    Z80* Cpu() const { return _z80; }
    Memory* Mem() const { return _memory; }
    EmulatorState& State() const;
    ttd::TimeTravelController* Ttd() const { return _ttd; }
    /// CPU T-states in one frame of the clone's machine
    uint32_t FrameT() const { return _frameT; }

private:
    AnalysisInstance _clone;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    ttd::TTDTimePoint _first{};
    ttd::TTDTimePoint _end{};
    ttd::TTDTimePoint _start{};
    uint32_t _frameT = 0;
    bool _stop = false;
};

}  // namespace analysis
