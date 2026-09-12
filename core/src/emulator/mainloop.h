#pragma once
#include <atomic>
#include <chrono>
#include <thread>

#include "3rdparty/message-center/eventqueue.h"
#include "common/logger.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "stdafx.h"

class MainLoop : public Observer
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_MAINLOOP;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    ModuleLogger* _logger;

    EmulatorState* _state = nullptr;
    Core* _cpu = nullptr;
    Screen* _screen = nullptr;
    SoundManager* _soundManager = nullptr;

    volatile bool _isRunning = false;
    volatile bool _stopRequested = false;
    std::atomic<bool> _isPausedConfirmed{false};  // Set by Z80 thread when actually paused
    std::condition_variable _pauseCV;              // Signaled when pause is confirmed
    std::mutex _pauseMutex;                        // Protects pause state
    std::atomic<std::thread::id> _runThreadId{};   // Thread currently executing Run() (emulation thread)


    // Absolute deadline for the next frame (steady clock). Advanced by exactly
    // one frame duration per iteration so scheduler wake-up latency does not
    // accumulate into the effective frame period (that accumulation drains the
    // audio ring to its watermark and causes visible rubber-banding when the
    // catch-up frames arrive). Zero-initialized = resync on first frame.
    std::chrono::steady_clock::time_point _nextFrameTime{};

    /// region <Turbo render decimation>
public:
    /// While turbo mode is active (and no recording is in progress) only 1 of
    /// every N frames performs rendering work. N starts at this default and, when
    /// adaptive decimation is on, follows the measured turbo frame rate so the
    /// rendered cadence stays within [target, 2 x target) frames per second
    /// (5008 fps on a 50.08 Hz machine -> every 100th frame). Public so tests can
    /// align their expectations with the fixed cadence.
    static constexpr uint64_t TURBO_RENDER_DECIMATION = 50;
    static constexpr uint64_t TURBO_RENDER_DECIMATION_MAX = 4096;
    static constexpr double TURBO_RATE_SAMPLE_SECONDS = 0.5;

    /// Decimation for a measured turbo rate, clamped to [1, TURBO_RENDER_DECIMATION_MAX].
    /// With a display bound (maxRenderFps > target): N = ceil(measured / maxRenderFps), so the
    /// rendered rate is <= the display rate and > half of it (5008 fps on a 120 Hz panel ->
    /// every 42nd frame, 119 rendered fps). Without one: N = floor(measured / target), so the
    /// rendered rate is in [target, 2 x target).
    static uint64_t ComputeTurboRenderDecimation(double measuredFps, double targetFps, double maxRenderFps = 0.0);

    /// Upper bound for the rendered rate in turbo mode - the display's refresh rate (or the
    /// top of its VRR range), supplied by the host UI. 0 = no display bound (2 x target rule).
    void SetTurboRenderMaxFps(double maxFps) { _turboRenderMaxFps = maxFps > 0.0 ? maxFps : 0.0; }
    double GetTurboRenderMaxFps() const { return _turboRenderMaxFps; }

    /// Adaptive decimation (default on). Tests that assert the fixed cadence turn it off.
    void SetTurboRenderAdaptive(bool adaptive) { _turboRenderAdaptive = adaptive; }
    bool IsTurboRenderAdaptive() const { return _turboRenderAdaptive; }
    uint64_t GetTurboRenderDecimation() const { return _turboRenderDecimation; }
    /// Emulated frames per second measured while in turbo mode (0 until the first sample)
    double GetTurboMeasuredFps() const { return _turboMeasuredFps; }

protected:
    /// Computed once per frame in OnFrameStart; gates per-frame rendering work
    /// only (contingent UpdateScreen, batch render, framebuffer latch and the
    /// frame-refresh notification). Machine-time work (CPU, tape, FDC, TTD,
    /// analyzers, keyboard) always runs, keeping turbo timing invariant.
    bool _renderThisFrame = true;
    /// True while the previous frame rendered. A rendered frame that follows
    /// one or more skipped frames needs Screen::ResetPrevTstate() so DrawPeriod
    /// starts the beam at t=0 instead of a stale end-of-frame position.
    bool _lastFrameRendered = true;

    bool _turboRenderAdaptive = true;
    double _turboRenderMaxFps = 0.0;
    uint64_t _turboRenderDecimation = TURBO_RENDER_DECIMATION;
    double _turboMeasuredFps = 0.0;
    bool _turboRateSampling = false;
    std::chrono::steady_clock::time_point _turboRateSampleTime{};
    uint32_t _turboRateSampleFrame = 0;

    /// Sample the turbo frame rate (wall clock vs. frame counter) and refresh
    /// _turboRenderDecimation every TURBO_RATE_SAMPLE_SECONDS; resets when turbo ends
    void UpdateTurboRenderDecimation(bool turboMode, const CONFIG& config);
    /// endregion </Turbo render decimation>
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    MainLoop() = delete;  // Disable default constructor. C++ 11 or better feature
    MainLoop(EmulatorContext* context);
    virtual ~MainLoop();
    /// endregion </Constructors / destructors>

