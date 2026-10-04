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
#include <utility>

#include "base/featuremanager.h"
#include "common/autoresetevent.h"
#include "common/uuid.h"
#include "corestate.h"
#include "cpu/z80.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/config.h"
#include "emulator/video/screen.h"  // For DisplayViewport
#include "emulator/cpu/core.h"
#include "emulator/mainloop.h"
#include "emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/rzx/rzxsession.h"


class BreakpointManager;

namespace ttd
{
enum class TTDGuardedAction : uint8_t;  // debugger/ttd/timetravelmanager.h
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
    StateDestroying  // Prevents new operations during destruction
};

inline const char* getEmulatorStateName(EmulatorStateEnum value)
{
    static const char* names[] = {"StateUnknown", "StateInitialized", "StateRun",       "StatePaused",
                                  "StateResumed", "StateStopped",     "StateDestroying"};

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
    EmulatorStateEnum _state = StateUnknown;
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
    bool LoadSnapshotStaged(const std::function<bool(std::string& error)>& load, const std::string& openedPath);

    // Control flow
    volatile bool _stopRequested = false;

    /// Direct stepping (RunNFrames, RunTStates, RunUntil*, ...) drives the Z80 on the caller's thread while
    /// the emulation thread is parked. A GUI that reads the machine (debugger, memory views) must not do so
    /// meanwhile: the memory map changes under it. Depth, not a flag: the calls may nest
    std::atomic<int> _directStepDepth{0};

public:
    /// A breakpoint stop: which breakpoint, where (PC, memory address or port) and on what access
    struct BreakpointStop
    {
        bool hit = false;
        uint16_t breakpointId = 0xFFFF;
        uint16_t address = 0;
        BreakpointHitKind kind = BreakpointHitKind::Execute;
    };

private:
    /// The breakpoint that stopped the current / last direct run (OnBreakpointHit)
    BreakpointStop _directStop;
    /// A breakpoint's pause: Pause() puts it into its NC_EMULATOR_STATE_CHANGE payload, then clears it
    BreakpointStop _pendingPauseCause;
    /// The emulator is stopped at an execution breakpoint at this PC (nothing executed since)
    bool _stoppedAtExecBreakpoint = false;
    uint16_t _stoppedAtExecPc = 0;
    /// The direct run's first instruction may pass the execution breakpoint the emulator is stopped at
    bool _passExecBreakpointArmed = false;
    uint16_t _passExecBreakpointPc = 0;
    /// A direct run's loops end on a stop request or on a breakpoint stop
    bool RunHalted() const { return _stopRequested || _directStop.hit; }
    /// Marks a direct-stepping call for its duration and holds the host audio output meanwhile (a direct run is
    /// never paced to real time: the machine computes the same samples, the speakers get nothing, as while
    /// paused); the last one out posts NC_EXECUTION_CPU_STEP so a GUI that
    /// skipped updates meanwhile refreshes once, at the end. The first one in starts a new run: no breakpoint
    /// stop yet, and the first instruction may leave the execution breakpoint the emulator is stopped at
    class DirectStepScope
    {
    public:
        explicit DirectStepScope(Emulator& emulator);
        ~DirectStepScope();
        DirectStepScope(const DirectStepScope&) = delete;
        DirectStepScope& operator=(const DirectStepScope&) = delete;

    private:
        Emulator& _emulator;
        /// The host audio hold this direct run takes (reason DirectRun): it is not paced to real time, so its
        /// frames must not reach the speakers sped up. Taken after the depth marks the run active, released before
        /// it unmarks it, so Resume's reconcile never sees this hold without its run
        SoundManager::HostOutputHold _hostHold;
    };

    // Emulator state
    // _pauseWaitMutex guards _isPaused transitions so the parked CPU thread's
    // CV predicate (WaitWhilePaused) can never miss a Pause/Resume/Stop flip;
    // _resumeCV wakes it in microseconds instead of the legacy 20 ms poll.
    mutable std::mutex _pauseWaitMutex;
    std::condition_variable _resumeCV;
    volatile bool _isPaused = false;
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
    volatile bool _isReleased = false;

    // Context leases (LeaseContext): readers hold _leaseMutex shared; the
    // removal sets _retiring, then takes it exclusively before Stop()/Release()
    mutable std::shared_mutex _leaseMutex;
    std::atomic<bool> _retiring{false};
    std::atomic<bool> _romReloadPending{false};  ///< RequestRomReload: reread the ROM at the next Reset

    // Step-over synchronization
    AutoResetEvent _stepOverSyncEvent;
    uint16_t _pendingStepOverBpId = 0;                  // Track active step-over breakpoint for cleanup
    /// A step over that steps across a CALL resumes the machine to a temporary breakpoint: the machine then runs
    /// paced to real time, but it is a debugger step and must be silent like every other step. The host output
    /// hold (reason DirectRun) lasts from that resume to the stop: the breakpoint, a cancel or any pause
    SoundManager::HostOutputHold _stepOverHostHold;
    std::vector<uint16_t> _stepOverDeactivatedBps;      // Breakpoints deactivated during step-over

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

    /// Set a custom config file path. Must be called before Init().
    /// If set, this path is used instead of the default config search.
    void SetCustomConfigPath(const std::string& path)
    {
        _customConfigPath = path;
    }

    [[nodiscard]] bool Init();
    void Release();

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

    /// A group that runs this instance in lockstep with others (the ZX-Poly
    /// master) takes host speed changes itself: SetSpeedMultiplier hands the
    /// validated multiplier to `interceptor`, which queues it for the next frame
    /// boundary, where the group gives it to every member at once. Without it
    /// a change written from another thread could reach the master one frame
    /// before the slaves. An empty function removes it
    void SetSpeedChangeInterceptor(std::function<bool(uint8_t)> interceptor);

    /// @brief Why a recording-destructive action is refused right now (empty when
    /// allowed) - TimeTravelManager::RecordingGuard. The loaders below refuse with
    /// it themselves; surfaces ask first to report the reason.
    std::string RecordingGuard(ttd::TTDGuardedAction action) const;

    /// @brief Run a guest-memory edit made by a tool (a script, a debugger
    /// surface) so a TTD recording stays consistent: the edit is recorded as a
    /// debugger-edit marker (replay cannot reproduce it) and, from any thread but
    /// the emulation one, the emulator is parked for it (the dirty-page tracker
    /// belongs to the emulation thread). Nothing extra happens when not recording.
    /// The edit must reach TTD itself: Memory::DirectWriteToZ80Memory for the CPU
    /// view, Memory::MarkRamPageEdited after writing a physical RAM page.
    void EditMemoryFromTool(const char* source, const std::function<void()>& edit);
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
    void Pause(bool broadcast = true);   // broadcast=false for internal operations (won't trigger UI updates)
    void Resume(bool broadcast = true);  // broadcast=false for internal operations (won't trigger UI updates)
    void WaitWhilePaused();              // Block until resumed (used by breakpoint handlers)
    /// Block until the Z80 thread has actually parked in response to a prior Pause().
    /// Returns true on confirmation, false on timeout. Callers that mutate emulator
    /// state (TTD seek/step, snapshot restore, memory patches) MUST call this between
    /// Pause() and the mutation to close the race in which the in-flight frame loop
    /// overwrites the freshly written state. See MainLoop::WaitForPauseConfirmation.
    bool WaitForPauseConfirmation(uint32_t timeout_ms = 1000);
    void Stop();

    // File format operations
    /// `reportedPath`: the file named in the load notification and the core
    /// state instead of `path` (an RZX start snapshot written to a temporary file)
    bool LoadSnapshot(const std::string& path, const std::string& reportedPath = {});
    /// A snapshot image already in memory (`extension`: sna, z80, szx), loaded
    /// like a file: paused, TTD rules, the frame restarted, NC_FILE_LOADED with
    /// `reportedPath`
    bool LoadSnapshotData(const std::vector<uint8_t>& data, const std::string& extension,
                          const std::string& reportedPath);
    /// The raw load of an in-memory image into the machine, nothing around it
    /// (no pause, no TTD handling, no frame restart, no notification): for a
    /// caller that already owns the machine - an RZX snapshot block applied on
    /// the emulation thread. False with `error`
    bool ApplySnapshotData(const std::vector<uint8_t>& data, const std::string& extension, std::string& error);

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
    /// `ext` (no dot, any case) is an RZX recording
    static bool IsRzxExtension(const std::string& ext);
    bool SaveSnapshot(const std::string& path);
    /// A tape file (any TapeLoaderRegistry format) or a folder into the tape
    /// slot, at once; the deck stops and plays the new tape from its start
    bool LoadTape(const std::string& path, std::string* error = nullptr);
    /// The tape out of the deck. Refused while a TTD recording runs
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
    bool LoadDisk(const std::string& path, uint8_t drive = 0, std::string* error = nullptr);

    /// Take the disk out of `drive` (0-3, A-D) and free it; the other drives are untouched.
    /// @param force Eject even when the disk has unsaved writes (they are lost); without it a
    ///              dirty disk stays in and `error` says so
    /// @param error When non-null and the call fails, receives a human-readable reason
    /// An empty drive is not an error. Like LoadDisk, it is refused while a TTD recording runs
    bool EjectDisk(uint8_t drive, bool force = false, std::string* error = nullptr);

    /// Layout of a blank disk from CreateBlankDisk()
    enum class BlankDiskFormat
    {
        Auto,         ///< Plus3 on a +3 (its own controller), Unformatted elsewhere
        Unformatted,  ///< No sectors: for the machine's own FORMAT command (TR-DOS, +3DOS)
        Plus3,        ///< +3DOS: 9 x 512-byte sectors per track, formatted (filler #E5)
    };

    /// Parse "auto" / "unformatted" / "plus3" (case-insensitive); false for anything else
    static bool ParseBlankDiskFormat(const std::string& text, BlankDiskFormat& format);
    static const char* BlankDiskFormatName(BlankDiskFormat format);

    /// Create a blank disk and insert it into `drive`, owned like a loaded image (the previous image of
    /// the drive is released). The drive's path reads "<blank>" until the disk is saved.
    /// @param cylinders 40 or 80; 0 = the format's default (Unformatted 80, Plus3 40)
    /// @param sides 1 or 2; 0 = the format's default (Unformatted 2, Plus3 1)
    /// @param error When non-null and the call fails, receives a human-readable reason
    /// @param resolved When non-null, receives the format and geometry actually used
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
    /// stepping (a ZX-Poly group advancing its slaves after every master instruction)
    void RunUntilCondition(std::function<bool(const Z80State&)> predicate, uint64_t maxTStates = 0,
                           bool notifyDebugger = true);

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

    /// Release() has run: the context and every subsystem are gone. A holder
    /// of a shared_ptr (a UI binding) must not call into the instance any more
    bool IsReleased() const { return _isReleased; }

    /// A short-lived guarantee that the context (and every subsystem it points
    /// to) stays alive: EmulatorManager::RemoveEmulator() waits for all live
    /// leases before it stops and frees the instance. Empty (false) once the
    /// removal has begun or the instance is released - the holder then must not
    /// touch the instance. For threads that do not own the instance (the UI):
    /// a raw GetContext() can be freed by a removal on another thread at any
    /// moment. Keep a lease for one handler at most, and never remove the leased
    /// instance (or wait for a thread that does) while holding it.
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
        ContextLease(std::shared_lock<std::shared_mutex>&& lock, EmulatorContext* context)
            : _lock(std::move(lock)), _context(context)
        {
        }

        std::shared_lock<std::shared_mutex> _lock;
        EmulatorContext* _context = nullptr;
    };
    ContextLease LeaseContext();

    /// The removal has begun (EmulatorManager): LeaseContext() refuses from now on
    bool IsRetiring() const { return _retiring.load(); }
    /// EmulatorManager, first step of a removal: refuse new leases
    void BeginRetirement();
    /// EmulatorManager, before Stop()/Release(): wait until every lease taken
    /// before BeginRetirement() has ended. Hold no lock a lease holder may wait for
    void WaitForContextLeases();
    void SetState(EmulatorStateEnum state);

    // Status methods
    bool IsRunning();
    bool IsPaused();

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

    /// Every debugger breakpoint hit goes through here (the Z80's instruction start, memory reads and writes,
    /// port reads and writes). On the emulator's own run it pauses, notifies and parks the emulation thread
    /// until Resume(), as before. During a direct run (a control thread stepping a paused emulator: WebAPI,
    /// CLI, DeZog, Lua, Python) it must not park - nothing would resume the caller - so it records the stop
    /// (LastDirectStop), notifies, and the run ends after the current step. An execution breakpoint stops a
    /// direct run before its instruction: the return value true tells the Z80 not to execute it. The
    /// execution breakpoint the emulator is stopped at does not stop the run's first instruction (stepping
    /// on from a breakpoint)
    bool OnBreakpointHit(uint16_t breakpointId, uint16_t address, BreakpointHitKind kind);
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
