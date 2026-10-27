#pragma once

#ifndef _INCLUDED_EMULATOR_H_
#define _INCLUDED_EMULATOR_H_

#include "stdafx.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/featuremanager.h"
#include "common/autoresetevent.h"
#include "common/uuid.h"
#include "corestate.h"
#include "cpu/z80.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/config.h"
#include "emulator/video/screen.h"  // For DisplayViewport
#include "emulator/cpu/core.h"
#include "emulator/execution/executionarbiter.h"
#include "emulator/mainloop.h"
#include "emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/rzx/rzxsession.h"
#include "loaders/snapshot/snapshotcapture.h"
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/snapshotreport.h"


class BreakpointManager;

namespace ttd
{
enum class TTDGuardedAction : uint8_t;  // debugger/ttd/timetravelmanager.h
class TTDSessionPort;                   // debugger/ttd/ttdsessionport.h
}

namespace coordinator  // emulator/coordinator/
{
class MachineCoordinator;
class IMachineOps;
class OperationLog;
class OpStage;
}

/// region <Types>

enum EmulatorStateEnum : uint8_t
{
    StateUnknown = 0,
    StateInitialized,
    StateRun,
    StatePaused,
    StateResumed,
    StateStopped,
    StateDestroying,  // Prevents new operations during destruction
    StateRetired      // Removed from the manager: stopped, detached, refuses changes; alive until its last holder lets go
};

inline const char* getEmulatorStateName(EmulatorStateEnum value)
{
    static const char* names[] = {"StateUnknown", "StateInitialized", "StateRun",       "StatePaused",
                                  "StateResumed", "StateStopped",     "StateDestroying", "StateRetired"};

    return names[value];
};

/// endregion </Types>

class FloppyDriveSlots;
enum class FrontPanelSwitch : uint8_t;  // emulator/ports/portdecoder.h
class TapeSlot;

class Emulator : public ISoftResetSink
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
    unreal::UUID _uuid;                                           // Auto-generated UUID
    std::string _emulatorId;                              // Symbolic representation  of the UUID
    std::string _symbolicId;                              // Optional user-provided symbolic ID
    std::chrono::system_clock::time_point _createdAt;     // When instance was created
    std::chrono::system_clock::time_point _lastActivity;  // When last operation was performed
    // Written under _stateMutex; atomic for the unlocked "being destroyed" checks of the API entry points
    std::atomic<EmulatorStateEnum> _state{StateUnknown};
    mutable std::mutex _stateMutex;

    std::atomic<bool> _initialized{false};
    mutable std::mutex _mutexInitialization;

    std::thread* _asyncThread = nullptr;

    LoggerLevel _loggerLevel = LoggerLevel::LogTrace;
    EmulatorContext* _context = nullptr;

    // Programmatically-requested machine model (CreateEmulatorWithModel):
    // applied by Init() right after config load, overriding the INI's
    // HIMEM/RamSize selection before any model-dependent subsystem initializes
    bool _hasPreferredModel = false;
    MEM_MODEL _preferredModel = MM_PENTAGON;
    uint32_t _preferredRamSize = 0;
    std::function<void(CONFIG&)> _configOverride;
    std::string _customConfigPath;  // Optional custom config file path
    std::string _initError;         // why Init() refused the configuration ("" for other failures)

    Config* _config = nullptr;
    Core* _core = nullptr;
    FloppyDriveSlots* _floppySlots = nullptr;  // fdd.a-d registered with the media manager while the drives exist
    TapeSlot* _tapeSlot = nullptr;             // "tape", registered while the deck exists
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    MainLoop* _mainloop = nullptr;
    DebugManager* _debugManager = nullptr;
    BreakpointManager* _breakpointManager = nullptr;
    FeatureManager* _featureManager = nullptr;  // Feature toggle manager
    std::atomic<bool> _hiddenGroupMember{false};  // see SetHiddenGroupMember
    std::mutex _speedInterceptorMutex;
    std::function<bool(uint8_t)> _speedInterceptor;  // see SetSpeedChangeInterceptor

    // RZX playback, created on first use. _rzxMutex serializes the commands
    // (play, stop, seek: a seek may play for seconds); _rzxSessionMutex only
    // guards the pointer, so a status read never waits for a command
    mutable std::mutex _rzxMutex;
    mutable std::mutex _rzxSessionMutex;
    std::unique_ptr<rzx::RzxSession> _rzxSession;
    rzx::RzxSession& RzxSessionLocked();
    /// The common body of the snapshot loads: RZX stop, TTD guard, pause,
    /// `load`, frame restart, resume, NC_FILE_LOADED
    /// Pause, run `load` and restart the frame. `load` receives the options to plan with: the caller's, plus a beforeCommit that ends
    /// the TTD recording session when (and only when) the plan has decided the snapshot will be committed
    bool LoadSnapshotStaged(const std::function<bool(std::string& error, const snapshot::Options& planned)>& load,
                            const std::string& openedPath, const snapshot::Options& options);
    /// What the snapshot pipeline did with the last load (empty before the first one)
    snapshot::Report _lastSnapshotReport;
    mutable std::mutex _snapshotPathMutex;   ///< guards coreState.snapshotFilePath

    // The machine state coordinator (docs/inprogress/2026-10-09-machine-state-coordinator/): every operation on the
    // whole machine goes through it. Created with the emulator, before every participant; WireCoordinator links it
    class CoordinatorPorts;   ///< the emulator's ports: the machine thread, the clock, the owner of its own operations
    struct SnapshotRequest;
    struct AutostartRequest;
    std::unique_ptr<coordinator::MachineCoordinator> _coordinator;
    std::unique_ptr<CoordinatorPorts> _coordinatorPorts;
    std::unique_ptr<ttd::TTDSessionPort> _ttdSessionPort;
    /// Every link between the coordinator and the participants, in one place (architecture §3.3). Called again when
    /// a participant arrives (the media manager in Init); owners already linked are left as they are
    void WireCoordinator();
    /// The emulator's own operations, run by the coordinator (stage: where the TTD session effect lands)
    void ResetNow(bool hardReset, coordinator::OpStage& stage);
    bool AutostartNow(AutostartRequest& request, coordinator::OpStage& stage);
    bool LoadSnapshotNow(SnapshotRequest& request, coordinator::OpStage& stage);
    snapshot::SaveResult _lastSaveResult;

    // Control flow
    std::atomic<bool> _stopRequested{false};  // Stop() on any thread, the loop polls it

    /// Direct stepping (RunNFrames, RunTStates, RunUntil*, ...) drives the Z80 on the caller's thread while
    /// the emulation thread is parked. A GUI that reads the machine (debugger, memory views) must not do so
    /// meanwhile: the memory map changes under it. Depth, not a flag: the calls may nest
    std::atomic<int> _directStepDepth{0};
    /// The outermost direct run's CPU hold and its thread: at each frame boundary it lends the CPU to the holds
    /// queued behind it (ExecutionArbiter::LendCpu) - a TTD stop, a save, a reader do not wait for a long run_frames
    const execution::HoldToken* _lendingHold = nullptr;
    std::thread::id _lendingThread;
    // The debugger snapshot's change counter and the registers at the previous stop (tdd 2026-10-04-debugger-snapshot
    // §4.5): updated at stops, run starts and tool edits, never per instruction
    std::atomic<uint64_t> _debugSeq{0};
    std::mutex _debugSeqMutex;              ///< WaitDebugChange's condition variable only
    std::condition_variable _debugSeqChanged;
    mutable std::mutex _prevStopMutex;
    Z80State _prevStopState{};
    bool _hasPrevStop = false;

