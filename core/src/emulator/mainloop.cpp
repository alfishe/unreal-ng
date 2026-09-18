#include "mainloop.h"

#include <common/stringhelper.h>

#include <algorithm>

#include "3rdparty/message-center/eventqueue.h"
#include "common/modulelogger.h"
#include "common/threadhelper.h"
#include "common/timehelper.h"
#include "emulator/sound/soundmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator.h"
#include "emulator/notifications.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/tape/tapeturbocontroller.h"
#include "stdafx.h"

#include <cmath>

MainLoop::MainLoop(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;

    // Auto-register mainloop in the context
    _context->pMainLoop = this;

    _state = &_context->emulatorState;
    _cpu = _context->pCore;
    _screen = _context->pScreen;
    _soundManager = _context->pSoundManager;

    _isRunning = false;
}

MainLoop::~MainLoop()
{
    if (_isRunning)
        Stop();

    // De-register mainloop from the context (if context still exists)
    if (_context)
    {
        _context->pMainLoop = nullptr;
    }

    _screen = nullptr;
    _cpu = nullptr;
    _state = nullptr;
    _context = nullptr;

    MLOGDEBUG("MainLoop::~MainLoop()");
}

//
// Main emulator loop. Processes all events and executes CPU and video cycles
//
void MainLoop::Run(volatile bool& stopRequested)
{
    if (_cpu == nullptr || _context == nullptr)
    {
        MLOGERROR("MainLoop::Run - _cpu and _context shouldn't be nullptr");
        return;
    }

    _stopRequested = false;
    _isRunning = true;
    _runThreadId.store(std::this_thread::get_id(), std::memory_order_release);

    // Real-time scheduling for the audio producer: only the ACTIVE instance's
    // thread is elevated (EmulatorManager maintains _realtimeRequested), so a
    // bank of headless instances never competes with the audible one. Windows
    // previously did this unconditionally with ABOVE_NORMAL - the same policy
    // now lives in ThreadHelper, applied to every platform and gated here
    UpdateRealtimeScheduling();

    /// region <Info logging>
    uint64_t lastRun = 0;
    [[maybe_unused]] uint64_t betweenIterations = 0;
    /// endregion </Info logging>

    while (!stopRequested)
    {
        uint64_t startTime = TimeHelper::GetTimestampUs();
        betweenIterations = startTime - lastRun;

#ifdef _WIN32
        ULONG64 cyclesBefore = 0;
        QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore);
#endif
        unsigned duration1 = measure_us(&MainLoop::RunFrame, this);
#ifdef _WIN32
        ULONG64 cyclesAfter = 0;
        QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter);
        // Cycles -> us at the nominal clock (the Frame time line prints the
        // 10M-LCG probe, which calibrates this if the clock differs)
        const unsigned frameCpuUs = static_cast<unsigned>((cyclesAfter - cyclesBefore) / 3700);
#else
        const unsigned frameCpuUs = duration1;
