#include "stdafx.h"

#include "mainloop.h"

MainLoop::MainLoop(CPU* cpu, EmulatorContext* context)
{
	_cpu = cpu;
	_context = context;
}

MainLoop::~MainLoop()
{
	_cpu = nullptr;
	_context = nullptr;
}

//
// Main emulator loop. Processes all events and executes CPU and video cycles
//
void MainLoop::Run(const bool& exit)
{
	while (!exit)
	{
		RunFrame();
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
	ExecuteCPUFrame();

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
}

//
// Proceed with single frame CPU operations
//
void MainLoop::ExecuteCPUFrame()
{

}