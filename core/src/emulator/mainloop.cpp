#include "stdafx.h"

#include "common/modulelogger.h"

#include "mainloop.h"
#include "common/timehelper.h"
#include <algorithm>

MainLoop::MainLoop(EmulatorContext* context)
{
	_context = context;
    _logger = context->pModuleLogger;

	_state = &_context->state;
	_cpu = _context->pCPU;
    _screen = _context->pScreen;

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
		    LOGINFO("Pause requested");

		    while (_pauseRequested)
            {
		        sleep_ms(20);
            }

		    continue;
        }
		/// endregion </Handle Pause>

		MLOGINFO("Frame recalculation time: %d us", duration1);
		sleep_us(15000U - std::min(duration1, 15000U));
	}

	MLOGINFO("Stop requested, exiting main loop");

    // Stop animation recording and finalize the file
    //gifAnimationHelper.StopAnimation();

    _isRunning = false;
}

void MainLoop::Stop()
{
    _stopRequested = true;
}

void MainLoop::Pause()
{
    _pauseRequested = true;
}

void MainLoop::Resume()
{
    _pauseRequested = false;
}

void MainLoop::RunFrame()
{
    Screen& screen = *_context->pScreen;
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

	// Prepare sound and video for next cycle
	InitSoundFrame();
	InitVideoFrame();

	// Execute CPU cycles for single video frame
	ExecuteCPUFrameCycle();

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
        uint32_t* buffer;
        size_t size;
        screen.GetFramebufferData(&buffer, &size);
        //gifAnimationHelper.WriteFrame(buffer, size);
    }
	i++;

    if (i >= 2000)
    {
        //gifAnimationHelper.StopAnimation();

        //exit(1);
    }

	// DEBUG: save frame to disk as image

	// Notify that frame is composed and ready for rendering
    messageCenter.Post(NC_LOGGER_FRAME_REFRESH);
}

//
// Init all sound devices before rendering next frame
//
void MainLoop::InitSoundFrame()
{
}

//
// Init platform specific video capabilities before rendering next frame
//
void MainLoop::InitVideoFrame()
{
	_screen->InitFrame();
}

//
// Proceed with single frame CPU operations
//
void MainLoop::ExecuteCPUFrameCycle()
{
	_cpu->CPUFrameCycle();
}

//
// Holds execution of current thread until next frame
// Note: uses SSE2 pause CPU instruction (_mm_pause instrinc) to achieve ultimate time resolution at the cost of CPU core utilization (although with low power consumption)
//
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