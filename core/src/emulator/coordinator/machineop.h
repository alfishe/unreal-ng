#pragma once

/// @file machineop.h
/// @brief An operation on the machine as a whole, as a caller hands it to the coordinator, and the answer.
///
/// Design: docs/inprogress/2026-10-09-machine-state-coordinator/architecture.md §2. The coordinator library
/// includes nothing outside its own directory and the standard library (requirements N7): every type it needs
/// is here or in ports.h; what an owner needs to carry (a media source, snapshot options) rides in `payload`.

#include <any>
#include <cstdint>
#include <string>

namespace coordinator
{
/// What the operation does. A new kind needs a row in the policy table (machinepolicy.cpp): without one the
/// library does not compile
enum class OpKind : uint8_t
{
    Reset,          ///< a machine reset (soft or hard)
    Autostart,      ///< a disk autostart's reset into TR-DOS
    SnapshotLoad,   ///< a snapshot replaces the machine state
    MediaInsert,    ///< a medium goes into a slot (tape, disk, hard disk, SD, CD)
    MediaEject,     ///< a medium leaves its slot
    MediaDiscard,   ///< a medium's unsaved writes are dropped
    DiskCreate,     ///< a new blank disk goes into a drive
    NetworkChange,  ///< the network settings change: the network devices are fitted again (D43, M2)
    IdeDriveKind,   ///< an empty IDE unit gets another drive: hard disk, CD-ROM, CompactFlash (D43, M2)
    RzxRestore,     ///< an RZX playback replaces the machine: its snapshot block, a seek back (D43, M2)
    ModelSwitch,    ///< another model replaces the instance, or another machine's state moves in (D26): asked only
                    ///< (IMachineOps::Ask) - the switch replaces the instance and its coordinator (Q1)
    TimelineCut,    ///< a recording continues from a point in its past (Rec From Here, M5), with what was decided about
                    ///< the media that could not go back there (M6: IMachineOps::RunOwnLocked)
    RomLoad,        ///< the ROM set is loaded again (a ROM file, a reset's pending reload: a BIOS, a flash discard) (M6)
    GsCardSwitch,   ///< the General Sound card changes its personality (LLE, lightweight, NeoGS) (M6)
    SpeedChange,    ///< the host speed multiplier changes (M6)
    StateTransfer,  ///< another machine's state moves into this one (MachineStateTransfer, D26) (M6)
    SlotChange,     ///< a slot is plugged, removed or set up (applied by a machine restart): asked only (M6)
    CdFrontPanel,   ///< a CD drive's play, pause, stop or volume from outside the guest: asked only (M6)
    ToolEdit,       ///< a tool edits the machine: memory, registers, ports, sectors, device memory - any surface (M6)
    TtdStart,       ///< a TTD recording starts (M6; TTD's own lock: IMachineOps::RunOwnLocked)
    TtdStop,        ///< a TTD recording stops (M6)
    TtdDiscard,     ///< the TTD session and its history are discarded (M6)
    TimelineSeek,   ///< the machine moves to a point of the recorded history: a seek, a step, a reverse run (M6)
    TtdLoad,        ///< a TTD session is loaded from a file (M6)

    Count
};

/// Who asked: every surface, the machine thread itself, a test
enum class OpSource : uint8_t
{
    Unknown,
    Qt,
    WebApi,
    Cli,
    Lua,
    Python,
    Gdb,
    DeZog,
    Mcp,
    Companion, ///< a paired companion client (docs/inprogress/2026-10-16-companion-client)
    Machine,   ///< the machine thread: a trap, a device, an RZX playback
    Core,      ///< the emulator's own code (a model's start-up, a composite operation)
    Test,
    HostDevice, ///< a host input device bound to an action (a pad, the Surface Dial): the user's hands

    Count
};

/// What the machine is doing, as far as the rules care (architecture §5)
enum class MachineMode : uint8_t
{
    Live,        ///< no recording; a stopped session's history may be there
    Recording,   ///< a recording the user started (also paused for browsing): protected
    BlackBox,    ///< a background recording (Always Record): never protected, its session ends instead
    Replaying,   ///< a replay re-executes recorded history
    Detached,    ///< the machine sits at a point in the recorded past

    Count
};

/// Which refusal text a media operation gets (the surfaces' wording of today)
enum class MediaClass : uint8_t
{
    Any,    ///< a slot operation from the media manager
    Tape,   ///< Emulator::LoadTape
    Disk    ///< Emulator::LoadDisk
};

/// What happens to the TTD session when the operation goes ahead; applied once, at the operation's commit
/// point (OpStage::CommitPoint)
enum class SessionEffect : uint8_t
{
    None,
    EndKeepHistory,        ///< D42: the session ends, the history of the same machine stays browsable
    MediaChanged,          ///< a medium changed: today's TTD rule for media (a background session ends, history goes)
    StopThenMediaChanged,  ///< the caller asked to end a protected recording instead of being refused
    MachineChanged,        ///< the machine itself changed (ROM, General Sound card, a state transfer): the session
                           ///< ends, its history goes (it belongs to the old machine)
    TimingChanged          ///< the host speed changed: a history goes, the black box's stays (recorded at 1x)
};

enum class OpStatus : uint8_t
{
    Done,
    Refused,   ///< the rules or the owner said no; nothing happened
    Failed     ///< the owner could not do it (a bad file); see OpResult::sessionEffect for what the session saw
};

/// Where the machine was when an operation was logged
struct MachineTime
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    uint64_t wallMs = 0;
};

struct MachineOp
{
    OpKind kind = OpKind::Reset;
    OpSource source = OpSource::Unknown;
    /// For the log: the slot and the file, the snapshot's path, "hard"
    std::string summary;
    /// The reason the TTD session names in status ("reset", "tape-load"); empty: the kind's name
    std::string sessionReason;
    MediaClass media = MediaClass::Any;
    /// End a protected recording instead of being refused (media operations)
    bool endRecording = false;
    /// What the owner needs; the coordinator never looks inside
    std::any payload;
};

struct OpResult
{
    OpStatus status = OpStatus::Done;
    /// Refused / Failed: the one sentence every surface shows
    std::string reason;
    /// The session effect applied (None when refused, failed before the commit point, or nothing to do)
    SessionEffect sessionEffect = SessionEffect::None;
    /// The operation's entry in the operation log
    uint64_t logSeq = 0;
    /// What the owner hands back (a media report, an autostart result), also when it failed
    std::any output;

    bool Ok() const { return status == OpStatus::Done; }
};

/// Who asks from this thread: the innermost SourceScope, else the thread's source (SetThreadSource), else Unknown.
/// Run, WouldRefuse and Ask take it for an operation that names no source: each surface marks its own thread (an
/// interpreter, a debugger session, the CLI client) or request (the WebAPI) once instead of every call site
OpSource CurrentSource();
/// This thread's source until set again (a surface's own thread at its start, the WebAPI per request)
void SetThreadSource(OpSource source);

/// The source of the operations asked for in a scope on this thread; the previous one comes back at its end
class SourceScope
{
public:
    explicit SourceScope(OpSource source);
    ~SourceScope();
    SourceScope(const SourceScope&) = delete;
    SourceScope& operator=(const SourceScope&) = delete;

private:
    OpSource _previous;
};

const char* OpKindName(OpKind kind);
const char* OpSourceName(OpSource source);
const char* MachineModeName(MachineMode mode);
const char* SessionEffectName(SessionEffect effect);
const char* OpStatusName(OpStatus status);
}  // namespace coordinator