#endif

        // Producer health (audio-sync design): a frame that takes longer than
        // its budget starves the audio ring no matter how precise the pacing.
        // Summarize per 256-frame window (~5 s), and only when the window was
        // over budget or came close to it, so a healthy run stays silent.
        if (!_context->config.turbo_mode)
        {
            const unsigned budgetUs = _context->config.frame_duration_us;
            _frameStatMaxUs = std::max(_frameStatMaxUs, duration1);
            if (duration1 > budgetUs)
            {
                _frameStatOverruns++;
                // Per-overrun record: the sleep history that preceded this frame
                // tells whether the core had time to power down before it ran
                MLOGWARNING("Frame overrun: frame #%u took %u us wall / %u us cpu (%u steps); "
                            "slept before this frame %u us, before previous frame %u us; "
                            "clock probe at start %u us (window best so far %u us), cache probe %u us",
                            _frameStatWindow, duration1, frameCpuUs, _stepCount, _lastWaitUs, _prevWaitUs,
                            _frameClockProbeUs, _frameStatMinClockProbeUs, _frameCacheProbeUs);
            }
            if (duration1 > _frameStatWorstWallUs)
            {
                _frameStatWorstWallUs = duration1;
                _frameStatWorstCpuUs = frameCpuUs;
                _frameStatWorstIndex = _frameStatWindow;
                _frameStatWorstCacheProbeUs = _frameCacheProbeUs;
                _frameStatWorstClockProbeUs = _frameClockProbeUs;
            }
            _frameStatSumCacheProbeUs += _frameCacheProbeUs;
            _frameStatSumClockProbeUs += _frameClockProbeUs;
            if (_frameStatMinClockProbeUs == 0 || _frameClockProbeUs < _frameStatMinClockProbeUs)
                _frameStatMinClockProbeUs = _frameClockProbeUs;
            _frameStatHist[std::min<unsigned>(duration1 / 5000, 4)]++;
            _frameStatSumCpuUs += _frameCpuUs;

            // Stage split: "other" is whatever RunFrame spent outside the three
            // timed stages (frame-start/end handlers, HUD, recording, notifications)
            const unsigned timedUs = _frameCpuUs + _frameVideoUs + _frameSoundUs;
            _frameStatMaxCpuUs = std::max(_frameStatMaxCpuUs, _frameCpuUs);
            _frameStatMaxVideoUs = std::max(_frameStatMaxVideoUs, _frameVideoUs);
            _frameStatMaxSoundUs = std::max(_frameStatMaxSoundUs, _frameSoundUs);
            _frameStatMaxOtherUs = std::max(_frameStatMaxOtherUs, duration1 > timedUs ? duration1 - timedUs : 0u);

            // Inside the cpu stage: per-step hooks vs the Z80 core (remainder)
            const unsigned stepScreenUs = static_cast<unsigned>(_stepScreenNs / 1000);
            const unsigned stepIoUs = static_cast<unsigned>(_stepIoNs / 1000);
            const unsigned stepSoundUs = static_cast<unsigned>(_stepSoundNs / 1000);
            const unsigned hooksUs = stepScreenUs + stepIoUs + stepSoundUs;
            _frameStatMaxStepScreenUs = std::max(_frameStatMaxStepScreenUs, stepScreenUs);
            _frameStatMaxStepIoUs = std::max(_frameStatMaxStepIoUs, stepIoUs);
            _frameStatMaxStepSoundUs = std::max(_frameStatMaxStepSoundUs, stepSoundUs);
            _frameStatSumStepScreenUs += stepScreenUs;
            _frameStatSumStepIoUs += stepIoUs;
            _frameStatSumStepSoundUs += stepSoundUs;
            _frameStatMaxZ80Us = std::max(_frameStatMaxZ80Us, _frameCpuUs > hooksUs ? _frameCpuUs - hooksUs : 0u);
            _frameStatMaxSteps = std::max(_frameStatMaxSteps, _stepCount);

            // Start-to-start period catches stalls OUTSIDE RunFrame (late
            // wake-up, blocking log I/O). Gaps over 1 s are pause/anchor
            // artefacts, not stalls.
            if (betweenIterations < 1000000)
                _frameStatMaxPeriodUs = std::max(_frameStatMaxPeriodUs, static_cast<unsigned>(betweenIterations));

            if (++_frameStatWindow == 256)
            {
                if (_frameStatOverruns > 0 || _frameStatMaxUs * 4 > budgetUs * 3 || _frameStatMaxPeriodUs > budgetUs * 2)
                {
                    // Environment probes (diagnostic only): where this thread runs and
                    // how fast - a fixed integer workload and the cost of a clock read,
                    // comparable with BM_CpuSpeedProbe in core-benchmarks
                    unsigned probeUs = 0;
                    unsigned clockNs = 0;
                    {
                        using clk = std::chrono::steady_clock;
                        const auto p0 = clk::now();
                        uint64_t x = 0x9E3779B97F4A7C15ULL;
                        for (uint32_t i = 0; i < 10'000'000; i++)
                            x = x * 6364136223846793005ULL + 1442695040888963407ULL;
                        const auto p1 = clk::now();
                        for (uint32_t i = 0; i < 1000; i++)
                            x += static_cast<uint64_t>(clk::now().time_since_epoch().count());
                        const auto p2 = clk::now();
                        volatile uint64_t sink = x;
                        (void)sink;
                        probeUs = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(p1 - p0).count());
                        clockNs = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::nanoseconds>(p2 - p1).count() / 1000);
                    }
                    unsigned processorNumber = 0;
                    int threadPriority = 0;
#ifdef _WIN32
                    processorNumber = GetCurrentProcessorNumber();
                    threadPriority = GetThreadPriority(GetCurrentThread());
#endif

                    const uint64_t logStart = TimeHelper::GetTimestampUs();
                    MLOGWARNING("Frame time: %u of 256 frames over budget, max %u us, max period %u us "
                                "(budget %u us); stage max: cpu %u, video %u, sound %u, other %u us; "
                                "cpu split max: z80 %u, step-screen %u, step-io %u, step-sound %u us (%u steps); "
                                "worst frame #%u: wall %u us, thread cpu %u us (nominal 3.7 GHz); "
                                "histogram <5/5-10/10-15/15-20/>20 ms: %u/%u/%u/%u/%u; "
                                "window avg: cpu %u, step-screen %u, step-io %u, step-sound %u us; "
                                "clock probe at frame start (200k LCG): worst frame %u us, avg %u us, best %u us; "
                                "cache probe at frame start: worst frame %u us, avg %u us; "
                                "env: cpu#%u prio %d, 10M-LCG probe %u us, clock read %u ns; "
                                "previous log write took %u us",
                                _frameStatOverruns, _frameStatMaxUs, _frameStatMaxPeriodUs, budgetUs,
                                _frameStatMaxCpuUs, _frameStatMaxVideoUs, _frameStatMaxSoundUs, _frameStatMaxOtherUs,
                                _frameStatMaxZ80Us, _frameStatMaxStepScreenUs, _frameStatMaxStepIoUs,
                                _frameStatMaxStepSoundUs, _frameStatMaxSteps,
                                _frameStatWorstIndex, _frameStatWorstWallUs, _frameStatWorstCpuUs,
                                _frameStatHist[0], _frameStatHist[1], _frameStatHist[2], _frameStatHist[3], _frameStatHist[4],
                                static_cast<unsigned>(_frameStatSumCpuUs / 256), static_cast<unsigned>(_frameStatSumStepScreenUs / 256),
                                static_cast<unsigned>(_frameStatSumStepIoUs / 256), static_cast<unsigned>(_frameStatSumStepSoundUs / 256),
                                _frameStatWorstClockProbeUs, static_cast<unsigned>(_frameStatSumClockProbeUs / 256), _frameStatMinClockProbeUs,
                                _frameStatWorstCacheProbeUs, static_cast<unsigned>(_frameStatSumCacheProbeUs / 256),
                                processorNumber, threadPriority, probeUs, clockNs, _frameStatLastLogUs);
                    _frameStatLastLogUs = static_cast<unsigned>(TimeHelper::GetTimestampUs() - logStart);
                }
                _frameStatWindow = 0;
                _frameStatMaxUs = 0;
                _frameStatMaxPeriodUs = 0;
                _frameStatOverruns = 0;
                _frameStatMaxCpuUs = _frameStatMaxVideoUs = _frameStatMaxSoundUs = _frameStatMaxOtherUs = 0;
                _frameStatMaxZ80Us = _frameStatMaxStepScreenUs = _frameStatMaxStepIoUs = _frameStatMaxStepSoundUs = 0;
                _frameStatMaxSteps = 0;
                _frameStatWorstWallUs = _frameStatWorstCpuUs = 0;
                _frameStatWorstIndex = 0;
                std::fill(std::begin(_frameStatHist), std::end(_frameStatHist), 0u);
                _frameStatSumCpuUs = _frameStatSumStepScreenUs = _frameStatSumStepIoUs = _frameStatSumStepSoundUs = 0;
                _frameStatWorstCacheProbeUs = 0;
                _frameStatSumCacheProbeUs = 0;
                _frameStatWorstClockProbeUs = _frameStatMinClockProbeUs = 0;
                _frameStatSumClockProbeUs = 0;
            }
        }

        /// region <Handle Pause>
        // Check if Emulator has requested pause (Emulator is single source of truth)
        Emulator* emulator = _context->pEmulator;
        if (emulator && emulator->IsPaused())
        {
            MLOGINFO("Pause requested");

            // Signal that we've entered paused state
            {
                std::lock_guard<std::mutex> lock(_pauseMutex);
                _isPausedConfirmed.store(true, std::memory_order_release);
            }
            _pauseCV.notify_all();  // Wake up any thread waiting for pause confirmation

            while (emulator->IsPaused())
            {
                // React on stop request while paused
                if (stopRequested)
                {
                    MLOGINFO("Stop requested while paused");
                    break;  // Exit pause loop
                }

                // Use condition variable to wait for resume (more responsive than polling)
                std::unique_lock<std::mutex> lock(_pauseMutex);
                _pauseCV.wait_for(lock, std::chrono::milliseconds(20), [emulator, &stopRequested]() {
                    return !emulator->IsPaused() || stopRequested;
                });
            }

            // Clear paused confirmation when resuming
            _isPausedConfirmed.store(false, std::memory_order_release);

            continue;  // Either we'll render next frame or exit main loop via stopRequested check
        }
        /// endregion </Handle Pause>

        /// region <Info logging>
        // MLOGINFO("Frame recalculation time: %d us", duration1);
        // std::cout << StringHelper::Format("Frame recalculation time: %d us", duration1) << std::endl;
        // std::cout << StringHelper::Format("Between iterations: %d us", betweenIterations) << std::endl;
        /// endregion </Info logging>

        // Selection may have moved to another instance while we run - sampled
        // here (one relaxed atomic read) so a live switch demotes this thread
        // and promotes the newly active one within a frame
        UpdateRealtimeScheduling();

        // Synchronization strategy depends on turbo mode setting
        // Recording should NEVER interfere with normal frame pacing - encoder
        // backpressure (blocking mode) handles non-realtime encoders separately
        const CONFIG& config = _context->config;

        if (!config.turbo_mode)
        {
            // Normal mode: absolute-deadline frame pacing.
            // The frame clock is the timing master: each frame is released at
            // exactly config.frame_duration_us intervals (Pentagon: 20480us =
            // 48.83 fps; see CalculateFrameDurationUs). wait_until against an
            // accumulated deadline self-corrects scheduler wake-up latency.
            // Fine rate matching against the audio DAC is the DRC controller's
            // job (SoundManager::updateDrcControl) - the deadline only has to
            // be approximately right; DRC absorbs the residual continuously.
            const std::chrono::microseconds frameDuration(config.frame_duration_us);
            const auto now = std::chrono::steady_clock::now();

            // Emergency refill (audio-sync design 5.3): if the ring is nearly
            // empty (cold start, debugger stall, disk hitch), skip the sleep
            // and produce frames back-to-back until occupancy recovers -
            // DRC's +-0.5% trim is far too slow for bulk refill.
            // Threshold is rate-aware and deliberately far below the DRC
            // target: the occupancy sawtooth dips ~1 frame below target every
            // cycle, and the refill must NEVER fire in steady state (see
            // SoundManager::EMERGENCY_REFILL_MS)
            const uint32_t devRate = _context->pAudioDeviceSampleRate.load(std::memory_order_relaxed);
            const uint32_t refillThresholdFrames = static_cast<uint32_t>(
                (devRate ? devRate : AUDIO_SAMPLING_RATE) * SoundManager::EMERGENCY_REFILL_MS / 1000.0);
            const std::atomic<uint32_t>* occCell =
                _context->pAudioRingOccupancy.load(std::memory_order_acquire);
            if (occCell && occCell->load(std::memory_order_relaxed) < refillThresholdFrames)
            {
                _nextFrameTime = now;  // Re-anchor: refill burst must not distort the cadence after
                _prevWaitUs = _lastWaitUs;
                _lastWaitUs = 0;
                continue;
            }

            // (Re)anchor after start, pause, debugger stall, or heavy lag -
            // never try to "catch up" more than one frame via a stale deadline.
            // _nextFrameTime is still the deadline of the frame that just ran,
            // so it lags 'now' by that frame's duration even in steady state.
            // The threshold must therefore be TWO frames: with one, any frame
            // that overran its budget by a few hundred us re-anchored and
            // scheduled the next frame a full frame after the late finish -
            // a ~2x period the audio ring cannot cover, heard as a dropout on
            // every overrun. Up to one frame of catch-up is allowed instead;
            // DRC absorbs the residual (audio-sync design).
            if (_nextFrameTime < now - 2 * frameDuration || _nextFrameTime > now + frameDuration)
            {
                _nextFrameTime = now;
            }
            _nextFrameTime += frameDuration;

            // Precise, interruptible sleep (polls the stop flag every few ms).
            // Must NOT be std::condition_variable::wait_until: on Windows it
            // wakes 1 ms (MSVC) to 10-17 ms (MinGW) late, which consumed the
            // whole audio ring trough (DRC_TARGET_MS - 1 frame ~ 19.5 ms)
            // against WASAPI's 10 ms pulls and caused steady underruns
            // ("ring errors ... dequeue=N" growing) - see
            // TimeHelper::WaitUntilPrecise and SoundAdaptivity.AVLatencyBudget
            const auto waitStart = std::chrono::steady_clock::now();
            TimeHelper::WaitUntilPrecise(_nextFrameTime, [&stopRequested] { return (bool)stopRequested; });
            _prevWaitUs = _lastWaitUs;
            _lastWaitUs = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                    std::chrono::steady_clock::now() - waitStart).count());
        }
        else
        {
            // Turbo mode: Yield CPU time-slice to prevent 100% core usage
            std::this_thread::yield();
        }

        lastRun = startTime;
    }

    MLOGINFO("Stop requested, exiting main loop");

    // Return the thread to normal scheduling before it is joined - it may
    // also be restarted later via a fresh StartAsync
    if (_realtimeApplied)
    {
        ThreadHelper::setNormalPriority();
        _realtimeApplied = false;
    }

    _isRunning = false;
}