public:
    void Run(volatile bool& exit);
    void Stop();

    /// @brief CPU core access for debugger/automation port I/O.
    /// @note Callers from control threads must only use it while the
    ///       emulation thread is parked (paused) - same discipline as direct
    ///       memory access via Emulator::GetMemory().
    Core* GetCPU() const { return _cpu; }

    /// @brief Returns true when the emulation thread has actually parked in the pause loop
    /// (Emulator::Pause() only sets a flag; the current frame still finishes executing)
    bool IsPauseConfirmed() const { return _isPausedConfirmed.load(std::memory_order_acquire); }

    /// @brief Block until the emulation thread confirms it parked in the pause loop
    /// @param timeoutMs Maximum time to wait, in milliseconds
    /// @return true if pause was confirmed within the timeout
    /// @note Safe to call from any thread. When called from the emulation thread itself
    ///       (e.g. a breakpoint handler pausing mid-frame) it returns immediately -
    ///       no frame can be in flight concurrently with the caller in that case.
    ///       May time out legitimately when execution is paused inside a frame
    ///       (e.g. breakpoint hit), since the pause loop is only reached at frame end.
    bool WaitForPauseConfirmation(uint32_t timeoutMs);

    /// @brief Confirm the pause from the CPU thread itself when it parks mid-frame
    /// @note Used by Emulator::WaitWhilePaused(): when the CPU parks inside a frame
    ///       (breakpoint/watchpoint handler or the per-instruction pause check in
    ///       Z80FrameCycle), Run()'s frame-end park/confirm path is unreachable
    ///       above it. Confirming on MainLoop's behalf lets WaitForPauseConfirmation()
    ///       callers observe the park immediately instead of burning their timeout.
    void ConfirmPauseFromCpu();

    /// @brief Eagerly drop a (possibly) stale pause confirmation
    /// @note Called from Emulator::Resume()/Stop() right after un-parking the CPU
    ///       thread. Without this, a Pause() issued immediately after Resume()
    ///       could consume a stale "parked" confirmation from the previous park
    ///       and return while the emulation thread is already executing the next
    ///       frame - two Z80 drivers at once.
    void InvalidatePauseConfirmation();

protected:
    void RunFrame();
    void ExecuteCPUFrameCycle();

    /// region <Event handlers>
public:
    void OnFrameStart();
    void OnCPUStep();
    void OnFrameEnd();
    /// endregion </Event handlers>

};

// Code Under Test (CUT) wrapper to allow access to protected and private methods
// for unit testing / benchmark purposes.
//
// Guarded like the other CUTs in the codebase (see screenzx.h): the wrapper only
// exists in translation units that ask for it, so a production build cannot reach
// RunFrame()/ExecuteCPUFrameCycle() around the run-loop's own pacing. Both macros
// are honoured - the tests target defines _CODE_UNDER_TEST, the benchmarks target
// defines _CODE_UNDER_BENCHMARK.

#if defined(_CODE_UNDER_TEST) || defined(_CODE_UNDER_BENCHMARK)

class MainLoop_CUT : public MainLoop
{
public:
    using MainLoop::MainLoop;  // Inherit constructors
    using MainLoop::RunFrame;
    using MainLoop::ExecuteCPUFrameCycle;

    /// Alias kept because the TTD frame-overhead benchmark calls it under this
    /// name. It is one frame of work with no sync and no frame limiting - which
    /// is the whole point of driving RunFrame() directly rather than Run().
    void RunFramePublic() { RunFrame(); }

    /// Same, for the CPU-only path the benchmark measures as its baseline.
    void ExecuteCPUFrameCyclePublic() { ExecuteCPUFrameCycle(); }
};

/// Underscore-free spelling. Both names are in use: the tests say MainLoop_CUT,
/// the TTD frame-overhead benchmark says MainLoopCUT.
using MainLoopCUT = MainLoop_CUT;

#endif  // _CODE_UNDER_TEST || _CODE_UNDER_BENCHMARK