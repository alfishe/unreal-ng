#pragma once
#include <atomic>

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

    /// @brief Block until the Z80 thread has actually entered the paused state.
    ///
    /// Emulator::Pause() only sets a flag and returns immediately — the
    /// emulator thread notices it at the top of the next frame iteration
    /// and parks. Any caller that mutates emulator state right after
    /// Pause() (e.g. TTD seek/step-back/step-forward) MUST call this first
    /// to close the race window in which the in-flight frame loop could
    /// overwrite the freshly restored framebuffer /writethrough.
    ///
    /// Returns true if pause was confirmed before the timeout elapsed,
    /// false on timeout (caller should proceed anyway — the state mutation
    /// is still correct, just slightly racy).
    ///
    /// Safe to call when the emulator is not running async (e.g. tests,
    /// synchronous mode): in that case _isPausedConfirmed may never flip
    /// and the wait will time out, which is the correct behaviour.
    bool WaitForPauseConfirmation(uint32_t timeout_ms = 1000);

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