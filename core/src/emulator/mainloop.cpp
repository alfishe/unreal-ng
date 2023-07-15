#include "stdafx.h"

#include "common/modulelogger.h"

#include "mainloop.h"
#include <common/stringhelper.h>
#include "common/timehelper.h"
#include "3rdparty/message-center/eventqueue.h"
#include <algorithm>


MainLoop::MainLoop(EmulatorContext* context)
{
	_context = context;
    _logger = context->pModuleLogger;

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

	/// region <Debug>

	// Initialize animation
    FramebufferDescriptor& framebuffer = _screen->GetFramebufferDescriptor();
    //gifAnimationHelper.StartAnimation("unreal.gif", framebuffer.width, framebuffer.height, 20);

	/// endregion </Debug>

    static std::chrono::milliseconds timeout(20); // Set timeout for audio buffer refresh wait
    uint64_t lastRun = 0;
    uint64_t betweenIterations = 0;

	while (!stopRequested)
	{
        uint64_t startTime = TimeHelper::GetTimestampUs();
        betweenIterations = startTime - lastRun;

		unsigned duration1 = measure_us(&MainLoop::RunFrame, this);

		/// region <Handle Pause>
		if (_pauseRequested)
        {
		    MLOGINFO("Pause requested");

		    while (_pauseRequested)
            {
		        sleep_ms(20);
            }

		    continue;
        }
		/// endregion </Handle Pause>

        //MLOGINFO("Frame recalculation time: %d us", duration1);
        //std::cout << StringHelper::Format("Frame recalculation time: %d us", duration1) << std::endl;
        //std::cout << StringHelper::Format("Between iterations: %d us", betweenIterations) << std::endl;

        // Wait until audio callback requests more data and buffer is about half-full
        // That means we're in sync between audio and video frames
        std::unique_lock<std::mutex> lock(_audioBufferMutex);
        auto moreAudioDataRequested = std::ref(_moreAudioDataRequested);
        _cv.wait_for(lock, timeout, [&moreAudioDataRequested]{ return moreAudioDataRequested.get().load(std::memory_order_acquire); });
        _moreAudioDataRequested.store(false);
        lock.unlock();

        lastRun = startTime;
	}

	MLOGINFO("Stop requested, exiting main loop");

    // Stop animation recording and finalize the file
    gifAnimationHelper.StopAnimation();

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
#ifdef _DEBUG
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

	/// region <DEBUG: save frame to disk as image>

    static int i = 0;
	//if (i % 100 == 0)
	{
        //screen.RenderOnlyMainScreen();

        // Save frame if video memory was changed
        //if (_state->video_memory_changed)
        //    screen.SaveZXSpectrumNativeScreen();

        // Save frame to GIF animation
        Screen& screen = *_context->pScreen;
        uint32_t* buffer;
        size_t size;
        screen.GetFramebufferData(&buffer, &size);
        gifAnimationHelper.WriteFrame(buffer, size);
    }
	i++;

    if (i >= 2000)
    {
        //gifAnimationHelper.StopAnimation();

        //exit(1);
    }

    /// endregion <DEBUG: save frame to disk as image>
}

void MainLoop::OnFrameStart()
{
    _context->pTape->handleFrameStart();
    _soundManager->handleFrameStart();
    _screen->InitFrame();
}

void MainLoop::OnFrameEnd()
{
    // Update counters
    _context->emulatorState.t_states += _context->config.frame;

    // Trigger events for peripherals
    _context->pTape->handleFrameEnd();
    _context->pSoundManager->handleFrameEnd();  // Sound manager will call audio callback by itself

    // Notify that video frame is composed and ready for rendering
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_VIDEO_FRAME_REFRESH, new SimpleNumberPayload(_context->emulatorState.frame_counter));
}

void MainLoop::handleAudioBufferHalfFull(int id, Message* message)
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
