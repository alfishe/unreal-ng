#pragma once

/// What the emulator core calls into time travel (Phase 5, Step 1, layer 2 of
/// docs/inprogress/2026-09-25-ttd-v2-migration/phase-5-switchover-tdd.md §4.2.2).
///
/// The core (loaders, devices, MainLoop, the CPU's input gate, the feature
/// manager) talks to time travel only through this interface and
/// EmulatorContext::pTimeTravelHooks, so the object behind it can change - v1's
/// TimeTravelManager today, the engine after the switch - without touching the
/// core. Every call is per frame, on a rare event, or behind the CPU's step-work
/// bit; per-event recording (memory and port writes, coverage) is not here.
///
/// Surfaces (WebAPI, CLI, Lua, Python, GDB, DeZog, Qt) and lockstep groups that
/// drive a session (ZX-Poly) are not core callers: they use the command layer.

#include <cstdint>
#include <functional>
#include <string>

namespace ttd
{
struct TTDInputEvent;
struct TTDNetInput;
struct TTDPortQuery;
struct TTDPortSearchResult;
class TTDSerializable;
enum class TTDReplaySource : uint8_t;
enum class TTDExternalEventKind : uint8_t;
enum class PeripheralId : uint8_t;

/// @brief Session state machine values (TDD §4.2).
enum class TTDSessionState : uint8_t
{
    Idle       = 0,  ///< No active recording. History may or may not be present.
    Recording  = 1,  ///< Capture is active; OnFrameBoundary appends checkpoints.
    Detached   = 2,  ///< Emulator paused at a historical point (future seek state).
};

/// @brief Stable string identifier for a TTDSessionState.
///
/// The values ("idle" / "recording" / "detached") are part of the public
/// automation contract per parent TDD §10.4: WebAPI JSON, Lua tables, Python
/// attributes, and CLI tokens all use these exact spellings. Defined here
/// (rather than in each surface's own adapter) so the contract lives in one
/// place and is unit-testable from core-tests.
///
/// Keep in sync with the enum order above.
const char* TTDSessionStateToString(TTDSessionState state);

/// @brief Actions that would end, wipe or corrupt a recording in progress.
/// While TTD records they are refused (ITimeTravelHooks::RecordingGuard):
/// stop the recording first. A machine reset is not one of them - it stops
/// the recording and keeps the history.
enum class TTDGuardedAction : uint8_t
{
    LoadSnapshot,        ///< replaces the whole machine state
    LoadTape,            ///< a new medium
    LoadDisk,            ///< a new medium
    CreateDisk,          ///< a new medium
    LoadRom,             ///< the code every checkpoint relies on
    Invalidate,          ///< discards the session
    DisableTimeTravel,   ///< capture stops mid-session
    DisableDebugMode,    ///< writes stop reaching the history
    SwitchGsCard,        ///< a General Sound personality switch changes the device set (FR-4)
    CdFrontPanel         ///< a CD drive's play / pause / stop / volume from outside the guest: not in the journal
};

/// What replaced the machine's state or its media (OnLoad). Today every kind
/// ends the session; Phase 5, Step 2 makes them events on the timeline (D10, D25)
enum class TTDLoadKind : uint8_t
{
    Snapshot,       ///< a snapshot file
    RzxSnapshot,    ///< the snapshot an RZX file starts from
    RzxSeek,        ///< an RZX playback seek restored its own key frame
    Tape,           ///< a tape image
    Disk,           ///< a disk image
    DiskCreate,     ///< a new blank disk
    Media           ///< a media slot change (MediaManager)
};

/// What changed in the machine's configuration (OnConfigurationChange)
enum class TTDConfigChangeKind : uint8_t
{
    SpeedMultiplier,  ///< the host speed multiplier
    RomReload,        ///< the ROM set was loaded again
    GsCard            ///< the General Sound card personality
};

/// Result of ITimeTravelHooks::SubmitMachineTask
enum class TTDMachineTaskResult : uint8_t
{
    RanNow,   ///< run at once (synchronous mode)
    Queued,   ///< runs at the next instruction boundary on the machine's thread
    Refused   ///< the journal owns input (a replay, or Detached in the past)
};

class ITimeTravelHooks
{
public:
    virtual ~ITimeTravelHooks() = default;