void MainLoop::UpdateRealtimeScheduling()
{
    // Turbo mode runs frames back-to-back with no cadence: a time-constraint
    // thread violating its declared compute budget gets force-demoted by the
    // OS watchdog (macOS), and an SCHED_FIFO producer spinning flat-out would
    // starve the audio stack it is supposed to feed. Real-time is reserved
    // for cadenced realtime playback only
    const bool requested = _realtimeRequested.load(std::memory_order_relaxed)
                               && !_context->config.turbo_mode;

    if (requested == _realtimeApplied)
        return;

    if (requested)
    {
        ThreadHelper::setRealtimePriority();
        MLOGINFO("Emulation thread elevated to realtime scheduling");
    }
    else
    {
        ThreadHelper::setNormalPriority();
        MLOGINFO("Emulation thread returned to normal scheduling");
    }

    _realtimeApplied = requested;
}

bool MainLoop::WaitForPauseConfirmation(uint32_t timeoutMs)
{
    // Fast path: not running at all means no frame can be mid-flight
    if (!_isRunning)
        return true;

    // Called from the emulation thread itself (e.g. breakpoint handler pausing
    // mid-frame): no frame can be executing concurrently with the caller, and
    // waiting here would only stall until the timeout. Return immediately.
    if (_runThreadId.load(std::memory_order_acquire) == std::this_thread::get_id())
        return true;

    std::unique_lock<std::mutex> lock(_pauseMutex);
    return _pauseCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this]() { return _isPausedConfirmed.load(std::memory_order_acquire); });
}

