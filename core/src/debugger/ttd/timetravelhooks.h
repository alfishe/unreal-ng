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
#include <utility>
#include <vector>

#include "ttdphyspage.h"

namespace ttd
{
struct TTDInputEvent;
class TTDInputJournal;
struct TTDSessionInfo;
struct TTDNetInput;
struct TTDPortQuery;
struct TTDPortSearchResult;
class TTDSerializable;
enum class TTDReplaySource : uint8_t;
enum class TTDExternalEventKind : uint8_t;
enum class PeripheralId : uint8_t;
enum class TTDDeviceType : uint16_t;

/// @brief Session state machine values (TDD §4.2).
enum class TTDSessionState : uint8_t
{
    Idle       = 0,  ///< No active recording. History may or may not be present.
    Recording  = 1,  ///< Capture is active; OnFrameBoundary appends checkpoints.
    Detached   = 2,  ///< Emulator paused at a historical point (future seek state).
};

/// @brief Where the session's position is, as the backend last published it (control surfaces architecture §7):
/// what a readout, a dial's seek and a companion polling the position read, from any thread, without a lock and
/// without the machine (ITimeTravelHooks::PublishedCursor). Written at every frame boundary and at the end of every
/// session operation (a seek, a step, a start, a stop), so it is at most one frame old while the machine runs.
struct TTDCursor
{
    uint64_t frame = 0;        ///< the position: the frame...
    uint32_t tinframe = 0;     ///< ...and the T-state in it (the frame's start between boundaries of a running machine)
    uint64_t first = 0;        ///< the earliest frame the history keeps (its first checkpoint)
    uint64_t end = 0;          ///< where the history ends (SessionEndPosition): the frame...
    uint32_t endTinframe = 0;  ///< ...and the T-state in it
    TTDSessionState state = TTDSessionState::Idle;
    bool history = false;      ///< the session holds a recorded history (first / end are meaningful)
    bool paused = false;       ///< the machine is paused (read live, not published)

    bool operator==(const TTDCursor& other) const
    {
        return frame == other.frame && tinframe == other.tinframe && first == other.first && end == other.end &&
               endTinframe == other.endTinframe && state == other.state && history == other.history &&
               paused == other.paused;
    }
    bool operator!=(const TTDCursor& other) const { return !(*this == other); }
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
/// stop the recording first. A machine reset and a snapshot load are not among them: they END the session (EndSession).
enum class TTDGuardedAction : uint8_t
{
    LoadTape,            ///< a new medium
    LoadDisk,            ///< a new medium
    CreateDisk,          ///< a new medium
    LoadRom,             ///< the code every checkpoint relies on
    Invalidate,          ///< discards the session
    SwitchGsCard,        ///< a General Sound personality switch changes the device set (FR-4)
    CdFrontPanel,        ///< a CD drive's play / pause / stop / volume from outside the guest: not in the journal
    SwitchModel,         ///< another machine model (a snapshot that needs one, a machine state transfer): D26
    ChangeSlots          ///< a slot change (plug, remove, options) changes the device set (D38, ZX-bus slots R-OP-7)
};

/// What changed under the session, as the machine coordinator's rules say (EndSessionFor): the coordinator's
/// session port is the one caller (a reset and a snapshot load are EndSession: the history of the same machine stays)
enum class TTDChange : uint8_t
{
    Media,     ///< a medium changed: a background session ends, its recording kept on disk; otherwise the history goes
    Machine,   ///< the machine itself changed (the ROM set, the General Sound card, a state transfer): the history
               ///< belongs to the old one and goes
    Timing     ///< the host speed changed: the history goes, except the black box's (recorded at 1x, it stays valid)
};

/// Result of ITimeTravelHooks::SubmitMachineTask
enum class TTDMachineTaskResult : uint8_t
{
    RanNow,   ///< run at once (synchronous mode)
    Queued,   ///< runs at the next instruction boundary on the machine's thread
    Refused   ///< the journal owns input (a replay, or Detached in the past)
};

/// The per-event writes the core reports while time travel records (Phase 5, C1d):
/// memory writes on the debug write path and port OUTs. Set once per machine in
/// EmulatorContext::ttdWriteSink; the recorder checks its own state. Coverage takes
/// a different route (EmulatorContext::ttdCoverage, an inline record) because it
/// runs on every instruction fetch and read
class ITTDWriteSink
{
public:
    virtual ~ITTDWriteSink() = default;
    virtual void RecordMemoryWrite(uint16_t addr, uint8_t oldVal, uint8_t newVal, uint16_t m1pc, PhysPage physPage) = 0;
    virtual void RecordIoWrite(uint16_t port, uint8_t value, uint16_t m1pc) = 0;
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
    /// Ends a recording; the history is kept. For an operation that ends the session (reset, autostart) use EndSession
    virtual void StopRecording() = 0;
    /// THE rule (D42): a reset, an autostart, a snapshot load, a change of the machine itself END the recording session. The
    /// history of the same machine stays browsable; a new session starts only when the `ttdrestart` feature is on (the black
    /// box always starts one), at the next frame boundary
    virtual void EndSession(const char* reason) = 0;
    /// RZX playback facts (Phase 3, Step 2): an RZX frame ended / where replay input comes from
    virtual void NoteRzxFrameEnd(uint64_t rzxFrame, bool interrupt) = 0;
    virtual void NoteReplaySource(TTDReplaySource source) = 0;