    // Running
    /// MainLoop, once per frame
    virtual void OnFrameBoundary() = 0;
    /// The machine thread is about to stop for a while (pause, model switch)
    virtual void OnMachineParking() = 0;
    /// Z80, before an instruction, only when EmulatorContext::kStepWorkTtdInput is set
    virtual void ServiceInput() = 0;
    /// The machine left the recorded timeline from outside (reset)
    virtual void OnMachineReset() = 0;
    /// Ends a recording (a reset, an autostart); the history is kept
    virtual void StopRecording() = 0;
    /// RZX playback facts (Phase 3, Step 2): an RZX frame ended / where replay input comes from
    virtual void NoteRzxFrameEnd(uint64_t rzxFrame, bool interrupt) = 0;
    virtual void NoteReplaySource(TTDReplaySource source) = 0;

    // Things that happen to the machine
    /// A load replaced the machine state or a medium; @p reason names it in status
    virtual void OnLoad(TTDLoadKind kind, const char* reason) = 0;
    /// The machine's configuration changed under the session
    virtual void OnConfigurationChange(TTDConfigChangeKind kind, const char* reason) = 0;
    /// The machine state moved to another model (D26)
    virtual void OnModelTransfer(const char* reason) = 0;
    /// Something outside the guest happened (tape control, a disk write, a NeoGS card)
    virtual void RecordExternalEvent(TTDExternalEventKind kind, const char* reason) = 0;
    /// A tool's edit of the machine: Begin before it, End after it (Emulator::EditMemoryFromTool)
    virtual void BeginToolEdit() = 0;
    virtual void EndToolEdit(const char* source) = 0;
    /// A device was swapped for another one (the General Sound card)
    virtual void UpdatePeripheral(PeripheralId oldId, PeripheralId newId, TTDSerializable* device) = 0;

    // Input
    /// The one entry point for live input (any thread); false while OwnsInput()
    virtual bool SubmitLiveInput(const TTDInputEvent& ev) = 0;
    virtual bool SubmitLiveInput(const TTDInputEvent& ev, const TTDNetInput& net, const uint8_t* payload, uint32_t length) = 0;
    /// True while the journal owns input (a replay, or the machine Detached in the past)
    virtual bool OwnsInput() const = 0;
    /// A lockstep group takes live input itself (the ZX-Poly master); empty function removes it
    virtual void SetLiveInputInterceptor(std::function<bool(const TTDInputEvent&)> interceptor) = 0;
    /// Run @p task on the machine's thread (a device the executing thread may be using)
    virtual TTDMachineTaskResult SubmitMachineTask(std::function<void()> task) = 0;
    /// Journal input a lockstep group applies itself (ZX-Poly gives every member the same
    /// input at one frame boundary): call before applying it, while recording
    virtual void RecordInputEvent(uint8_t key, bool pressed) = 0;
    virtual void RecordMouseMove(int dx, int dy) = 0;
    virtual void RecordMouseButtons(uint8_t activeLowMask) = 0;
    virtual void RecordMouseWheel(int steps) = 0;

    // State the core asks about
    virtual TTDSessionState GetState() const = 0;
    virtual bool IsRecording() const = 0;
    virtual bool IsReplayActive() const = 0;
    /// The frame the session's position is in (the recording's present, or where a seek left it)
    virtual uint64_t CurrentFrame() const = 0;
    /// The session's port journals searched (ttdportsearch.h): device reports that read the
    /// recorded OUTs (the Sprinter PLD journal, source=ttd)
    virtual TTDPortSearchResult SearchPortEvents(const TTDPortQuery& q) const = 0;
    /// True when the session holds any recorded history
    virtual bool HasHistory() const = 0;
    /// Empty when @p action may run now; otherwise one sentence saying why not
    virtual std::string RecordingGuard(TTDGuardedAction action) const = 0;
    /// Time travel is not available for this instance (a ZX-Poly member); empty: available
    virtual void SetUnavailableReason(const std::string& reason) = 0;
    /// A feature flag changed (debug mode, time travel, coverage)
    virtual void UpdateFeatureCache() = 0;
};
}  // namespace ttd