void MainLoop::ConfirmPauseFromCpu()
{
    // Mirror of the park-entry confirm in Run()'s pause loop (same mutex/CV
    // pair), issued by the CPU thread when it parks INSIDE a frame via
    // Emulator::WaitWhilePaused() - Run()'s own confirm is unreachable above
    // that park, so without this every WaitForPauseConfirmation() caller
    // would burn its full timeout while the CPU is in fact safely parked.
    {
        std::lock_guard<std::mutex> lock(_pauseMutex);
        _isPausedConfirmed.store(true, std::memory_order_release);
    }
    _pauseCV.notify_all();  // Wake threads waiting for the pause confirmation
}

void MainLoop::InvalidatePauseConfirmation()
{
    // Eager drop of a confirmation from a park we are exiting (Resume/Stop).
    // The Run() loop clears its own flag only after its parked wait wakes
    // (up to its 20 ms poll quantum); until then a new Pause() could consume
    // the stale "parked" confirmation and return with the emulation thread
    // already running the next frame - two Z80 drivers at once.
    std::lock_guard<std::mutex> lock(_pauseMutex);
    _isPausedConfirmed.store(false, std::memory_order_release);
}

void MainLoop::Stop()
{
    _stopRequested = true;  // Frame wait polls this flag (TimeHelper::WaitUntilPrecise, <= 4 ms)
    _pauseCV.notify_all();
}

