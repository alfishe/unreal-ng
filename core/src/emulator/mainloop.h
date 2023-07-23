#pragma once
#include "stdafx.h"

#include <atomic>
#include "common/logger.h"
#include "common/image/gifanimationhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

class MainLoop : public Observer
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_MAINLOOP;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
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

    std::atomic<bool> _moreAudioDataRequested;
    std::condition_variable _cv;
    std::mutex _audioBufferMutex;


    GIFAnimationHelper gifAnimationHelper;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
	MainLoop() = delete;	// Disable default constructor. C++ 11 or better feature
	MainLoop(EmulatorContext* context);
	virtual ~MainLoop();
    /// endregion </Constructors / destructors>

public:
	void Run(volatile bool& exit);
	void Stop();

	void Pause();
	void Resume();

protected:
	void RunFrame();
	void ExecuteCPUFrameCycle();

    /// region <Event handlers>
public:
    void OnFrameStart();
    void OnCPUStep();
    void OnFrameEnd();
    /// endregion </Event handlers>

public:
    void handleAudioBufferHalfFull(int id, Message* message);
};