public:
    /// A breakpoint stop: which breakpoint, where (PC, memory address or port) and on what access
    struct BreakpointStop
    {
        bool hit = false;
        uint16_t breakpointId = 0xFFFF;
        uint16_t address = 0;
        BreakpointHitKind kind = BreakpointHitKind::Execute;
        /// Whose breakpoint: another CPU's (a card) stopped the machine at that CPU's instruction `cpuPc`
        DebugCpuId cpu = DebugCpuId::Main;
        uint16_t cpuPc = 0;
    };

    /// Why the emulator stopped last (the debugger snapshot's `pause`)
    struct DebugStop
    {
        enum class Reason : uint8_t
        {
            None,         ///< not stopped since it was created
            Pause,        ///< a pause request (a client, the GUI)
            Breakpoint,   ///< a breakpoint (`breakpoint`)
            Step,         ///< a direct step / run ended
        } reason = Reason::None;
        BreakpointStop breakpoint;
    };

private:
    DebugStop _lastStop;   // guarded by _prevStopMutex
    /// The breakpoint that stopped the current / last direct run (OnBreakpointHit)
    BreakpointStop _directStop;
    /// A breakpoint's pause: Pause() puts it into its NC_EMULATOR_STATE_CHANGE payload, then clears it
    BreakpointStop _pendingPauseCause;
    /// The owner of that breakpoint ("dezog", "gdb", "interactive"): the origin its pause carries
    std::string _pendingPauseOwner;
    /// The emulator is stopped at an execution breakpoint at this PC (nothing executed since)
    bool _stoppedAtExecBreakpoint = false;
    uint16_t _stoppedAtExecPc = 0;
    /// The direct run's first instruction may pass the execution breakpoint the emulator is stopped at
    bool _passExecBreakpointArmed = false;
    uint16_t _passExecBreakpointPc = 0;
    /// A direct run's loops end on a stop request or on a breakpoint stop
    bool RunHalted() const
    {
        return _stopRequested || _directStop.hit || _arbiter.ServingAborted() || t_directRunRefused;
    }
    /// This thread's direct run did not get the CPU (a deadlock, not a wait: 60 s): it does not run at all, rather
    /// than drive the Z80 next to the holder
    static inline thread_local bool t_directRunRefused = false;
    /// The last direct run ended because it was aborted (the user took control back from an automation session)
    std::atomic<bool> _lastDirectRunAborted{false};
    /// Marks a direct-stepping call for its duration and holds the host audio output meanwhile (a direct run is
    /// never paced to real time: the machine computes the same samples, the speakers get nothing, as while
    /// paused); the last one out posts NC_EXECUTION_CPU_STEP so a GUI that
    /// skipped updates meanwhile refreshes once, at the end. The first one in starts a new run: no breakpoint
    /// stop yet, and the first instruction may leave the execution breakpoint the emulator is stopped at
    class DirectStepScope
    {
    public:
        /// `tag`: the command as the user knows it ("run_frames"): a pause queued behind it names it
        DirectStepScope(Emulator& emulator, const char* tag);
        ~DirectStepScope();
        DirectStepScope(const DirectStepScope&) = delete;
        DirectStepScope& operator=(const DirectStepScope&) = delete;

    private:
        Emulator& _emulator;
        /// The CPU for this run (execution arbiter, design §6 X2): a driving command. Outermost, it parks the main
        /// loop and applies the pause ("stepping leaves the machine paused"); nothing else is served meanwhile - a
        /// Resume() or a silent pause from another thread waits for its end. Nested (a step inside a silent pause,
        /// on the loop's own thread) it is inline and leaves the intent alone
        execution::HoldToken _hold;
        /// This scope is a new driver of the Z80 (not nested in one that already drives)
        bool _driver = false;
        /// This scope's hold is the one lent between frames (Emulator::_lendingHold)
        bool _lends = false;
        /// It did not get the CPU: the run does not step (t_directRunRefused)
        bool _refused = false;
        /// The host audio hold this direct run takes (reason DirectRun): it is not paced to real time, so its
        /// frames must not reach the speakers sped up. Taken after the depth marks the run active, released before
        /// it unmarks it, so Resume's reconcile never sees this hold without its run
        SoundManager::HostOutputHold _hostHold;
    };

    // Emulator state
    /// Decides whether the Z80 executes: intents (pause / run), commands that hold the CPU, the main loop's parking
    /// (docs/inprogress/2026-10-09-execution-arbiter/design.md). Pause() / Resume() are its compat front
    execution::ExecutionArbiter _arbiter;
    /// StartAsync submitted the start's Run intent for the Start() its thread runs
    bool _startIntentSubmitted = false;
    /// The breakpoint cause of each Pause edge, by edge seq: taken at the edge (gate), posted with its broadcast
    std::mutex _edgeCausesMutex;
    std::vector<std::pair<uint64_t, BreakpointStop>> _edgeCauses;
    /// At the moment an intent edge applies, before the machine can run (under the arbiter's lock)
    void OnIntentEdgeGate(const execution::IntentEdge& edge, bool parked);
    /// After it applied, outside the lock: state and broadcasts
    void OnIntentEdge(const execution::IntentEdge& edge);
    /// Threads executing this machine's Z80 right now: the main loop between its parks, or a direct run. More than
    /// one is a bug (the one the arbiter removed); counted, logged, asserted by the tests
    std::atomic<int> _activeDrivers{0};
    std::atomic<uint64_t> _driverViolations{0};
    std::atomic<bool> _isRunning{false};  // Atomic to support idempotent Stop()
    // Serializes Stop(): a caller that loses the _isRunning CAS below must still
    // block until the thread that won it has actually joined, not just signaled.
    // Without this, IsRunning() goes false (and _asyncThread->join() can still be
    // in flight) before the emulation thread has left MainLoop::Run() - a second
    // caller that skips its own Stop() because IsRunning() already reads false
    // (e.g. EmulatorManager::RemoveEmulatorInstance()) could free Core's Screen /
    // SoundManager / TimeTravelManager while that thread is still mid-frame.
    std::mutex _stopMutex;
    volatile bool _isDebug = false;
    std::atomic<bool> _isReleased{false};

    // Context leases (LeaseContext): readers hold _leaseMutex shared; the
    // removal sets _retiring, then takes it exclusively before Stop()/Retire()
    mutable std::shared_timed_mutex _leaseMutex;
    std::atomic<bool> _retiring{false};
    /// Retire() has run: stopped, detached, refusing changes; the memory stays until the last shared_ptr goes
    std::atomic<bool> _isRetired{false};
    /// The emulation thread has left Start() (StopFor waits on it with a timeout instead of an unbounded join)
    std::mutex _loopExitMutex;
    std::condition_variable _loopExitCv;
    bool _loopExited = true;
    std::function<void()> _threadExitHook;
    std::atomic<bool> _romReloadPending{false};  ///< RequestRomReload: reread the ROM at the next Reset

    // Step-over synchronization
    AutoResetEvent _stepOverSyncEvent;
    /// The step over's temporary breakpoint while its run is under way (0: none). Its cleanup (the breakpoint
    /// removed, the breakpoints it deactivated back on, the feature flags restored) runs on the thread that claims
    /// this id by exchanging it with 0: the emulation thread when the breakpoint fires (OnBreakpointHit), a control
    /// thread when a step cancels it (after parking the machine), or the release. Before, a MessageCenter observer did
    /// it on the dispatcher thread while the emulation thread still read the breakpoint map
    /// (EmulatorStepOverObserver_Test.DestroyedEmulatorLeavesNoHandlerBehind crashed in GetBreakpointById)
    std::atomic<uint16_t> _pendingStepOverBpId{0};
    bool _stepOverRestoreDebugMode = false;             // feature flags to restore when the step over ends
    bool _stepOverRestoreBreakpoints = false;
    bool _stepOverOwnsBreakpoint = false;   // false: the step reuses a breakpoint of the user's, which stays
    /// A step over that steps across a CALL resumes the machine to a temporary breakpoint: the machine then runs
    /// paced to real time, but it is a debugger step and must be silent like every other step. The host output
    /// hold (reason DirectRun) lasts from that resume to the stop: the breakpoint, a cancel or any pause
    SoundManager::HostOutputHold _stepOverHostHold;
    std::vector<uint16_t> _stepOverDeactivatedBps;      // Breakpoints deactivated during step-over
    /// The cleanup of a step over whose breakpoint id this thread claimed; `restoreFeatures`: the run ended at its
    /// breakpoint (a cancel leaves the flags to the step that cancels it, as before)
    void FinishStepOver(uint16_t breakpointId, bool restoreFeatures);

    // Frame step target (persistent to prevent cumulative drift)
    unsigned _frameStepTargetPos = 0;                   // Target t-state position within frame
    bool _hasFrameStepTarget = false;                   // Whether target has been set

    // Line-step anchor (prevents horizontal drift when stepping by scanlines)
    // Stores the initial offset within a scanline; subsequent steps target the same offset.
    int _lineStepAnchorOffset = -1;                     // -1 = not set (first line-step will capture it)
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    explicit Emulator(LoggerLevel level);
    explicit Emulator(const std::string& symbolicId, LoggerLevel level = LoggerLevel::LogTrace);
    virtual ~Emulator();
    /// endregion </Constructors / destructors>

    /// ISoftResetSink: device-initiated resets (the ZX-Evo AVR). They run on
    /// the trigger's thread (keyboard dispatch / WebAPI) - the same
    /// external-thread contract as the GUI reset action
    void RequestSoftReset() { Reset(); }
    void RequestHardReset() { Reset(true); }