void MainLoop::RunFrame()
{
    /// region <Sanity checks>
    // Check for null context - return early if context is destroyed (during shutdown)
    if (!_context)
        return;

    if (!_context->pScreen)
        return;

#ifdef _DEBUG
    // Additional debug-only validation
    if (!_context)
        throw std::logic_error("MainLoop::RunFrame - context undefined");

    if (!_context->pScreen)
        throw std::logic_error("MainLoop::RunFrame - screen not initialized");
#endif
    /// endregion <Sanity checks>

    /// region <Frame start handlers>

    OnFrameStart();

    /// endregion </Frame start handlers>

    // Execute CPU cycles for single video frame

    // Turbo render decimation: suspend contingent rendering for the CPU cycle
    // of a skipped frame. The screen-side flag covers ALL DrawPeriod entries
    // in one place - including Screen::SetBorderColor's own UpdateScreen call,
    // which border-striping loaders reach thousands of times per frame. It is
    // cleared again before frame end, so batch render, framebuffer latch and
    // any manual debug stepping while paused are never affected.
    _screen->SetTurboRenderSkip(!_renderThisFrame);

    _stepScreenNs = _stepIoNs = _stepSoundNs = 0;
    _stepCount = 0;

    // Clock-speed probe (diagnostic): how fast does this core run right now?
    {
        using clk = std::chrono::steady_clock;
        const auto p0 = clk::now();
        uint64_t x = 0x9E3779B97F4A7C15ULL;
        for (uint32_t i = 0; i < 200'000; i++)
            x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto p1 = clk::now();
        volatile uint64_t sink = x;
        (void)sink;
        _frameClockProbeUs = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(p1 - p0).count());
    }

    // Cache-residency probe (diagnostic): is the framebuffer still in cache?
    _frameCacheProbeUs = 0;
    {
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        if (fb.memoryBuffer != nullptr && fb.memoryBufferSize >= 65536)
        {
            using clk = std::chrono::steady_clock;
            const auto p0 = clk::now();
            const volatile uint8_t* base = reinterpret_cast<const volatile uint8_t*>(fb.memoryBuffer);
            unsigned sum = 0;
            for (size_t off = 0; off < 65536; off += 64)
                sum += base[off];
            const auto p1 = clk::now();
            volatile unsigned sink = sum;
            (void)sink;
            _frameCacheProbeUs = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(p1 - p0).count());
        }
    }

    const uint64_t cpuStart = TimeHelper::GetTimestampUs();
    ExecuteCPUFrameCycle();
    _frameCpuUs = static_cast<unsigned>(TimeHelper::GetTimestampUs() - cpuStart);

    _screen->SetTurboRenderSkip(false);

    /// region <Frame end handlers>

    OnFrameEnd();

    /// endregion </Frame end handlers

    // Process external periphery devices

    // Flush all generated data and buffers

    // Render Video and Audio using host platform capabilities
    // RenderVideo();
    // RenderAudio();

    // Queue new frame data to Video/Audio encoding
    // Note: Recording is handled by RecordingManager via OnFrameEnd() callback
    // when the recording feature is enabled
}

