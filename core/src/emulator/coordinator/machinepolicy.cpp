#include "machinepolicy.h"

#include <array>
#include <cstddef>

namespace coordinator
{
namespace
{
enum class Action : uint8_t
{
    Allow,
    Refuse,
    RefuseUnlessEnd,   ///< refused, unless the caller asked to end the recording (MachineOp::endRecording)
    EventUnlessEnd     ///< an event of the recording at the next frame boundary, unless the caller ends the recording
};

struct Cell
{
    Action action;
    SessionEffect effect;
    bool park = false;   ///< parked in this mode even without a session effect
};

struct Row
{
    OpKind kind;
    bool hold;                ///< always parked (the machine state is replaced)
    bool machineResetAfter;   ///< the session hears MachineReset after the commit
    bool endsPlayback;        ///< an input playback (RZX) ends first: the machine leaves the recorded path
    std::array<Cell, static_cast<size_t>(MachineMode::Count)> cells;   ///< Live, Recording, BlackBox, Replaying, Detached
};

constexpr Cell Allow(SessionEffect effect) { return {Action::Allow, effect}; }
constexpr Cell AllowHeld(SessionEffect effect) { return {Action::Allow, effect, true}; }
constexpr Cell kMediaRecording{Action::EventUnlessEnd, SessionEffect::StopThenMediaChanged};
constexpr Cell kRefuse{Action::Refuse, SessionEffect::None};

// Today's behaviour (M1), read from the code it replaces (current-state.md §2-3):
// - reset, autostart, snapshot load: D42, the session ends and the history stays (TimeTravelController::EndSession)
//   in every mode; the machine is held; reset and autostart then tell TTD the machine left the recorded timeline;
// - media: while a recording the user started runs, a change is an event of the recording (M3, owner 2026-10-10:
//   the history keeps the change and then ports and sectors only): it lands at the next frame boundary and the
//   recording goes on; a caller that asks to end the recording ends it instead; everywhere else the TTD media rule
//   (OnLoad(Media)): a background session ends, a kept history goes.
// D43 cells (owner 2026-10-10, M2):
// - network settings, an IDE unit's drive: refused while a recording the user started runs and in the recorded past
//   (replay, detached); the black box ends its session as for a medium; live: nothing for TTD (a host-side setting,
//   RemoteAccess alone, is no operation: it changes nothing the machine sees);
// - an RZX playback's snapshot block or seek back: a snapshot load (D42).
// A model switch or a machine state transfer: allowed in every mode, a change of the machine itself ends the session
// (owner rule 2026-10-05); the TTD v1 backend still refuses it while recording, as its owner's refusal (the
// emulator's), until v1 is deleted.
// M6, the changes of the machine itself (owner rule 2026-10-05; today's TTD engine): a ROM load, a General Sound card
// switch, a state transfer end the session in every mode and drop its history (the black box starts again on the
// changed machine); TTD v1 refuses them while recording, as the emulator's owner refusal, until v1 is deleted. A speed
// change drops a history except the black box's (held: dropping a stopped session's history takes the CPU, which must
// not wait inside the coordinator); a recording the user started locks the speed at 1x (the feature
// manager refuses the change before it is asked). A slot change is applied by a restart and only asked here (v1
// refuses it as the owner). A CD drive's front panel from outside the guest is refused while a recording the user
// started runs: a replay would not repeat it.
// A tool's edit (any surface): nothing for the session at the commit point - while a recording runs the edit is
// recorded as an event of it (BeginToolEdit / EndToolEdit around it; a paused recording the user started goes on
// there, D9) and the machine is held for it; live, it is written as it comes (no hold: a script's pokes stay cheap).
// In the recorded past (a replay, a point of a stopped history) it is refused (owner 2026-10-11, §4): nothing would
// keep it - the next seek sets the point back, a replay goes off its recording; Rec From Here first.
// TTD's own operations (start, stop, discard, a seek, a load, Rec From Here): TTD decides where they may go (its
// replies refuse them), the rules add nothing; they run on TTD's own lock (IMachineOps::RunOwnLocked, M6).
// A reset, an autostart's reset and a snapshot load take the machine off the path an RZX playback drives: the playback
// ends first (M6; the autostart's reset joined, it left the playback running on a reset machine before).
// Live, Recording, BlackBox, Replaying, Detached
constexpr SessionEffect kKeep = SessionEffect::EndKeepHistory;
constexpr SessionEffect kMedia = SessionEffect::MediaChanged;
constexpr SessionEffect kNone = SessionEffect::None;
constexpr SessionEffect kMachine = SessionEffect::MachineChanged;
constexpr SessionEffect kTiming = SessionEffect::TimingChanged;
constexpr std::array<Row, static_cast<size_t>(OpKind::Count)> kTable{{
    {OpKind::Reset, true, true, true, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::Autostart, true, true, true, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::SnapshotLoad, true, false, true, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::MediaInsert, false, false, false,
     {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::MediaEject, false, false, false,
     {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::MediaDiscard, false, false, false,
     {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::DiskCreate, false, false, false,
     {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::NetworkChange, false, false, false, {Allow(kNone), kRefuse, Allow(kMedia), kRefuse, kRefuse}},
    {OpKind::IdeDriveKind, true, false, false, {Allow(kNone), kRefuse, Allow(kMedia), kRefuse, kRefuse}},
    {OpKind::RzxRestore, true, false, false, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::ModelSwitch, false, false, false, {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::TimelineCut, false, false, false, {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::RomLoad, true, false, false,
     {Allow(kMachine), Allow(kMachine), Allow(kMachine), Allow(kMachine), Allow(kMachine)}},
    {OpKind::GsCardSwitch, false, false, false,
     {Allow(kMachine), Allow(kMachine), Allow(kMachine), Allow(kMachine), Allow(kMachine)}},
    {OpKind::SpeedChange, true, false, false,
     {Allow(kTiming), Allow(kTiming), Allow(kTiming), Allow(kTiming), Allow(kTiming)}},
    {OpKind::StateTransfer, true, false, false,
     {Allow(kMachine), Allow(kMachine), Allow(kMachine), Allow(kMachine), Allow(kMachine)}},
    {OpKind::SlotChange, false, false, false, {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::CdFrontPanel, false, false, false, {Allow(kNone), kRefuse, Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::ToolEdit, false, false, false,
     {Allow(kNone), AllowHeld(kNone), AllowHeld(kNone), kRefuse, kRefuse}},
    {OpKind::TtdStart, false, false, false,
     {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::TtdStop, false, false, false,
     {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::TtdDiscard, false, false, false,
     {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::TimelineSeek, false, false, false,
     {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
    {OpKind::TtdLoad, false, false, false,
     {Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone), Allow(kNone)}},
}};

constexpr bool RowsInKindOrder()
{
    for (size_t i = 0; i < kTable.size(); ++i)
        if (static_cast<size_t>(kTable[i].kind) != i)
            return false;
    return true;
}
static_assert(RowsInKindOrder(), "machinepolicy: one row per OpKind, in the enum's order");

std::string RefusalText(const MachineOp& op, MachineMode mode)
{
    const bool past = mode == MachineMode::Replaying || mode == MachineMode::Detached;
    if (op.kind == OpKind::NetworkChange)
        return past ? "the machine is in recorded history (a TTD replay or a point in the past): the network settings "
                      "are fixed until it is live again"
                    : "a TTD recording is running: the network settings are fixed until it stops";
    if (op.kind == OpKind::IdeDriveKind)
        return past ? "the machine is in recorded history (a TTD replay or a point in the past): its hardware is fixed "
                      "until it is live again"
                    : "a TTD recording runs: the machine's hardware is fixed until it stops";
    if (op.kind == OpKind::ToolEdit)
        return "the machine is in recorded history (a TTD replay or a point in the past): an edit would not be kept. "
               "Continue the recording from here (ttd resume) or go back to live first";
    if (op.kind == OpKind::CdFrontPanel)
        return "Cannot play, pause, stop or change the volume of a CD drive from outside the guest while TTD is "
               "recording: a replay would not repeat it. Let the guest's CD player do it, or stop the recording first.";
    // No other cell refuses today (media changes while recording are events since M3; TTD v1 refuses them as the
    // media owner's refusal)
    return std::string("'") + OpKindName(op.kind) + "' is not allowed while the machine is " + MachineModeName(mode);
}
}  // namespace

PolicyDecision Decide(const MachineOp& op, MachineMode mode)
{
    PolicyDecision d;
    const size_t kind = static_cast<size_t>(op.kind);
    const size_t column = static_cast<size_t>(mode);
    if (kind >= kTable.size() || column >= static_cast<size_t>(MachineMode::Count))
    {
        d.refused = true;
        d.reason = "unknown machine operation";
        return d;
    }
    const Row& row = kTable[kind];
    const Cell& cell = row.cells[column];
    if (cell.action == Action::Refuse || (cell.action == Action::RefuseUnlessEnd && !op.endRecording))
    {
        d.refused = true;
        d.reason = RefusalText(op, mode);
        return d;
    }
    if (cell.action == Action::EventUnlessEnd && !op.endRecording)
    {
        d.atFrameBoundary = true;   // the session sees nothing at the commit: the change is its event where it lands
        return d;
    }
    d.effect = cell.effect;
    // A session effect outside Live may stop a recording, which parks the machine: hold it first, so the
    // operation never waits for the machine while holding the coordinator (machinecoordinator.cpp, Run)
    d.hold = row.hold || cell.park || (d.effect != SessionEffect::None && mode != MachineMode::Live);
    d.machineResetAfter = row.machineResetAfter;
    d.endsPlayback = row.endsPlayback;
    return d;
}
}  // namespace coordinator
