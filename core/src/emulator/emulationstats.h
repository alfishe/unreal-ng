#pragma once

/// @file emulationstats.h
/// @brief The measured pace of one emulator instance (the monitoring planes, `emulator.stats`): frames per second,
/// real-time speed, the host time a frame's work took, interrupts accepted per frame.
///
/// The emulation thread calls OnFrame once per frame from MainLoop::Run with a steady-clock timestamp (NowUs, one
/// clock read a frame) and the work time the loop already measures: no lock, a few integer operations. Once per window (kWindowSeconds) it publishes
/// the window's figures through a seqlock; any thread reads them with Read() without waiting for the machine. A park
/// (pause, a command holding the CPU) ends the window (Restart), so a pause never counts as slow frames; while no
/// frame arrives for longer than two windows the reading says `measuring: false` and fps 0.
///
/// Speed: emulated machine time over wall time, 100 = real time. A frame stands for one nominal frame period times the
/// host speed multiplier (it runs that many more T-states), so 2x shows about 200, the unlimited turbo its rate. The
/// machine's own hardware turbo (a guest-selected 7 MHz) is the machine's real speed and does not count.

#include <atomic>
#include <cstdint>

/// One published window
struct EmulationStats
{
    bool measuring = false;      ///< a window ended within the last two windows (false: paused, stopped, no loop)
    double fps = 0;              ///< frames per wall-clock second over the window
    double nominalFps = 0;       ///< the machine's frame rate at 1x (1e6 / frame_duration_us)
    double speedPercent = 0;     ///< fps / nominalFps * host multiplier * 100
    double frameWorkUsAvg = 0;   ///< host time one frame's emulation took (the loop's RunFrame), mean
    double frameWorkUsMax = 0;   ///< and its maximum in the window
    double frameBudgetPercent = 0;  ///< frameWorkUsAvg / frame_duration_us * 100 (> 100: the host cannot keep up)
    double interruptsPerFrame = 0;  ///< maskable interrupts the CPU accepted, per frame
    double haltedPercent = 0;    ///< T-states the CPU spent in HALT, of the window's frame T-states (frame cost)
    uint64_t windowFrames = 0;   ///< frames in the window
    double windowSeconds = 0;    ///< its wall-clock length
    uint64_t windows = 0;        ///< windows published since the loop started
    uint64_t frame = 0;          ///< the machine's frame counter at the window's end
    uint64_t tStates = 0;        ///< its cumulative T-states then (a reading changes once a window, not per frame)
};

/// The machine's cumulative counters the meter takes deltas of (emulation thread)
struct EmulationCounters
{
    uint64_t interrupts = 0;     ///< Z80::interruptsAccepted
    uint64_t haltedTStates = 0;  ///< EmulatorState::tstates_halted_total
    uint64_t frameTStates = 0;   ///< EmulatorState::tstates_frame_total
    uint64_t frame = 0;          ///< EmulatorState::frame_counter
    uint64_t tStates = 0;        ///< EmulatorState::t_states (cumulative)
};

class EmulationStatsMeter
{
public:
    static constexpr double kWindowSeconds = 0.5;

    /// Emulation thread, once per frame: `nowUs` the frame's start (NowUs),
    /// `workUs` the frame's work, `frameDurationUs` the nominal period, `hostMultiplier` the host speed setting
    /// (1..16), `counters` the machine's cumulative counters. With no work time (a frame that was not timed) pass 0:
    /// the frame counts for the rate, not for the work figures
    void OnFrame(uint64_t nowUs, uint32_t workUs, uint32_t frameDurationUs, uint32_t hostMultiplier,
                 const EmulationCounters& counters);
    /// Emulation thread: the next frame starts a new window (after a park; at the loop's start)
    void Restart();

    /// Any thread: the last published window, `measuring` judged at `nowUs` (the same clock as OnFrame)
    EmulationStats Read(uint64_t nowUs) const;
    /// Read now
    EmulationStats Read() const;
    /// The steady clock in microseconds (the meter's time base)
    static uint64_t NowUs();

private:
    void Publish(uint64_t nowUs);

    /// region <The window (emulation thread only)>
    bool _open = false;
    uint64_t _startUs = 0;
    uint64_t _lastUs = 0;  ///< the previous frame's start
    uint64_t _frames = 0;
    uint64_t _workUsSum = 0;
    uint64_t _workedFrames = 0;
    uint32_t _workUsMax = 0;
    EmulationCounters _atStart;
    EmulationCounters _last;
    uint32_t _frameDurationUs = 0;
    uint32_t _hostMultiplier = 1;
    uint64_t _windows = 0;
    /// endregion </The window>

    /// region <Published (seqlock: odd while the writer writes)>
    std::atomic<uint32_t> _seq{0};
    std::atomic<uint64_t> _pubAtUs{0};
    std::atomic<double> _pubFps{0};
    std::atomic<double> _pubNominal{0};
    std::atomic<double> _pubSpeed{0};
    std::atomic<double> _pubWorkAvg{0};
    std::atomic<double> _pubWorkMax{0};
    std::atomic<double> _pubBudget{0};
    std::atomic<double> _pubInterrupts{0};
    std::atomic<double> _pubHalted{0};
    std::atomic<uint64_t> _pubFrames{0};
    std::atomic<double> _pubSeconds{0};
    std::atomic<uint64_t> _pubWindows{0};
    std::atomic<uint64_t> _pubFrame{0};
    std::atomic<uint64_t> _pubTStates{0};
    /// endregion </Published>
};