void MainLoop::OnFrameStart()
{
    // Guard against null context during shutdown
    if (!_context)
        return;

    _context->pTape->handleFrameStart();
    _soundManager->handleFrameStart();
    _context->pMemory->handleFrameStart();
    _screen->handleFrameStart();
    _screen->InitFrame();

    /// region <Turbo render decimation>
    // In turbo mode the GUI only needs a preview cadence: 49 of every 50
    // frames skip rendering entirely (per-t-state DrawPeriod work, batch
    // render, framebuffer latch, frame-refresh notification). The one
    // rendered frame keeps full ScreenHQ fidelity - border and multicolor
    // effects inside it are drawn per t-state exactly as at normal speed.
    // Machine timing is untouched: CPU, tape, FDC, TTD and analyzers still
    // process every frame (turbo tape design §3.1 invariance). Recording
    // forces a render every frame so captured video is never decimated.
    {
        const CONFIG& config = _context->config;
        bool recording = false;
#ifdef ENABLE_RECORDING
        recording = _context->pRecordingManager && _context->pRecordingManager->IsRecording();
#endif
        UpdateTurboRenderDecimation(config.turbo_mode, config);
        _renderThisFrame = !config.turbo_mode || recording ||
                           (_state->frame_counter % _turboRenderDecimation == 0);

        // A rendered frame after skipped ones starts with a stale _prevTstate
        // (InitFrame does not reset it). DrawPeriod would self-heal through
        // the wrap-adjust + bounds check but lose the frame's first t-states;
        // resetting the tracker makes the rendered frame complete from t=0.
        if (_renderThisFrame && !_lastFrameRendered)
            _screen->ResetPrevTstate();
    }
    /// endregion </Turbo render decimation>

    // Dispatch frame start event to AnalyzerManager
    if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager())
    {
        _context->pDebugManager->GetAnalyzerManager()->dispatchFrameStart();
    }
}

void MainLoop::OnCPUStep()
{
    // Guard against null context during shutdown
    if (!_context)
        return;

    // Validate pointers before use
    if (!_context->pScreen || !_context->pBetaDisk || !_context->pSoundManager)
    {
        MLOGERROR("MainLoop::OnCPUStep - null peripheral pointer detected");
        return;
    }

    // Turbo render decimation: skipped frames bypass contingent rendering.
    // Everything below still runs on every CPU step in every mode.
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();

    if (_renderThisFrame)
    {
        _context->pScreen->UpdateScreen();  // Trigger screen update after each CPU command cycle
    }
    const auto t1 = clk::now();

    _context->pBetaDisk->handleStep();
    _context->pTape->handleStep();  // Process tape audio each step
    const auto t2 = clk::now();

    _context->pSoundManager->handleStep();
    const auto t3 = clk::now();

    _stepScreenNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    _stepIoNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    _stepSoundNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();
    _stepCount++;
}

