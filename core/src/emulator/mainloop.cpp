#include "stdafx.h"

#include "common/modulelogger.h"

#include "mainloop.h"
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

    _isRunning = false;
}

MainLoop::~MainLoop()
{
    if (_isRunning)
        Stop();

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

	/// region <Debug>

	// Initialize animation
    FramebufferDescriptor& framebuffer = _screen->GetFramebufferDescriptor();
    //gifAnimationHelper.StartAnimation("unreal.gif", framebuffer.width, framebuffer.height, 20);

	/// endregion </Debug>

	while (!stopRequested && !_stopRequested)
	{
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

		sleep_us(20000U - std::min(duration1, 20000U));
        MLOGINFO("Frame recalculation time: %d us", duration1);
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

	// DEBUG: save frame to disk as image (only each 100th)

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

	// DEBUG: save frame to disk as image


}

void MainLoop::OnFrameStart()
{
    _context->pTape->handleFrameStart();
    _soundManager->handleFrameStart();
    _screen->InitFrame();
}

void MainLoop::OnFrameEnd()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    _context->pTape->handleFrameEnd();
    _context->pSoundManager->handleFrameEnd();  // Sound manager will send NC_AUDIO_FRAME_REFRESH notification by itself

    // Notify that video frame is composed and ready for rendering
    messageCenter.Post(NC_VIDEO_FRAME_REFRESH, new SimpleNumberPayload(_context->emulatorState.frame_counter));
}


//
// Proceed with single frame CPU operations
//
void MainLoop::ExecuteCPUFrameCycle()
{
	_cpu->CPUFrameCycle();
}


/// Holds execution of current thread until next frame
/// Note: uses SSE2 pause CPU instruction (_mm_pause instrinc) to achieve ultimate time resolution at the cost of CPU core utilization (although with low power consumption)
void MainLoop::DoIdle()
{
	const CONFIG& config = _context->config;
	const HOST& host = _context->host;

	static volatile uint64_t prev_time = 0;

	prev_time = rdtsc();

	for (;;)
	{
		_mm_pause();

		volatile uint64_t cur_time = rdtsc();
		if ((cur_time - prev_time) >= host.ticks_frame)
			break;

		if (config.sleepidle)
		{
			uint32_t delay = uint32_t(((host.ticks_frame - (cur_time - prev_time)) * 1000ULL) / host.cpufq);

			if (delay > 0)
			{
				sleep_ms(delay);
				break;
			}
		}
	}

	prev_time = rdtsc();
}