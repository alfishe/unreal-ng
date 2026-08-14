#pragma once
#include <atomic>
#include <chrono>

#include "3rdparty/message-center/eventqueue.h"
#include "common/logger.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "stdafx.h"

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
    std::atomic<bool> _isPausedConfirmed{false};  // Set by Z80 thread when actually paused
    std::condition_variable _pauseCV;              // Signaled when pause is confirmed
    std::mutex _pauseMutex;                        // Protects pause state

    std::atomic<bool> _moreAudioDataRequested;
    std::condition_variable _cv;
    std::mutex _audioBufferMutex;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    MainLoop() = delete;  // Disable default constructor. C++ 11 or better feature
    MainLoop(EmulatorContext* context);
    virtual ~MainLoop();
    /// endregion </Constructors / destructors>

public:
    void Run(volatile bool& exit);
    void Stop();

    /// @brief Returns true when the emulation thread has actually parked in the pause loop
    /// (Emulator::Pause() only sets a flag; the current frame still finishes executing)
    bool IsPauseConfirmed() const { return _isPausedConfirmed.load(std::memory_order_acquire); }

    /// @brief Block until the emulation thread confirms it parked in the pause loop
    /// @param timeoutMs Maximum time to wait, in milliseconds
    /// @return true if pause was confirmed within the timeout
    /// @note Never call from the emulation thread itself - it would deadlock until timeout.
    ///       May time out legitimately when execution is paused inside a frame
    ///       (e.g. breakpoint hit), since the pause loop is only reached at frame end.
    bool WaitForPauseConfirmation(uint32_t timeoutMs);

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

/// CUT (Component Under Test) wrapper for unit testing
/// Exposes protected and private methods for direct testing access
class MainLoop_CUT : public MainLoop
{
public:
    using MainLoop::MainLoop;  // Inherit constructors
    using MainLoop::RunFrame;
    using MainLoop::ExecuteCPUFrameCycle;
};