private:
    void RetireNoGuard();
    void ReleaseNoGuard();

public:
    // Initialization operations
    /// Request a specific machine model (applied during Init() right after
    /// config load, overriding the INI's HIMEM/RamSize). Must be called
    /// before Init(). Lifecycle intent lives here, not in CONFIG.
    void SetPreferredModel(MEM_MODEL model, uint32_t ramSize)
    {
        _preferredModel = model;
        _preferredRamSize = ramSize;
        _hasPreferredModel = true;
    }

    /// Adjust this instance's configuration after it is loaded and the
    /// preferred model is applied, before any device is created from it.
    /// Must be called before Init(). Per instance - unlike the process-wide
    /// Config::SetConfigLoadedHook - so one caller cannot change another
    /// instance's hardware (MachineStateTransfer fits the source's cards).
    void SetConfigOverride(std::function<void(CONFIG&)> configOverride)
    {
        _configOverride = std::move(configOverride);
    }
    /// The override this instance was created with (empty when none): a restart of the same machine with another
    /// slot set (SlotChange) applies it again, so a machine variant's board or a create-time option survives
    const std::function<void(CONFIG&)>& GetConfigOverride() const
    {
        return _configOverride;
    }

    /// Set a custom config file path. Must be called before Init().
    /// If set, this path is used instead of the default config search.
    void SetCustomConfigPath(const std::string& path)
    {
        _customConfigPath = path;
    }

    [[nodiscard]] bool Init();
    /// Retire (if not yet) and free everything: the context, Core, every device. For the last owner (~Emulator) and
    /// direct callers (tests, a failed Init). EmulatorManager never calls it: a removal retires, the last holder frees
    void Release();
    /// The removal's end (EmulatorManager, after Stop): the instance stops being a live machine. No emulation thread,
    /// no host input, no audio callback, no coordinator session; the arbiter refuses execution and the coordinator
    /// refuses machine-changing operations. Nothing is freed: a holder of a shared_ptr (a GUI handler that runs late,
    /// a WebAPI request in flight) keeps reading a stopped, valid machine. Idempotent
    void Retire();
    bool IsRetired() const { return _isRetired.load(); }
    /// The configuration loader of this instance (what the INI said beyond CONFIG: the media set, [MIDI] Bank=);
    /// nullptr before Init
    const Config* GetConfigLoader() const
    {
        return _config;
    }
    /// Why Init() refused the machine's configuration (the slot set's conflicts, ZX-bus slots Q8); "" when it did
    /// not, or failed for another reason
    const std::string& GetInitError() const
    {
        return _initError;
    }

    /// A hidden member of a multi-instance machine (a ZX-Poly slave): left out
    /// of instance listings, index lookup and "most recent" selection, but
    /// still addressable by its ID (debugger, WebAPI)
    void SetHiddenGroupMember(bool hidden) { _hiddenGroupMember = hidden; }
    bool IsHiddenGroupMember() const { return _hiddenGroupMember; }

    // Timestamp helpers
    void UpdateLastActivity();
    std::chrono::system_clock::time_point GetCreationTime() const;
    std::chrono::system_clock::time_point GetLastActivityTime() const;
    std::string GetUptimeString() const;

    // ID management
    unreal::UUID GetUUID() const;
    std::string GetSymbolicId() const;
    void SetSymbolicId(const std::string& symbolicId);

    // Info methods
    void GetSystemInfo();

    // Performance management
    BaseFrequency_t GetSpeed();
    void SetSpeed(BaseFrequency_t speed);
    bool SetSpeedMultiplier(uint8_t multiplier);
    /// Why the host speed may not change now ("" = it may): TTD keeps a history recorded at 1x
    std::string SpeedLockReason() const;

    /// A group that runs this instance in lockstep with others (the ZX-Poly
    /// master) takes host speed changes itself: SetSpeedMultiplier hands the
    /// validated multiplier to `interceptor`, which queues it for the next frame
    /// boundary, where the group gives it to every member at once. Without it
    /// a change written from another thread could reach the master one frame
    /// before the slaves. An empty function removes it
    void SetSpeedChangeInterceptor(std::function<bool(uint8_t)> interceptor);

    /// @brief Why a recording-destructive action is refused right now (empty when allowed) -
    /// ITimeTravelHooks::RecordingGuard: the TTD v1 backend's refusals, which the coordinator's owner of the
    /// emulator's operations reports (goes with v1). Surfaces ask the coordinator instead (Ask*)
    std::string RecordingGuard(ttd::TTDGuardedAction action) const;

    /// @brief Run an edit of the machine made by a tool (a script, a debugger surface: memory, registers, ports,
    /// sectors) as an operation of the coordinator (OpKind::ToolEdit, logged with @p source) so a TTD session stays
    /// consistent: while one exists the edit is recorded as an event of it (a replay applies it) and, from any
    /// thread but the emulation one, the machine is parked for it (the dirty-page tracker belongs to the emulation
    /// thread). Live, the edit runs as it comes. The edit must reach TTD itself: Memory::DirectWriteToZ80Memory for
    /// the CPU view, Memory::MarkRamPageEdited after writing a physical RAM page.
    /// Refused in the recorded past (a replay, a point of a stopped session's history; owner 2026-10-11): the edit
    /// does not run, the reason comes back (empty: done) for the surface to report
    std::string EditMemoryFromTool(const char* source, const std::function<void()>& edit);
    /// EditMemoryFromTool for a script: a refusal throws (std::runtime_error, the reason), so it is a script error
    void EditMemoryFromToolOrThrow(const char* source, const std::function<void()>& edit);
    /// A CPU register set by a tool (EditMemoryFromTool): false, with nothing changed or recorded, for an unknown
    /// register or a value above its maximum (Z80::SetRegisterValue), or refused (@p refusal: why)
    bool SetRegisterFromTool(const char* source, const std::string& name, uint16_t value,
                             std::string* refusal = nullptr);
    uint8_t GetSpeedMultiplier() const;
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
    /// @param occupancyFrames Optional ring-occupancy cell (stereo frames)
    ///        owned by the caller; enables the DRC rate controller
    /// @param deviceDescriptor Optional full device/ring descriptor
    ///        (audiodevicedescriptor.h) for realtime monitoring; owned by the
    ///        caller and must outlive the registration
    void SetAudioCallback(void* obj, AudioCallback callback,
                          const std::atomic<uint32_t>* occupancyFrames = nullptr,
                          const AudioDeviceDescriptor* deviceDescriptor = nullptr);

    /// Report the attached audio device's native sample rate (0 = no
    /// device). The DRC resampler converts core->device at this base ratio.
    /// A rate CHANGE (device hotplug/reroute) re-derives the core rate from
    /// the priority chain (runtime pin > device > [SOUND] CoreRate): without
    /// a pin the full sound pipeline re-rates at the next frame boundary;
    /// with a pin only the DRC base ratio follows the device.
    void SetAudioDeviceSampleRate(uint32_t rate);
    void ClearAudioCallback();

    /// Realtime device/ring monitoring state (nullptr = no device attached)
    const AudioDeviceDescriptor* GetAudioDeviceDescriptor() const;

    /// The machine state coordinator: run an operation on the whole machine, ask whether it would be refused (the
    /// reason every surface shows), the machine's mode
    coordinator::IMachineOps& MachineOps();
    /// The last operations on this machine: who did what, at which frame, with which result (always on)
    const coordinator::OperationLog& GetOperationLog() const;
    /// A media change a surface asks about before it prepares the change itself (AskMediaChange)
    enum class MediaChange : uint8_t
    {
        LoadTape,
        LoadDisk,
        CreateDisk
    };
    /// Why @p change would be refused right now (empty: it would go ahead), asked of the machine state coordinator:
    /// a refusal is logged with the caller's source and the sentence the operation itself gives. For a surface about
    /// to prepare the change (an upload, a staged file, a dialog). While a TTD recording runs the engine says empty
    /// (the change will be an event of the recording); TTD v1 refuses. @p drive: the floppy drive; @p what: the file
    std::string AskMediaChange(MediaChange change, uint8_t drive = 0, const std::string& what = {});
    /// Why replacing this machine by another model, or moving another machine's state into it, would be refused right
    /// now (empty: it would go ahead), asked of the machine state coordinator (a refusal is logged). @p what: for the
    /// log ("to TSL", "state from <id>")
    std::string AskModelSwitch(const std::string& what);

    // Emulator control cycle
    void Reset(bool hardReset = false);

    /// Pulse the Z80 NMI line - accepted at the next instruction boundary
    /// (11T, PC pushed, vector #0066, IFF2<-IFF1). Safe to call while running.
    void RequestNMI();

    /// "Magic button": Scorpion pages the Shadow Monitor at #0000 (#1FFD bit 1) and
    /// pulses NMI, so the handler at #0066 executes monitor code; Profi raises the
    /// CF_TRDOS DOS-latch (same effect as the #3Dxx M1 trap) when DS80=0, then
    /// pulses NMI. Every other model falls back to a plain NMI. Safe to call while
    /// running.
    void RequestMNI();

    /// A front-panel switch (FrontPanelSwitch, e.g. the Profi's TURBO). Set operates it as an outside input: through
    /// the TTD input journal (journaled while recording, refused while a recording replays), applied on the
    /// emulation thread. Returns false when the machine has no such switch or the input was refused. Get answers
    /// -1 when the machine has no such switch, else 0 / 1
    bool SetFrontPanelSwitch(FrontPanelSwitch sw, bool on);
    int GetFrontPanelSwitch(FrontPanelSwitch sw) const;

    void Start();
    void StartAsync();
    void Pause();   // a pause with the Internal origin; a surface uses Pause(Requester); a still CPU: CpuHold
    void Resume();  // a run with the Internal origin; a surface uses Resume(Requester)
    /// A pause asked by a surface (execution arbiter intent): GUI, WebAPI, CLI, scripts, GDB, DeZog, TTD. The origin
    /// is kept with the pause (who set it) and broadcast with it. Behind a command another thread serves (a direct
    /// run, a hold) it is queued and the call returns at once: Pending, the command named in `detail` - the pause
    /// applies when that ends (a pause never aborts a command). Otherwise it waits for the main loop to park, up to
    /// 5 s (a starved host)
    execution::Result Pause(const execution::Requester& requester, const std::string& reason = "pause");
    /// The same, but waiting for the pause behind a command too, up to `budget`; still queued then: Busy (it stays
    /// queued and applies when the command ends)
    execution::Result PauseAndWait(const execution::Requester& requester, std::chrono::milliseconds budget,
                                   const std::string& reason = "pause");
    /// A run asked by a surface. Refused while another surface holds the run-control claim; with `onlyIfOwner`,
    /// refused when the pause in force was set by somebody else (DeZog's session close, the TTD resume verb)
    execution::Result Resume(const execution::Requester& requester, bool onlyIfOwner = false);
    void WaitWhilePaused();              // Block until resumed (used by breakpoint handlers)
    /// Block until the Z80 thread has actually parked in response to a prior Pause().
    /// Returns true on confirmation, false on timeout. Callers that mutate emulator
    /// state (TTD seek/step, snapshot restore, memory patches) MUST call this between
    /// Pause() and the mutation to close the race in which the in-flight frame loop
    /// overwrites the freshly written state. See MainLoop::WaitForPauseConfirmation.
    bool WaitForPauseConfirmation(uint32_t timeout_ms = 1000);
    void Stop();
    /// Stop() bounded: false when the emulation thread has not left within @p timeout. The thread is then detached
    /// and still runs; the instance must never be freed (EmulatorManager keeps it as stuck)
    [[nodiscard]] bool StopFor(std::chrono::milliseconds timeout);
    /// Tests: runs on the emulation thread after the main loop has returned, before the thread reports its exit
    /// (holding it there makes a stuck thread for StopFor)
    void SetThreadExitHookForTest(std::function<void()> hook) { _threadExitHook = std::move(hook); }

    // File format operations
    /// `reportedPath`: the file named in the load notification and the core
    /// state instead of `path` (an RZX start snapshot written to a temporary file)
    /// `options.commit`: "" = the plan decides, "legacy" = today's commit, or a registered policy name
    bool LoadSnapshot(const std::string& path, const std::string& reportedPath = {},
                      const snapshot::Options& options = {});
    /// What loading this file would do on this machine, touching nothing: the image of the file (banks as hashes,
    /// registers, paging, extensions) and the plan's report (who would commit, or the refusal and why). False + `error`
    /// when the file is not a snapshot this emulator can read
    bool InspectSnapshot(const std::string& path, const snapshot::Options& options, StateNode& result,
                         std::string& error);
    /// The snapshot pipeline's report of the last load: the commit that ran (or the refusal and why), the verdicts that
    /// led to it, the format's per-block outcomes, warnings. Read it after the load returned
    const snapshot::Report& LastSnapshotReport() const { return _lastSnapshotReport; }
    /// The file of the last snapshot loaded or saved (empty before the first one). A load and a save may finish on
    /// different threads, and the status surfaces read it from theirs: a copy, under its own lock
    std::string SnapshotFilePath() const;
    void SetSnapshotFilePath(const std::string& path);
    /// A snapshot image already in memory (`extension`: sna, z80, szx), loaded
    /// like a file: paused, TTD rules, the frame restarted, NC_FILE_LOADED with
    /// `reportedPath`
    bool LoadSnapshotData(const std::vector<uint8_t>& data, const std::string& extension,
                          const std::string& reportedPath, const snapshot::Options& options = {});
    /// The raw load of an in-memory image into the machine, nothing around it
    /// (no pause, no TTD handling, no frame restart, no notification): for a
    /// caller that already owns the machine - an RZX snapshot block applied on
    /// the emulation thread. False with `error`
    bool ApplySnapshotData(const std::vector<uint8_t>& data, const std::string& extension, std::string& error,
                           const snapshot::Options& options = {});

    /// RZX playback (emulator/rzx/rzxsession.h): the recording's start snapshot
    /// is loaded, then every IN returns the recorded value and the interrupts
    /// follow the recorded fetch counts until the end, a desync or StopRzx().
    /// The machine continues live afterwards. ModelMismatch names the model to
    /// switch to (rzx::PlayResult::requiredModel)
    rzx::PlayResult PlayRzx(const std::string& path, const rzx::PlayerOptions& options = {});
    rzx::PlayResult PlayRzx(std::shared_ptr<const rzx::File> file, const std::string& sourcePath,
                            const rzx::PlayerOptions& options = {});
    bool StopRzx();
    /// Move the RZX playback to the boundary after `frame` frames (keyframes
    /// make a seek back cost at most one keyframe interval of play)
    bool SeekRzx(uint64_t frame, std::string* error = nullptr);
    bool IsRzxPlaying() const;
    rzx::SessionStatus GetRzxStatus() const;
    /// The RZX session when a recording was played on this machine (its player
    /// kept after the end), else null: time travel records its position
    rzx::RzxSession* LoadedRzxSession();
    /// `ext` (no dot, any case) is an RZX recording
    static bool IsRzxExtension(const std::string& ext);

    /// Which time-travel implementation records and replays on instances
    /// created from now on (Phase 5): the engine's controller (the default) or
    /// v1's manager. Both objects exist on every instance; the selected one is
    /// what the core (hooks, write sink) and the verbs (TTDControl) drive. The
    /// core-tests binary selects V1 (v1's own tests, the reference); tests of
    /// the engine select it per test
    enum class TimeTravelBackend : uint8_t
    {
        Engine,
        V1
    };
    static void SetDefaultTimeTravelBackend(TimeTravelBackend backend);
    /// Where the engine writes each recording as it records (a folder per session under it,
    /// ttdrecordingfolders.h) for instances created from now on. The application sets
    /// ~/.unreal-ng/ttd (ttd::RecordingsRoot); empty (the default, tests): nothing is written
    static void SetDefaultTtdRecordingRoot(const std::string& root);
    static TimeTravelBackend DefaultTimeTravelBackend();
    /// Save the machine's snapshot view as .sna, .z80 or .szx (by the extension): pauses, waits until the machine has parked, writes.
    /// False = refused or failed, and LastSaveResult() says why (the machine's state is untouched); nothing is written then
    bool SaveSnapshot(const std::string& path);
    /// The outcome of the last SaveSnapshot: the format, what the file says it was made on, the refusal reason with a
    /// machine-readable `needs`, warnings
    const snapshot::SaveResult& LastSaveResult() const { return _lastSaveResult; }
    /// Which snapshot formats the machine can be saved in right now and why not (reasons, what would work instead)
    snapshot::SaveFormats SnapshotSaveFormats();
    /// A tape file (any TapeLoaderRegistry format) or a folder into the tape
    /// slot, at once; the deck stops and plays the new tape from its start.
    /// While a TTD recording runs it is an event of the recording: it lands at the next frame boundary
    /// (TTD v1: refused)
    bool LoadTape(const std::string& path, std::string* error = nullptr);
    /// The tape out of the deck. While a TTD recording runs it is an event of the recording: it lands at the next frame
    /// boundary (TTD v1: refused)
    bool EjectTape(std::string* error = nullptr);
    /// `ext` (no dot, any case) is a format a tape loader reads
    static bool IsTapeExtension(const std::string& ext);
    /// @param drive Target floppy drive, 0-3 (A-D). Must name a drive this machine actually has;
    ///               anything else is a hard failure (see `error`), never a silent fallback to A.
    /// @param error When non-null and the call fails, receives a human-readable reason
    ///               (invalid/absent drive, file not found, unsupported extension, format-specific
    ///               loader errors). The Qt UI's own drag-and-drop / Open File flow always passes
    ///               drive 0 today (unreal-qt/src/mainwindow.cpp) - that is a UI default, not a
    ///               limitation of this method; every other caller (WebAPI, CLI, MCP, Lua, Python)
    ///               must pass the drive the caller actually asked for.
    /// While a TTD recording runs the insert is an event of the recording: it lands at the next frame boundary (TTD v1:
    /// refused)
    bool LoadDisk(const std::string& path, uint8_t drive = 0, std::string* error = nullptr);

    /// Take the disk out of `drive` (0-3, A-D) and free it; the other drives are untouched.
    /// @param force Eject even when the disk has unsaved writes (they are lost); without it a
    ///              dirty disk stays in and `error` says so
    /// @param error When non-null and the call fails, receives a human-readable reason
    /// An empty drive is not an error. Like LoadDisk, while a TTD recording runs it is an event of the recording: it
    /// lands at the next frame boundary (TTD v1: refused)
    bool EjectDisk(uint8_t drive, bool force = false, std::string* error = nullptr);

    /// Layout of a blank disk from CreateBlankDisk()
    enum class BlankDiskFormat
    {
        Auto,         ///< Plus3 on a +3 (its own controller), Unformatted elsewhere
        Unformatted,  ///< No sectors: for the machine's own FORMAT command (TR-DOS, +3DOS)
        Plus3,        ///< +3DOS: 9 x 512-byte sectors per track, formatted (filler #E5)
        Trdos,        ///< TR-DOS: 16 x 256-byte sectors per track, formatted with an empty catalogue (as FORMAT leaves it)
    };

    /// Parse "auto" / "unformatted" (raw) / "plus3" / "trdos" (case-insensitive); false for anything else
    static bool ParseBlankDiskFormat(const std::string& text, BlankDiskFormat& format);
    static const char* BlankDiskFormatName(BlankDiskFormat format);

    /// Create a blank disk and insert it into `drive`, owned like a loaded image (the previous image of
    /// the drive is released). The drive's path reads "<blank>" until the disk is saved.
    /// @param cylinders 40 or 80; 0 = the format's default (Unformatted / Trdos 80, Plus3 40)
    /// @param sides 1 or 2; 0 = the format's default (Unformatted / Trdos 2, Plus3 1)
    /// @param error When non-null and the call fails, receives a human-readable reason
    /// @param resolved When non-null, receives the format and geometry actually used
    /// While a TTD recording runs the insert is an event of the recording: it lands at the next frame boundary (TTD v1:
    /// refused)
    struct BlankDiskResult
    {
        BlankDiskFormat format = BlankDiskFormat::Unformatted;
        uint8_t cylinders = 0;
        uint8_t sides = 0;
    };
    bool CreateBlankDisk(uint8_t drive, BlankDiskFormat format = BlankDiskFormat::Auto, uint8_t cylinders = 0,
                         uint8_t sides = 0, std::string* error = nullptr, BlankDiskResult* resolved = nullptr);

    /// Outcome of AutostartDisk()
    struct DiskAutostartResult
    {
        bool mounted = false;  // Disk image was mounted in drive A
        bool started = false;  // Machine was reset into TR-DOS to run the disk
        std::string message;   // Human readable outcome
    };

    /// Mount a disk image and, when the machine can run TR-DOS, quick-reset straight into TR-DOS so the
    /// disk starts by itself (boot file, single BASIC program, or an injected commander). A machine without
    /// TR-DOS keeps running untouched: the disk is only mounted and the problem is reported (log + HUD)
    /// @param drive Must be 0 (A) - TR-DOS/Beta 128's own "RUN boot" convention only ever boots from drive
    ///              A, this is a hardware constraint, not a missing feature. Any other value is a hard
    ///              failure (`mounted=false`, `message` explains why) rather than a silent fallback to A.
    DiskAutostartResult AutostartDisk(const std::string& path, uint8_t drive = 0);

    /// Result of SaveDisk()
    struct DiskSaveResult
    {
        bool saved = false;          // Image is on disk (at savedPath)
        bool retargeted = false;     // The requested format refused the image; it was written as UDI instead
        std::string savedPath;       // Path actually written
        std::string reason;          // Refusal reason / error message
    };

    /// Save the disk image in drive `drive` (0..3).
    /// @param path   Target file; empty = the image's own file path (a disk from a folder, a Hobeta file or a
    ///               blank disk has none). The extension selects the format (trd, scl, fdi, udi, dsk, td0,
    ///               mgt / img, hfe, scp; anything else = trd); the disk then stands for that file.
    /// @param allowRetarget  When the selected format refuses the image (TRD / SCL hold only 16 x 256-byte
    ///               TR-DOS tracks, FDI drops FM / non-nominal tracks with a warning but does not refuse),
    ///               save losslessly to `<path without extension>.udi` instead, keep the original file untouched
    ///               and post NC_FDD_DISK_SAVE_RETARGETED with the reason.
    DiskSaveResult SaveDisk(uint8_t drive = 0, const std::string& path = std::string(), bool allowRetarget = true);

    // Supported file extensions (for UI file dialogs)
    static std::vector<std::string> SupportedSnapshotExtensions();
    static std::vector<std::string> SupportedTapeExtensions();
    static std::vector<std::string> SupportedDiskExtensions();

    // Controlled emulator behavior
    void RunSingleCPUCycle(bool skipBreakpoints = true);
    /// One instruction that always executes: the execution breakpoint at PC is passed (no hit, no count) as when
    /// stepping on from a stop on it; breakpoints and watchpoints the instruction reaches still fire (ZEsarUX's
    /// cpu-step, a debugger's step at a breakpoint it did not stop on)
    void StepPastBreakpointAtPc();
    /// Returns the instructions executed: fewer than asked when a breakpoint stopped the run (LastDirectStop)
    unsigned RunNCPUCycles(unsigned cycles, bool skipBreakpoints = false);
    void RunFrame(bool skipBreakpoints = true);                   // Run until next frame boundary
    void RunNFrames(unsigned frames, bool skipBreakpoints = true); // Run N complete frames (64-bit T-state budget: TStateRunBudget)
    void StepOver();                                              // Execute instruction, skip calls and subroutines
    void StepOut();                                               // Run until the current subroutine returns (SP-tracking)

    // Cancel any pending step-over breakpoint (cleanup before starting a new step command)
    void CancelPendingStepOver();

    // Atomic debug stepping — zero overhead in non-debug mode (never called from hot path)
    void RunTStates(uint64_t tStates, bool skipBreakpoints = true);           // Run exact N t-states (1 = ULA step / 2 pixels)
    void RunUntilScanline(unsigned targetLine, bool skipBreakpoints = true);  // Run until scanline N boundary
    void RunNScanlines(unsigned count, bool skipBreakpoints = true);          // Run N complete scanlines from current position (drift-free)
    void ResetLineStepAnchor();                                               // Clear scanline-step anchor (call when switching away from line stepping)
    void RunUntilNextScreenPixel(bool skipBreakpoints = true);                // Skip vblank/borders to first paper pixel
    void RunUntilInterrupt(bool skipBreakpoints = true);                      // Run until Z80 accepts maskable interrupt (iff1 1→0)
    /// notifyDebugger = false skips the NC_EXECUTION_CPU_STEP post: for machine-internal
    /// stepping (a ZX-Poly group advancing its slaves after every master instruction).
    /// skipBreakpoints = false: a breakpoint ends the run first (run to the cursor; LastDirectStop says which)
    void RunUntilCondition(std::function<bool(const Z80State&)> predicate, uint64_t maxTStates = 0,
                           bool notifyDebugger = true, bool skipBreakpoints = true);

    /// Start the current frame again after machine state was replaced from
    /// outside the frame flow (reset, snapshot load). See MainLoop::RestartFrame
    void RestartFrame();

