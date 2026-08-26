#include "mainloop.h"

#include <common/stringhelper.h>

#include <algorithm>

#include "3rdparty/message-center/eventqueue.h"
#include "common/modulelogger.h"
#include "common/timehelper.h"
#include "emulator/sound/soundmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator.h"
#include "emulator/notifications.h"
#include "emulator/io/fdc/wd1793.h"
#include "stdafx.h"

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

#ifdef _WIN32
    // The emulation thread is the audio producer: a frame pre-empted by GUI /
    // background work lands its audio late and eats the ring trough. Above
    // normal (not time-critical) keeps it ahead of ordinary threads without
    // starving the audio device thread, which miniaudio already runs under MMCSS.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

    /// region <Info logging>
    uint64_t lastRun = 0;
    [[maybe_unused]] uint64_t betweenIterations = 0;
    /// endregion </Info logging>

    // TEMP DIAG (audio-underrun investigation): per-frame wake-lateness vs
    // frame-work split, summary every ~5s on stderr. Remove with the diag test.
    const bool audioDiag = getenv("UNREAL_AUDIO_DIAG") != nullptr;
    uint64_t diagFrames = 0, diagLate3 = 0, diagLate10 = 0, diagLate25 = 0;
    int64_t diagMaxLateUs = 0;
    unsigned diagMaxWorkUs = 0;

    while (!stopRequested)
    {
        uint64_t startTime = TimeHelper::GetTimestampUs();
        betweenIterations = startTime - lastRun;

        int64_t diagLateUs = 0;
        if (audioDiag && _nextFrameTime.time_since_epoch().count() > 0)
        {
            // Lateness vs the deadline the previous iteration waited for
            // (_nextFrameTime was already advanced past the wait). Negative
            // values (early, e.g. refill burst) are ignored below.
            diagLateUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - _nextFrameTime)
                             .count();
        }

        [[maybe_unused]] unsigned duration1 = measure_us(&MainLoop::RunFrame, this);

        if (audioDiag)
        {
            diagFrames++;
            if (duration1 > diagMaxWorkUs)
                diagMaxWorkUs = duration1;
            if (diagLateUs > diagMaxLateUs)
                diagMaxLateUs = diagLateUs;
            if (diagLateUs > 3000)
                diagLate3++;
            if (diagLateUs > 10000)
                diagLate10++;
            if (diagLateUs > 25000)
                diagLate25++;
            if ((diagFrames % 250) == 0)
            {
                const std::atomic<uint32_t>* occCellDiag =
                    _context->pAudioRingOccupancy.load(std::memory_order_acquire);
                fprintf(stderr, "[mainloop-diag] frames=%llu late>3ms=%llu >10ms=%llu >25ms=%llu maxLate=%lldus maxWork=%uu occ=%u\n",
                        (unsigned long long)diagFrames, (unsigned long long)diagLate3, (unsigned long long)diagLate10,
                        (unsigned long long)diagLate25, (long long)diagMaxLateUs, diagMaxWorkUs,
                        occCellDiag ? occCellDiag->load(std::memory_order_relaxed) : 0);
                fflush(stderr);
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
                continue;
            }

            // (Re)anchor after start, pause, debugger stall, or heavy lag -
            // never try to "catch up" more than one frame via a stale deadline
            if (_nextFrameTime < now - frameDuration || _nextFrameTime > now + frameDuration)
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
            TimeHelper::WaitUntilPrecise(_nextFrameTime, [&stopRequested] { return (bool)stopRequested; });
        }
        else
        {
            // Turbo mode: Yield CPU time-slice to prevent 100% core usage
            std::this_thread::yield();
        }

        lastRun = startTime;
    }

    MLOGINFO("Stop requested, exiting main loop");

    _isRunning = false;
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

    ExecuteCPUFrameCycle();

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
    _screen->InitFrame();
    
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

    _context->pScreen->UpdateScreen();  // Trigger screen update after each CPU command cycle

    _context->pBetaDisk->handleStep();
    _context->pTape->handleStep();  // Process tape audio each step
    _context->pSoundManager->handleStep();
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
    if (!_context->pScreen->IsScreenHQEnabled())
    {
        _context->pScreen->RenderFrameBatch();
    }

    // Latch the completed frame into the presentation buffer (tear-free copy
    // for GUI display and capture). Must happen after rendering is finished
    // for both batch and per-t-state (ScreenHQ) modes.
    _context->pScreen->LatchFramebuffer();

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
    if (!config.turbo_mode || config.turbo_mode_audio)
    {
        if (_context->pSoundManager)
        {
            try
            {
                _context->pSoundManager->handleFrameEnd();  // Sound manager will call audio callback by itself
            }
            catch (const std::exception& e)
            {
                // Log error but don't crash - audio failure shouldn't stop emulation
                MLOGERROR("SoundManager::handleFrameEnd failed: %s", e.what());
            }
        }
    }

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
    if (!_context->ttdReplayActive)
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
}


//
// Proceed with single frame CPU operations
//
void MainLoop::ExecuteCPUFrameCycle()
{
    _cpu->CPUFrameCycle();
}
