#include "debugger/analyzers/analysis/replaywindow.h"

#include <algorithm>

#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace analysis
{

namespace
{

bool Before(const ttd::TTDTimePoint& a, const ttd::TTDTimePoint& b)
{
    return a.frame < b.frame || (a.frame == b.frame && a.tInFrame < b.tInFrame);
}

/// The hooks of one run, set on construction and taken off on destruction: an early return, a stop or a stall
/// never leaves a hook pointing at a consumer's dead locals
class HookScope
{
public:
    HookScope(Z80& z80, ttd::TimeTravelController& ttd, const ReplayHooks& hooks) : _z80(z80), _ttd(ttd)
    {
        if (hooks.instruction)
            _z80.m1TraceHook = hooks.instruction;
        if (hooks.bus)
            _z80.busTraceHook = hooks.bus;
        if (hooks.editWrite)
            _ttd.SetEditWriteObserver(hooks.editWrite);
    }
    ~HookScope()
    {
        _z80.m1TraceHook = nullptr;
        _z80.busTraceHook = nullptr;
        _ttd.SetEditWriteObserver(nullptr);
    }
    HookScope(const HookScope&) = delete;
    HookScope& operator=(const HookScope&) = delete;

private:
    Z80& _z80;
    ttd::TimeTravelController& _ttd;
};

}  // namespace

EmulatorState& ReplayWindow::State() const
{
    return _context->emulatorState;
}

bool ReplayWindow::Open(const TraceBundle& bundle, const char* name, ttd::TTDTimePoint from, std::string& error)
{
    if (!bundle.session || !bundle.config)
    {
        error = "the trace bundle holds no recording";
        return false;
    }
    // The clone: a muted, headless instance of the source's model with the recording loaded into its TTD
    if (!_clone.Make(bundle, name, true, error))
        return false;
    _context = _clone.Context();
    _ttd = _context->pTimeTravelController;
    _z80 = _context->pCore->GetZ80();
    _memory = _context->pMemory;
    _frameT = _context->emulatorState.BaseToCpuT(_context->config.frame);
    if (_ttd->GetCheckpointCount() == 0)
    {
        error = "the recording has no keyframe";
        return false;
    }
    _first = _ttd->GetCheckpoint(0)->time;
    _end = _ttd->SessionEndPosition();
    // Nothing exists before the first keyframe: a window starting earlier starts there
    _start = Before(from, _first) ? _first : from;
    // A start at or past the recording's end has nothing to replay: no seek, the consumer reports its empty window
    if (!Before(_start, _end))
        return true;
    // TTD restores the machine from the nearest keyframe at or before the moment and replays up to it
    if (!_ttd->SeekTo(_start, nullptr))
    {
        error = "the analysis instance could not seek to frame " + std::to_string(_start.frame) + " t " +
                std::to_string(_start.tInFrame);
        return false;
    }
    return true;
}

ReplayWindow::Run ReplayWindow::RunTo(uint64_t endFrame, uint32_t chunkFrames, const ReplayHooks& hooks)
{
    _stop = false;
    const uint32_t chunk = std::max<uint32_t>(1, chunkFrames);
    EmulatorState& state = _context->emulatorState;
    // Replay mode: the clone plays the recorded inputs (keys, tape, the edits) instead of recording new ones
    ttd::TimeTravelController::ReplayModeScope replay(*_ttd);
    HookScope scope(*_z80, *_ttd, hooks);
    while (state.frame_counter < endFrame)
    {
        const uint64_t target = std::min<uint64_t>(endFrame, state.frame_counter + chunk);
        while (state.frame_counter < target)
        {
            // Run to the target frame's start: whole frames less what the current frame has already run. The
            // emulator may stop short (a frame boundary inside RunTStates); the loop runs the rest
            const uint64_t run = (target - state.frame_counter) * _frameT;
            const uint64_t t = _z80->t;
            const uint64_t before = state.t_states + t;
            _clone.GetEmulator()->RunTStates(run > t ? run - t : 1, /*skipBreakpoints=*/true);
            if (state.t_states + _z80->t == before)
                return Run::Stalled;
            if (_stop)
                return Run::Stopped;
        }
        if (hooks.chunk && !hooks.chunk(state.frame_counter))
            return Run::Stopped;
        if (_stop)
            return Run::Stopped;
    }
    return Run::Finished;
}

}  // namespace analysis
