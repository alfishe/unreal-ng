#pragma once
#include "stdafx.h"

#include "common/logger.h"
#include "common/image/gifanimationhelper.h"
#include "cpu/cpu.h"
#include "emulator/emulatorcontext.h"

class MainLoop
{
protected:
    EmulatorContext* _context = nullptr;
    State* _state = nullptr;
    CPU* _cpu = nullptr;
    Screen* _screen = nullptr;


	volatile bool _isRunning = false;
	volatile bool _stopRequested = false;
	volatile bool _pauseRequested = false;

    GIFAnimationHelper gifAnimationHelper;

public:
	MainLoop() = delete;	// Disable default constructor. C++ 11 or better feature
	MainLoop(EmulatorContext* context);
	virtual ~MainLoop();

	void Run(volatile bool& exit);
	void Stop();

	void Pause();
	void Resume();

protected:
	void RunFrame();

	void InitSoundFrame();
	void InitVideoFrame();

	void ExecuteCPUFrameCycle();
	void DoIdle();
};