#include "mainloop.h"

#include <common/stringhelper.h>

#include <algorithm>

#include "3rdparty/message-center/eventqueue.h"
#include "common/modulelogger.h"
#include "common/timehelper.h"
#include "emulator.h"
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

    _moreAudioDataRequested.store(false, std::memory_order_release);

    _isRunning = false;
}

MainLoop::~MainLoop()
{
    if (_isRunning)
        Stop();

    // Unsubscribe from audio buffer state event(s)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainLoop::handleAudioBufferHalfFull);
    messageCenter.RemoveObserver(NC_AUDIO_BUFFER_HALF_FULL, observerInstance, callback);

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
    _pauseRequested = false;
    _isRunning = true;

    // Subscribe to audio buffer state event(s)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    // Subscribe to video frame refresh events
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainLoop::handleAudioBufferHalfFull);
    messageCenter.AddObserver(NC_AUDIO_BUFFER_HALF_FULL, observerInstance, callback);

    /// region <Info logging>
    static std::chrono::milliseconds timeout(20);  // Set timeout for audio buffer refresh wait
    uint64_t lastRun = 0;
    [[maybe_unused]] uint64_t betweenIterations = 0;
    /// endregion </Info logging>

    while (!stopRequested)
    {
        uint64_t startTime = TimeHelper::GetTimestampUs();
        betweenIterations = startTime - lastRun;

        [[maybe_unused]] unsigned duration1 = measure_us(&MainLoop::RunFrame, this);

        /// region <Handle Pause>
        if (_pauseRequested)
        {
            MLOGINFO("Pause requested");

            while (_pauseRequested)
            {
                // React on stop request while paused
                if (stopRequested)
                {
                    MLOGINFO("Stop requested while paused");
                    break;  // Exit pause loop
                }

                sleep_ms(20);
            }

            continue;  // Either we'll render next frame or exit main loop via stopRequested check
        }
        /// endregion </Handle Pause>

        /// region <Info logging>
        // MLOGINFO("Frame recalculation time: %d us", duration1);
        // std::cout << StringHelper::Format("Frame recalculation time: %d us", duration1) << std::endl;
        // std::cout << StringHelper::Format("Between iterations: %d us", betweenIterations) << std::endl;
        /// endregion </Info logging>

        // Synchronization strategy depends on turbo mode setting
        const CONFIG& config = _context->config;
        if (!config.turbo_mode)
        {
            // Normal mode: Wait until audio callback requests more data and buffer is about half-full
            // That means we're in sync between audio and video frames
            std::unique_lock<std::mutex> lock(_audioBufferMutex);
            auto moreAudioDataRequested = std::ref(_moreAudioDataRequested);
            _cv.wait_for(lock, timeout, [&moreAudioDataRequested] {
                return moreAudioDataRequested.get().load(std::memory_order_acquire);
            });
            _moreAudioDataRequested.store(false);
            lock.unlock();
        }
        else
        {
            // Turbo mode: Run as fast as possible without audio synchronization
            // Optional: Yield CPU to prevent 100% core usage if desired
            // std::this_thread::yield();
        }

        lastRun = startTime;
    }

    MLOGINFO("Stop requested, exiting main loop");

    _isRunning = false;
}

void MainLoop::Stop()
{
    _stopRequested = true;
}

void MainLoop::Pause()
{
    _pauseRequested = true;

    _cpu->Pause();
}

void MainLoop::Resume()
{
    _pauseRequested = false;

    _cpu->Resume();
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
}

void MainLoop::OnCPUStep()
{
    // Guard against null context during shutdown
    if (!_context)
        return;

    _context->pScreen->UpdateScreen();  // Trigger screen update after each CPU command cycle

    _context->pBetaDisk->handleStep();
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

    // Notify that video frame is composed and ready for rendering
    // Send per-instance frame refresh event with emulator ID for filtering
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

void MainLoop::handleAudioBufferHalfFull([[maybe_unused]] int id, [[maybe_unused]] Message* message)
{
    std::unique_lock<std::mutex> lock(_audioBufferMutex);

    // Set the atomic variable to indicate frame sync
    _moreAudioDataRequested.store(true, std::memory_order_release);
    // Notify the main loop
    _cv.notify_one();
}

//
// Proceed with single frame CPU operations
//
void MainLoop::ExecuteCPUFrameCycle()
{
    _cpu->CPUFrameCycle();
}