void MainLoop::OnFrameEnd()
{
    // Guard against null context during shutdown
    if (!_context)
        return;

    // Additional safety checks - ensure context integrity
    if (!_context->pScreen || !_context->pSoundManager)
        return;

    // =========================================================================
    // SCREENHQ=OFF BATCH RENDERING
    // =========================================================================
    // When ScreenHQ feature is disabled, per-t-state Draw() calls are skipped
    // in Screen::DrawPeriod(). Instead, we render the entire screen here in
    // one batch using RenderScreen_Batch8() - approximately 25x faster.
    //
    // This MUST happen BEFORE we capture the frame for recording or display,
    // as the framebuffer would otherwise be empty (no per-t-state rendering).
    //
    // See: docs/inprogress/2026-01-11-performance-optimizations/phase-4-5-execution-log.md
    // =========================================================================
    // Turbo render decimation: skipped frames render nothing and keep the
    // previously latched framebuffer for display (decimated preview).
    _frameVideoUs = 0;
    if (_renderThisFrame)
    {
        const uint64_t videoStart = TimeHelper::GetTimestampUs();

        if (!_context->pScreen->IsScreenHQEnabled())
        {
            _context->pScreen->RenderFrameBatch();
        }

        // Latch the completed frame into the presentation buffer (tear-free copy
        // for GUI display and capture). Must happen after rendering is finished
        // for both batch and per-t-state (ScreenHQ) modes.
        _context->pScreen->LatchFramebuffer();

        _frameVideoUs = static_cast<unsigned>(TimeHelper::GetTimestampUs() - videoStart);
    }

    // Basic sanity check for context corruption
    if (_context->config.frame == 0 || _context->config.frame > 100000)
        return;  // Invalid frame timing suggests corruption

    // Update counters
    _context->emulatorState.t_states += _context->config.frame;

    // Trigger events for peripherals
    if (_context->pTape)
    {
        try
        {
            _context->pTape->handleFrameEnd();
        }
        catch (const std::exception& e)
        {
            MLOGERROR("Tape::handleFrameEnd failed: %s", e.what());
        }
    }

    // Turbo tape loading (design 2026-09-04-turbo-tape-loading §6.1): tick
    // right after the tape's own frame end so watchdog freezes and natural
    // end-of-tape are observed in the same frame they happen
    if (_context->pTapeTurboController)
    {
        try
        {
            _context->pTapeTurboController->handleFrameEnd();
        }
        catch (const std::exception& e)
        {
            MLOGERROR("TapeTurboController::handleFrameEnd failed: %s", e.what());
        }
    }
    if (_context->pBetaDisk)
    {
        try
        {
            _context->pBetaDisk->handleFrameEnd();
        }
        catch (const std::exception& e)
        {
            MLOGERROR("BetaDisk::handleFrameEnd failed: %s", e.what());
        }
    }

    // Audio generation: Skip in turbo mode unless explicitly requested
    const CONFIG& config = _context->config;
    _frameSoundUs = 0;
    if (!config.turbo_mode || config.turbo_mode_audio)
    {
        if (_context->pSoundManager)
        {
            const uint64_t soundStart = TimeHelper::GetTimestampUs();
            try
            {
                _context->pSoundManager->handleFrameEnd();  // Sound manager will call audio callback by itself
            }
            catch (const std::exception& e)
            {
                // Log error but don't crash - audio failure shouldn't stop emulation
                MLOGERROR("SoundManager::handleFrameEnd failed: %s", e.what());
            }
            _frameSoundUs = static_cast<unsigned>(TimeHelper::GetTimestampUs() - soundStart);
        }
    }

    // HUD notifications: emit once per frame (zero overhead when HUD disabled)
    _context->pMemory->handleFrameEnd();
    _screen->handleFrameEnd();

#ifdef ENABLE_RECORDING
    // Capture video frame for recording (if recording is active)
    // This is called AFTER UpdateScreen() has rendered the current frame
    // In turbo mode, this captures every emulated frame for correct timing
    if (_context->pRecordingManager && _context->pRecordingManager->IsRecording() && _context->pScreen)
    {
        try
        {
            _context->pRecordingManager->CaptureFrame(_context->pScreen->GetFramebufferDescriptor());
        }
        catch (const std::exception& e)
        {
            // Log error but don't crash - recording failure shouldn't stop emulation
            MLOGERROR("RecordingManager::CaptureFrame failed: %s", e.what());
        }
    }
#endif

    // Sync shared memory if enabled (for external viewers like screen-viewer, debuggers, memory dumpers, etc.)
    // This ensures the memory-mapped region is visible to other processes
    // Only sync when shared memory feature is actually enabled to avoid overhead
    if (_context->pMemory && _context->pMemory->IsSharedMemoryEnabled())
    {
        try
        {
            _context->pMemory->SyncToDisk();
        }
        catch (const std::exception& e)
        {
            MLOGERROR("Memory::SyncToDisk failed: %s", e.what());
        }
    }

    // Notify that video frame is composed and ready for rendering
    // Send per-instance frame refresh event with emulator ID for filtering
    //
    // TTD silent-replay suppression (parent TDD §8.2 + Appendix C):
    // during replay the UI must not redraw per-frame — replay may run
    // dozens of frames per seek and a redraw storm would dominate seek
    // latency. The replay engine restores the final frame visually via
    // Screen::InitFrame after ExitReplayMode.
    if (_renderThisFrame && !_context->ttdReplayActive)
    {
        try
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            std::string emulatorId = _context->pEmulator ? _context->pEmulator->GetId() : "";
            messageCenter.Post(NC_VIDEO_FRAME_REFRESH,
                               new EmulatorFramePayload(emulatorId, _context->emulatorState.frame_counter));
        }
        catch (const std::exception& e)
        {
            // Log error but don't crash - message center failure shouldn't stop emulation
            MLOGERROR("MessageCenter post failed: %s", e.what());
        }
    }

    // Dispatch frame end event to AnalyzerManager
    if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager())
    {
        _context->pDebugManager->GetAnalyzerManager()->dispatchFrameEnd();
    }

    // TTD per-frame checkpoint capture (parent TDD §7.1).
    // OnFrameBoundary is a no-op when the TTD manager is null, when the
    // session state is not Recording, or when the timetravel feature flag
    // is off (the cached bool in Memory gates the dirty hook). Cost when
    // idle: one predictable branch. Cost when recording: dirty pages get
    // a 16 KB Intern each, clean pages get a cheap AddRef.
    if (_context->pTimeTravelManager)
    {
        try
        {
            _context->pTimeTravelManager->OnFrameBoundary();
        }
        catch (const std::exception& e)
        {
            MLOGERROR("TimeTravelManager::OnFrameBoundary failed: %s", e.what());
        }
    }

    // Process keyboard injection sequences (for automation)
    // This is called each frame to advance any queued key sequences (tap/release timing)
    if (_context->pDebugManager && _context->pDebugManager->GetKeyboardManager())
    {
        _context->pDebugManager->GetKeyboardManager()->OnFrame();
    }

    // Release timed mouse clicks (automation) at frame boundaries
    if (_context->pDebugManager && _context->pDebugManager->GetMouseManager())
    {
        _context->pDebugManager->GetMouseManager()->OnFrame();
    }

    _lastFrameRendered = _renderThisFrame;
}


