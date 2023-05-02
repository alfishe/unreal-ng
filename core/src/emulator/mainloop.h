#pragma once
#include "stdafx.h"

#include "common/logger.h"
#include "common/image/gifanimationhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

class MainLoop
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_MAINLOOP;
    /// endregion </ModuleLogger definitions for Module/Submodule>

protected:
    EmulatorContext* _context = nullptr;
    ModuleLogger* _logger;

    EmulatorState* _state = nullptr;
    Core* _cpu = nullptr;
    Screen* _screen = nullptr;
    SoundManager* _soundManager = nullptr;


	volatile bool _isRunning = false;
	volatile bool _stopRequested = false;
	volatile bool _pauseRequested = false;

    GIFAnimationHelper gifAnimationHelper;

public:
	MainLoop() = delete;	// Disable default constructor. C++ 11 or better feature
	MainLoop(EmulatorContext* context);
	virtual ~MainLoop();

public:
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