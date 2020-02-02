#include "stdafx.h"

#include "common/logger.h"

#include "mainloop.h"
#include "common/timehelper.h"
#include <algorithm>

MainLoop::MainLoop(CPU* cpu, EmulatorContext* context)
{
	_cpu = cpu;
	_context = context;
}

MainLoop::~MainLoop()
{
	_cpu = nullptr;
	_context = nullptr;

	LOGDEBUG("MainLoop::~MainLoop()");
}

//
// Main emulator loop. Processes all events and executes CPU and video cycles
//
void MainLoop::Run(volatile bool& stopRequested)
{
	if (_cpu == nullptr || _context == nullptr)
	{
		LOGERROR("MainLoop::Run - _cpu and _context shouldn't be nullptr");
		return;
	}

	while (!stopRequested)
	{
		unsigned duration1 = measure_us(&MainLoop::RunFrame, this);

		LOGINFO("Frame recalculation time: %d us", duration1);
		sleep_us(15000U - std::min(duration1, 15000U));
	}
}

void MainLoop::Stop()
{

}

void MainLoop::RunFrame()
{
	// Prepare sound and video for next cycle
	InitSoundFrame();
	InitVideoFrame();

	// Execute CPU cycle
	ExecuteCPUFrameCycle();

	// Process external periphery devices

	// Flush all generated data and buffers

	// Render Video and Audio using host platform capabilities
	// RenderVideo();
	// RenderAudio();

	// Queue new frame data to Video/Audio encoding
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
	Screen& screen = *_context->pScreen;

	screen.InitFrame();
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
	CONFIG& config = _context->config;
	HOST& host = _context->host;

	static volatile uint64_t prev_time = rdtsc();

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