    // Things that happen to the machine
    /// @p change ends the session, or drops the history it left (see TTDChange); @p reason names it in status.
    /// Nothing to end: nothing happens. Called by the coordinator's session port only (an operation's session effect)
    virtual void EndSessionFor(TTDChange change, const char* reason) = 0;
    /// Something outside the guest happened (tape control, a disk write, a NeoGS card)
    virtual void RecordExternalEvent(TTDExternalEventKind kind, const char* reason) = 0;
    /// A medium change landed in @p slot at this frame boundary (before its checkpoint): @p what is the new medium
    /// ("<path>"), "ejected" or "discarded". A recording keeps it as an event there (M3, Q4); then ports and sectors
    /// only. A backend that keeps no such events refuses media changes while recording instead
    virtual void NoteMediaChange(const char* slot, const char* what)
    {
        (void)slot;
        (void)what;
    }
    /// Whether a recording keeps media changes as events (NoteMediaChange): the engine does, TTD v1 does not
    virtual bool RecordsMediaChanges() const { return false; }
    /// A tool's edit of the machine: Begin before it, End after it (Emulator::EditMemoryFromTool,
    /// and a fast loader's trap: what it wrote, the registers and the time it spent).
    /// End without a source: nothing was edited (a trap declined)
    virtual void BeginToolEdit() = 0;
    virtual void EndToolEdit(const char* source) = 0;
    /// Whether a recording keeps a logic callback's writes (RecordScriptEdit): the engine does, TTD v1 does not
    virtual bool RecordsScriptEdits() const { return false; }
    /// A logic callback's writes, just made at an instruction boundary (ScriptCallbackManager::ApplyEdits): the bytes
    /// in @p pokes (address, byte) written through the CPU's view, in order, and with @p cpu the registers it set.
    /// A recording keeps them as an edit there that a replay applies (the callback never runs on replay)
    virtual void RecordScriptEdit(const char* source, const std::vector<std::pair<uint16_t, uint8_t>>& pokes, bool cpu)
    {
        (void)source;
        (void)pokes;
        (void)cpu;
    }
    /// A device was swapped for another one (the General Sound card): every device of @p oldType leaves, @p device
    /// comes in under its own key
    virtual void UpdatePeripheral(TTDDeviceType oldType, TTDSerializable* device) = 0;

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
    /// The session's input journal: network devices restoring their state
    /// read received bytes from it by journal index (NetAt / PayloadOf)
    virtual const TTDInputJournal& InputJournal() const = 0;
    /// The session's status without blocking (a frame old while it records)
    /// and the last one published (what observers on other threads read)
    virtual TTDSessionInfo ReadSessionInfo() const = 0;
    virtual TTDSessionInfo GetPublishedSessionInfo() const = 0;
    /// The position and the history's bounds as last published (TTDCursor): lock-free, never waits for the machine
    /// and never parks it, so a companion may poll it at 50 Hz (a seqlock, ttdcursorcell.h)
    virtual TTDCursor PublishedCursor() const = 0;
    /// Empty when @p action may run now; otherwise one sentence saying why not
    virtual std::string RecordingGuard(TTDGuardedAction action) const = 0;
    /// Time travel is not available for this instance (a ZX-Poly member); empty: available
    virtual void SetUnavailableReason(const std::string& reason) = 0;
    /// A feature flag changed (debug mode, time travel, coverage)
    virtual void UpdateFeatureCache() = 0;
    /// FR-17: @p feature (timetravel or debugmode) is about to be switched off. A recording
    /// stops cleanly first - the machine parked, everything up to this instant kept - and
    /// status names the reason (last_stop_reason "feature-off:<feature>"). Not recording: nothing
    virtual void StopForFeatureChange(const char* feature) = 0;

    // The black box (Phase 5, Step 3; D29)
    /// Whether the recording holds the machine at real speed (host speed 1x;
    /// turbo, fast tape, turbo tape and fast disk off). A black box does not:
    /// it stops while an acceleration runs instead (OnAccelerationChanging)
    virtual bool LocksAcceleration() const = 0;
    /// An acceleration is about to start (@p accelerating, before it takes
    /// effect) or has ended (after it). A black box stops before it, its
    /// history kept, and starts a new session once nothing accelerates
    virtual void OnAccelerationChanging(bool accelerating) = 0;
    /// Why the host speed may not change now ("" = it may): a history the user recorded (recording, paused or
    /// stopped) runs at the 1x it was recorded at - another multiplier would drop it (frame timing is part of the
    /// determinism contract), so the speed control is refused, not the history. A black box refuses nothing
    virtual std::string SpeedLock() const { return {}; }
};
}  // namespace ttd
