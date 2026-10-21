#pragma once

/// @file runpacer.h
/// @brief The paced run of one instance (Stream Deck use case D1, the debugger's speed control): the machine runs a
/// step of a unit (a frame, N scanlines, an instruction) per interval, so a person sees it move at 25 / 5 / 1 fps,
/// line by line or instruction by instruction. The pace ladder is the Qt speed control's slider; the Qt widget, the
/// actions (run.pace_*), the WebAPI (/run_pace), MCP, the CLI (`pace`), Lua and Python are all clients of this one
/// pacer per instance.
///
/// - **The ladder** (Presets): `full` (the machine runs at its speed: no pacing), `25fps`, `5fps`, `1fps` (a frame
///   per 40 / 200 / 1000 ms), `frame`, `100lines`, `10lines`, `1line`, `1op` (one unit per the adjustable interval,
///   1 .. 5000 ms).
/// - **A run** steps on its own thread, which lives only while the pace runs (never the emulation thread, never a GUI
///   or network thread). Each tick is a direct run with breakpoints honoured: a breakpoint ends the paced run, the
///   machine stays paused there (`stop_reason` "breakpoint"). It also ends when someone resumes the machine
///   ("resumed"), another surface holds run control ("held"), the user takes control back ("revoked") or the instance
///   goes ("gone").
/// - **Choosing a preset** acts as the slider does: a paced preset while the machine runs at full speed pauses it and
///   starts the pace; `full` while the pace runs stops it and resumes the machine; between paced presets a running
///   pace goes on with the new one.
/// - **State** (State, StateBody): read by the `run.state` topic and readout at up to 50 Hz, under a short lock,
///   never waiting for the machine. Each change posts NC_RUN_PACE (RunPacePayload).
///
/// Calls block only for one tick (Step) or a pause: call them from a request worker, the actions worker or the GUI
/// thread, never from the emulation thread.

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "emulator/execution/executionarbiter.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/state/statenode.h"

class Emulator;

namespace execution
{

enum class PaceUnit : uint8_t
{
    Full,          ///< no pacing: the machine runs at its own speed
    Frames,        ///< Emulator::RunFrame
    Scanlines,     ///< Emulator::RunNScanlines (drift-free)
    Instructions,  ///< Emulator::RunNCPUCycles
};

const char* PaceUnitName(PaceUnit unit);

struct PacePreset
{
    const char* id;       ///< "full", "25fps", "5fps", "1fps", "frame", "100lines", "10lines", "1line", "1op"
    const char* label;    ///< as the Qt slider shows it: "1x (50fps)", "25 fps", ...
    PaceUnit unit;
    unsigned count;       ///< units per tick
    int fixedIntervalMs;  ///< -1: no timer (full); > 0: this interval; 0: the adjustable one
    int intervalMin;      ///< the adjustable interval's range and step (0 when fixed)
    int intervalMax;
    int intervalStep;
};

/// What a client shows: the preset, the interval, whether the pace runs and why it stopped last
struct PaceState
{
    size_t preset = 0;
    int intervalMs = 1000;       ///< the adjustable interval (kept while a fixed preset is chosen)
    int effectiveMs = 0;         ///< the interval the pace uses now (0: full speed)
    bool running = false;
    uint64_t ticks = 0;          ///< ticks of the current (or last) run
    std::string stopReason;      ///< "" while running or never run; "stopped", "breakpoint", "resumed", "held", ...
    uint16_t breakpointId = 0xFFFF;
    uint16_t breakpointAddress = 0;
};

class RunPacer
{
public:
    static constexpr int kIntervalMin = 1;
    static constexpr int kIntervalMax = 5000;

    /// The ladder, coarse to fine
    static const std::vector<PacePreset>& Presets();
    /// A preset by id ("5fps"), label ("5 fps") or index ("2"); false when none matches
    static bool PresetIndex(const std::string& name, size_t& index);

    /// The instance's pacer, made on first use
    static std::shared_ptr<RunPacer> For(const std::shared_ptr<Emulator>& emulator);
    /// The instance's pacer if it has one (the readouts: never makes one)
    static std::shared_ptr<RunPacer> Find(const std::string& instanceId);
    /// The instance is gone: the pace stops ("gone"), its thread is joined, the pacer forgotten
    static void Drop(const std::string& instanceId);
    /// The `pace` value of an instance without a pacer (the default preset, not running)
    static StateNode DefaultStateBody(bool ladder = false);

    explicit RunPacer(std::shared_ptr<Emulator> emulator);
    ~RunPacer();
    RunPacer(const RunPacer&) = delete;
    RunPacer& operator=(const RunPacer&) = delete;

    /// Choose a preset and / or the adjustable interval (1 .. 5000 ms), as the slider and the spin box do
    RunReply Set(const Requester& requester, std::optional<size_t> preset, std::optional<int> intervalMs);
    /// Move along the ladder by `delta` presets (+ finer, - coarser), clamped to its ends (a dial, next / prev)
    RunReply Shift(const Requester& requester, int delta);
    /// Start the pace at the chosen preset (full: resume the machine); a running machine is paused first
    RunReply Start(const Requester& requester);
    /// Stop the pace; the machine stays paused where the last tick left it
    RunReply Stop();
    /// Push the dial: a running pace stops, a machine at full speed pauses, else the pace starts
    RunReply Toggle(const Requester& requester);
    /// One tick at the preset now, on the caller's thread (the step button); a running pace stops first
    RunReply Step(const Requester& requester);

    PaceState State() const;
    /// {preset, label, unit, count, interval_ms, effective_ms, adjustable, running, ticks, stop_reason, breakpoint?,
    /// presets?}; `ladder` adds the presets
    StateNode StateBody(bool ladder = false) const;

    /// One tick of the paced run on the caller's thread: false when the run must end (the reason is stored). The
    /// pacer's thread calls it; tests call it to run deterministically
    bool Tick();

private:
    RunReply Reply(const std::string& message) const;
    RunReply Missing() const;
    RunReply HeldReply() const;
    void Loop();
    /// End a run with `reason` (the lock held); posts the change
    void EndLocked(const std::string& reason);
    int EffectiveMsLocked() const;
    void Post() const;
    /// Pause the machine for a paced run (waits up to 2 s for the pause)
    bool PauseFor(const Requester& requester);

    std::weak_ptr<Emulator> _emulator;
    std::string _instanceId;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::thread _thread;
    bool _quit = false;
    uint64_t _generation = 0;  ///< bumped by every start and stop: a sleeping loop of an ended run exits
    Requester _requester;
    PaceState _state;
};

}  // namespace execution
