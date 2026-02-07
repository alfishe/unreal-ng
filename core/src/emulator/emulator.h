#pragma once

#ifndef _INCLUDED_EMULATOR_H_
#define _INCLUDED_EMULATOR_H_

#include "stdafx.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <string>

#include "base/featuremanager.h"
#include "common/autoresetevent.h"
#include "common/uuid.h"
using unreal::UUID;  // Explicitly bring into scope to avoid Windows GUID typedef collision
#include "corestate.h"
#include "cpu/z80.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/mainloop.h"
#include "emulatorcontext.h"


class BreakpointManager;

/// region <Types>

enum EmulatorStateEnum : uint8_t
{
    StateUnknown = 0,
    StateInitialized,
    StateRun,
    StatePaused,
    StateResumed,
    StateStopped,
    StateDestroying  // Prevents new operations during destruction
};

inline const char* getEmulatorStateName(EmulatorStateEnum value)
{
    static const char* names[] = {"StateUnknown", "StateInitialized", "StateRun",       "StatePaused",
                                  "StateResumed", "StateStopped",     "StateDestroying"};

    return names[value];
};

/// endregion </Types>

class Emulator
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_GENERIC;

    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    // Emulator identity
    UUID _uuid;                                           // Auto-generated UUID
    std::string _emulatorId;                              // Symbolic representation  of the UUID
    std::string _symbolicId;                              // Optional user-provided symbolic ID
    std::chrono::system_clock::time_point _createdAt;     // When instance was created
    std::chrono::system_clock::time_point _lastActivity;  // When last operation was performed
    EmulatorStateEnum _state = StateUnknown;
    mutable std::mutex _stateMutex;

    std::atomic<bool> _initialized{false};
    mutable std::mutex _mutexInitialization;

    std::thread* _asyncThread = nullptr;

    LoggerLevel _loggerLevel = LoggerLevel::LogTrace;
    EmulatorContext* _context = nullptr;

    Config* _config = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    MainLoop* _mainloop = nullptr;
    DebugManager* _debugManager = nullptr;
    BreakpointManager* _breakpointManager = nullptr;
    FeatureManager* _featureManager = nullptr;  // Feature toggle manager

    // Control flow
    volatile bool _stopRequested = false;

    // Emulator state
    volatile bool _isPaused = false;
    std::atomic<bool> _isRunning{false};  // Atomic to support idempotent Stop()
    volatile bool _isDebug = false;
    volatile bool _isReleased = false;

    // Step-over synchronization
    AutoResetEvent _stepOverSyncEvent;
    uint16_t _pendingStepOverBpId = 0;                  // Track active step-over breakpoint for cleanup
    std::vector<uint16_t> _stepOverDeactivatedBps;      // Breakpoints deactivated during step-over

    // Frame step target (persistent to prevent cumulative drift)
    unsigned _frameStepTargetPos = 0;                   // Target t-state position within frame
    bool _hasFrameStepTarget = false;                   // Whether target has been set
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    explicit Emulator(LoggerLevel level);
    explicit Emulator(const std::string& symbolicId, LoggerLevel level = LoggerLevel::LogTrace);
    virtual ~Emulator();
    /// endregion </Constructors / destructors>

private:
    void ReleaseNoGuard();

public:
    // Initialization operations
    [[nodiscard]] bool Init();
    void Release();

    // Timestamp helpers
    void UpdateLastActivity();
    std::chrono::system_clock::time_point GetCreationTime() const;
    std::chrono::system_clock::time_point GetLastActivityTime() const;
    std::string GetUptimeString() const;

    // ID management
    UUID GetUUID() const;
    std::string GetSymbolicId() const;
    void SetSymbolicId(const std::string& symbolicId);

    // Info methods
    void GetSystemInfo();

    // Performance management
    BaseFrequency_t GetSpeed();
    void SetSpeed(BaseFrequency_t speed);
    void SetSpeedMultiplier(uint8_t multiplier);
    void EnableTurboMode(bool withAudio = false);
    void DisableTurboMode();
    bool IsTurboMode() const;

    // Integration interfaces
    EmulatorContext* GetContext();
    ModuleLogger* GetLogger();
    MainLoop* GetMainLoop();
    Memory* GetMemory();
    DebugManager* GetDebugManager();
    BreakpointManager* GetBreakpointManager();
    FramebufferDescriptor GetFramebuffer();
    void SetAudioCallback(void* obj, AudioCallback callback);
    void ClearAudioCallback();

    // Emulator control cycle
    void Reset();
    void Start();
    void StartAsync();
    void Pause(bool broadcast = true);   // broadcast=false for internal operations (won't trigger UI updates)
    void Resume(bool broadcast = true);  // broadcast=false for internal operations (won't trigger UI updates)
    void WaitWhilePaused();              // Block until resumed (used by breakpoint handlers)
    void Stop();

    // File format operations
    bool LoadSnapshot(const std::string& path);
    bool SaveSnapshot(const std::string& path);
    bool LoadTape(const std::string& path);
    bool LoadDisk(const std::string& path);

    // Controlled emulator behavior
    void RunSingleCPUCycle(bool skipBreakpoints = true);
    void RunNCPUCycles(unsigned cycles, bool skipBreakpoints = false);
    void RunFrame(bool skipBreakpoints = true);                   // Run until next frame boundary
    void RunNFrames(unsigned frames, bool skipBreakpoints = true); // Run N complete frames
    void StepOver();                                              // Execute instruction, skip calls and subroutines

    // Cancel any pending step-over breakpoint (cleanup before starting a new step command)
    void CancelPendingStepOver();

    // Atomic debug stepping — zero overhead in non-debug mode (never called from hot path)
    void RunTStates(unsigned tStates, bool skipBreakpoints = true);           // Run exact N t-states (1 = ULA step / 2 pixels)
    void RunUntilScanline(unsigned targetLine, bool skipBreakpoints = true);  // Run until scanline N boundary
    void RunNScanlines(unsigned count, bool skipBreakpoints = true);          // Run N complete scanlines from current position
    void RunUntilNextScreenPixel(bool skipBreakpoints = true);                // Skip vblank/borders to first paper pixel
    void RunUntilInterrupt(bool skipBreakpoints = true);                      // Run until Z80 accepts maskable interrupt (iff1 1→0)
    void RunUntilCondition(std::function<bool(const Z80State&)> predicate, unsigned maxTStates = 0);

    // Actions
    bool LoadROM(std::string path);

    // Debug methods
    void DebugOn();
    void DebugOff();

    Z80State* GetZ80State();

    // Identity and state methods
    const std::string& GetId() const;
    EmulatorStateEnum GetState();
    void SetState(EmulatorStateEnum state);

    // Status methods
    bool IsRunning();
    bool IsPaused();
    bool IsDestroying();  // Thread-safe check for destruction state
    bool IsDebug();
    std::string GetStatistics();
    std::string GetInstanceInfo();

    // Counters method
    void ResetCountersAll();
    void ResetCounter();

    FeatureManager* GetFeatureManager() const
    {
        return _featureManager;
    }
};

#endif