private:
    /// The one stepping primitive of every Emulator::Run* path: a single
    /// Z80::StepInstruction and, when it reached the frame limit, the full
    /// frame boundary (Core::FinishCPUFrame + MainLoop::CompleteFrame) - the
    /// same sequence as the continuous main loop, so every path produces the
    /// same machine trajectory, TTD checkpoints and device frame calls.
    /// @param frameCompleted set when this step closed a frame
    Z80::StepResult ExecuteStep(bool skipBreakpoints, bool* frameCompleted = nullptr);

public:

    // Actions
    bool LoadROM(std::string path);
    /// Reread the configured ROM at the next Reset (a BIOS selection at runtime: SprinterBios). Reset
    /// stops TTD recording first; the reload invalidates the TTD session as LoadROM does
    void RequestRomReload() { _romReloadPending = true; }
    bool RomReloadPending() const { return _romReloadPending; }
    /// The symbol bundles (data/symbols/manifest.json, in the resources or next to the executable) that match the
    /// loaded ROM pages go into the label manager as sets of origin "bundle"; UNREAL_SYMBOL_BUNDLES=0 turns it off
    void ApplySymbolBundles();

    // Debug methods
    void DebugOn();
    void DebugOff();

    // Video mode methods
    /// @brief Enable/disable Pentagon overscan mode (384x304 with extra border)
    /// @param enable true to enable overscan, false for standard 352x288
    /// @return true if mode changed, false if model doesn't support overscan
    bool SetOverscanMode(bool enable);

    /// @brief Check if currently in overscan mode
    bool IsOverscanMode() const;

    /// @brief Set display viewport for cropping framebuffer
    /// Only meaningful in overscan mode (M_P384)
    void SetDisplayViewport(const DisplayViewport& viewport);

    /// @brief Get current display viewport
    const DisplayViewport& GetDisplayViewport() const;

    Z80State* GetZ80State();

    // Identity and state methods
    const std::string& GetId() const;
    EmulatorStateEnum GetState();

    /// Release() has run: the context and every subsystem are gone. Only the last owner (~Emulator) or a direct
    /// caller releases; a removed instance is retired instead (IsRetired) and stays readable while held
    bool IsReleased() const { return _isReleased; }

    /// A short-lived guarantee that the machine is live, not being removed: EmulatorManager::RemoveEmulator() waits
    /// (bounded) for the live leases before it stops and retires the instance. Empty (false) once the removal has
    /// begun or the instance is released - the holder then must not change the machine. Memory safety does not
    /// depend on it: the memory stays while a shared_ptr<Emulator> is held. Keep a lease for one handler at most,
    /// and never remove the leased instance (or wait for a thread that does) while holding it.
    class ContextLease
    {
    public:
        ContextLease() = default;
        ContextLease(ContextLease&& other) noexcept
            : _lock(std::move(other._lock)), _context(std::exchange(other._context, nullptr))
        {
        }
        ContextLease& operator=(ContextLease&& other) noexcept
        {
            _lock = std::move(other._lock);
            _context = std::exchange(other._context, nullptr);
            return *this;
        }
        ContextLease(const ContextLease&) = delete;
        ContextLease& operator=(const ContextLease&) = delete;

        EmulatorContext* get() const { return _context; }
        EmulatorContext* operator->() const { return _context; }
        explicit operator bool() const { return _context != nullptr; }

    private:
        friend class Emulator;
        ContextLease(std::shared_lock<std::shared_timed_mutex>&& lock, EmulatorContext* context)
            : _lock(std::move(lock)), _context(context)
        {
        }

        std::shared_lock<std::shared_timed_mutex> _lock;
        EmulatorContext* _context = nullptr;
    };
    ContextLease LeaseContext();

    /// The removal has begun (EmulatorManager): LeaseContext() refuses from now on
    bool IsRetiring() const { return _retiring.load(); }
    /// EmulatorManager, first step of a removal: refuse new leases
    void BeginRetirement();
    /// EmulatorManager, before Stop()/Retire(): wait until every lease taken
    /// before BeginRetirement() has ended. Hold no lock a lease holder may wait for
    void WaitForContextLeases();
    /// The same, bounded: false when a lease is still held after @p timeout. Safe for a removal, which then retires
    /// (frees nothing): the holder goes on reading a retired machine
    [[nodiscard]] bool WaitForContextLeases(std::chrono::milliseconds timeout);
    void SetState(EmulatorStateEnum state);

    // Status methods
    bool IsRunning();
    bool IsPaused();
    /// The execution arbiter (intents, commands, parking): the one place that decides whether the Z80 executes
    execution::ExecutionArbiter& GetArbiter() { return _arbiter; }
    /// One relaxed load: the main loop must park at this instruction boundary (Z80FrameCycle, MainLoop::Run)
    bool ShouldPark() const { return _arbiter.ShouldYield(); }
    /// A thread starts / stops executing the Z80 (MainLoop around a frame, a direct run). Two at once is a violation
    void DriverEnter();
    void DriverLeave();
    /// The last direct run (RunNFrames, RunTStates, ...) was cut short by an abort: the user took control back from the
    /// automation session that asked for it. The surfaces answer that call with revoked_by_user
    bool LastDirectRunAborted() const { return _lastDirectRunAborted.load(); }
    /// Moments two threads executed the Z80 at once (0 on a correct run)
    uint64_t DriverViolations() const { return _driverViolations.load(std::memory_order_relaxed); }
    /// The machine's state may be changed from this thread right now: not running, paused, or this thread holds the
    /// CPU (a CpuHold, a direct run, the emulation thread itself)
    bool CanMutateNow();

    /// The CPU held for a piece of work that changes the machine (execution arbiter command, design §6 X3): the main
    /// loop parked, no direct run on another thread, nothing else served meanwhile. It never changes the pause and
    /// never broadcasts - a Pause() or Resume() asked meanwhile queues and applies after it. Nested holds (this thread
    /// already holds the CPU) are free. Replaces the "if running: Pause(); sleep; ...; Resume()" brackets
    class CpuHold
    {
    public:
        CpuHold(Emulator& emulator, const char* tag,
                std::chrono::milliseconds timeout = std::chrono::seconds(5));
        ~CpuHold();
        CpuHold(const CpuHold&) = delete;
        CpuHold& operator=(const CpuHold&) = delete;
        /// False when the CPU was not handed over in time (logged): the work then runs as the old bracket did
        bool Held() const { return _held; }

    private:
        Emulator& _emulator;
        execution::HoldToken _token;
        bool _held = false;
    };

    /// True when no emulation thread is drawing: not running, or paused and confirmed parked (the pause loop
    /// was reached, not merely requested). Then the frame buffers and registers are safe to read from another
    /// thread. False while the thread runs, or between a pause request and its confirmation
    bool IsEmulationParked();
    /// Run `work` on the caller's thread while the emulation stays parked: a confirmed pause and no direct run on any
    /// thread. Resume() waits for it (the pause flag cannot flip meanwhile), so the caller is the only thread driving
    /// the machine. Returns false, without running `work`, when the machine is not parked. `work` must not pause,
    /// resume or step this emulator
    bool RunWhileParked(const std::function<void()>& work);
    /// A direct-stepping call is driving the Z80 on some thread right now (see DirectStepScope)
    bool IsDirectStepping() const { return _directStepDepth.load(std::memory_order_acquire) > 0; }

    /// The debugger snapshot's `seq`: grows at every stop, run start and tool edit of this emulator (equal = nothing
    /// changed for a debugger view)
    uint64_t DebugSeq() const { return _debugSeq.load(std::memory_order_acquire); }
    /// Something a debugger view shows changed (a stop, a tool's register or memory edit)
    void NoteDebugChange();
    /// Block until DebugSeq() differs from `since` or `timeoutMs` passes; returns DebugSeq() (debugger additions
    /// tdd §6: long-poll). NoteDebugChange wakes it
    uint64_t WaitDebugChange(uint64_t since, uint32_t timeoutMs);
    /// A run starts (resume, a direct step / run): the registers now become "the previous stop" (prev_regs), seq grows
    void NoteRunStart();
    /// The registers at the stop before the current one; false before the first run start
    bool PreviousStopRegisters(Z80State& out) const;
    /// Why it stopped last
    DebugStop LastStop() const;
    /// Run `work` on the emulation thread at the next frame boundary of a running machine (MainLoop::RunAtFrameBoundary);
    /// false when no frame boundary came within `timeoutMs` (paused, stopped) - `work` then never runs
    bool RunAtFrameBoundary(const std::function<void()>& work, uint32_t timeoutMs);
    /// Where RunAtCoherentMoment ran its work
    enum class CoherentMoment : uint8_t
    {
        Paused,   ///< on the caller's thread while the emulation stayed parked (RunWhileParked)
        Stopped,  ///< on the caller's thread: never started and nobody steps it
        Frame,    ///< on the emulation thread between two frames of a running machine
        Busy,     ///< nowhere: no such moment within the timeout (another client is stepping it)
    };
    /// Run `work` once where nothing else changes the machine meanwhile (the debugger snapshot, a tool's port
    /// write): paused, stopped or at a frame boundary, waiting up to `timeoutMs` for one of them
    CoherentMoment RunAtCoherentMoment(const std::function<void()>& work, uint32_t timeoutMs);
    /// "paused", "stopped", "frame" or "busy"
    static const char* CoherentMomentName(CoherentMoment moment);

    /// Every debugger breakpoint hit goes through here (the Z80's instruction start, memory reads and writes,
    /// port reads and writes). On the emulator's own run it pauses, notifies and parks the emulation thread
    /// until Resume(), as before. During a direct run (a control thread stepping a paused emulator: WebAPI,
    /// CLI, DeZog, Lua, Python) it must not park - nothing would resume the caller - so it records the stop
    /// (LastDirectStop), notifies, and the run ends after the current step. An execution breakpoint stops a
    /// direct run before its instruction: the return value true tells the Z80 not to execute it. The
    /// execution breakpoint the emulator is stopped at does not stop the run's first instruction (stepping
    /// on from a breakpoint)
    bool OnBreakpointHit(uint16_t breakpointId, uint16_t address, BreakpointHitKind kind);
    /// A breakpoint of another CPU (`breakpoints` that CPU's, gs-debugger design §4) at that CPU's instruction
    /// boundary: the machine pauses with both positions - the main CPU wherever it stands (inside a host port access
    /// or at the frame end), the other at `cpuPc` - and this thread parks until Resume(). A direct run records the
    /// stop and ends after its current instruction
    void OnTargetBreakpointHit(DebugCpuId cpu, BreakpointManager& breakpoints, uint16_t breakpointId, uint16_t address,
                               BreakpointHitKind kind, uint16_t cpuPc);
    /// The stop goes to the script callbacks that wait for this breakpoint (on_breakpoint), before the pause
    void NoteBreakpointCallbacks(uint16_t breakpointId, uint16_t address, BreakpointHitKind kind);
    /// The breakpoint that ended the last direct run early; hit = false when the run did all it was asked
    const BreakpointStop& LastDirectStop() const { return _directStop; }
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