//
// Proceed with single frame CPU operations
//
void MainLoop::ExecuteCPUFrameCycle()
{
    _cpu->CPUFrameCycle();
}

/// region <Turbo render decimation>

uint64_t MainLoop::ComputeTurboRenderDecimation(double measuredFps, double targetFps, double maxRenderFps)
{
    if (!(measuredFps > 0.0) || !(targetFps > 0.0))
        return TURBO_RENDER_DECIMATION;

    double n;
    if (maxRenderFps > targetFps)
        n = std::ceil(measuredFps / maxRenderFps);  // rendered <= display rate, > display rate / 2
    else
        n = std::floor(measuredFps / targetFps);    // rendered in [target, 2 x target)

    if (n < 1.0)
        return 1;
    if (n >= static_cast<double>(TURBO_RENDER_DECIMATION_MAX))
        return TURBO_RENDER_DECIMATION_MAX;
    return static_cast<uint64_t>(n);
}

void MainLoop::UpdateTurboRenderDecimation(bool turboMode, const CONFIG& config)
{
    if (!turboMode || !_turboRenderAdaptive)
    {
        // Leaving turbo (or adaptive off): back to the fixed default, drop the sample window
        _turboRateSampling = false;
        _turboMeasuredFps = 0.0;
        _turboRenderDecimation = TURBO_RENDER_DECIMATION;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const uint32_t frame = _state->frame_counter;

    // (Re)start the sample window: first turbo frame, or the counter went backwards (reset)
    if (!_turboRateSampling || frame < _turboRateSampleFrame)
    {
        _turboRateSampling = true;
        _turboRateSampleTime = now;
        _turboRateSampleFrame = frame;
        return;
    }

    const double seconds = std::chrono::duration<double>(now - _turboRateSampleTime).count();
    if (seconds < TURBO_RATE_SAMPLE_SECONDS)
        return;

    const uint32_t frames = frame - _turboRateSampleFrame;
    _turboMeasuredFps = frames / seconds;

    const double targetFps = config.frame_duration_us > 0 ? 1000000.0 / config.frame_duration_us : 0.0;
    _turboRenderDecimation = ComputeTurboRenderDecimation(_turboMeasuredFps, targetFps, _turboRenderMaxFps);

    _turboRateSampleTime = now;
    _turboRateSampleFrame = frame;
}

/// endregion </Turbo render decimation>
