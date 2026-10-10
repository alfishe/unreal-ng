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
    RefuseUnlessEnd   ///< refused, unless the caller asked to end the recording (MachineOp::endRecording)
};

struct Cell
{
    Action action;
    SessionEffect effect;
};

struct Row
{
    OpKind kind;
    bool hold;                ///< always parked (the machine state is replaced)
    bool machineResetAfter;   ///< the session hears MachineReset after the commit
    std::array<Cell, static_cast<size_t>(MachineMode::Count)> cells;   ///< Live, Recording, BlackBox, Replaying, Detached
};

constexpr Cell Allow(SessionEffect effect) { return {Action::Allow, effect}; }
constexpr Cell kMediaRecording{Action::RefuseUnlessEnd, SessionEffect::StopThenMediaChanged};
constexpr Cell kRefuse{Action::Refuse, SessionEffect::None};

// Today's behaviour (M1), read from the code it replaces (current-state.md §2-3):
// - reset, autostart, snapshot load: D42, the session ends and the history stays (TimeTravelController::EndSession)
//   in every mode; the machine is held; reset and autostart then tell TTD the machine left the recorded timeline;
// - media: refused while a recording the user started runs (RecordingGuard), unless the caller ends it; everywhere
//   else the TTD media rule (OnLoad(Media)): a background session ends, a kept history goes.
// D43 cells (owner 2026-10-10, M2):
// - network settings, an IDE unit's drive: refused while a recording the user started runs and in the recorded past
//   (replay, detached); the black box ends its session as for a medium; live: nothing for TTD (a host-side setting,
//   RemoteAccess alone, is no operation: it changes nothing the machine sees);
// - an RZX playback's snapshot block or seek back: a snapshot load (D42).
// Live, Recording, BlackBox, Replaying, Detached
constexpr SessionEffect kKeep = SessionEffect::EndKeepHistory;
constexpr SessionEffect kMedia = SessionEffect::MediaChanged;
constexpr SessionEffect kNone = SessionEffect::None;
constexpr std::array<Row, static_cast<size_t>(OpKind::Count)> kTable{{
    {OpKind::Reset, true, true, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::Autostart, true, true, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::SnapshotLoad, true, false, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
    {OpKind::MediaInsert, false, false, {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::MediaEject, false, false, {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::MediaDiscard, false, false, {Allow(kMedia), kRefuse, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::DiskCreate, false, false, {Allow(kMedia), kMediaRecording, Allow(kMedia), Allow(kMedia), Allow(kMedia)}},
    {OpKind::NetworkChange, false, false, {Allow(kNone), kRefuse, Allow(kMedia), kRefuse, kRefuse}},
    {OpKind::IdeDriveKind, true, false, {Allow(kNone), kRefuse, Allow(kMedia), kRefuse, kRefuse}},
    {OpKind::RzxRestore, true, false, {Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep), Allow(kKeep)}},
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
    if (op.kind == OpKind::DiskCreate)
        return "Cannot create a disk while TTD is recording: a new medium would drop the recorded history. Create it "
               "before starting the recording, or stop the recording first.";
    switch (op.media)
    {
        case MediaClass::Tape:
            return "Cannot insert a tape while TTD is recording: a new medium would drop the recorded history. Insert "
                   "it before starting the recording, or stop the recording first.";
        case MediaClass::Disk:
            return "Cannot insert a disk while TTD is recording: a new medium would drop the recorded history. Insert "
                   "it before starting the recording, or stop the recording first.";
        case MediaClass::Any:
            break;
    }
    return "the media set is fixed while a TTD recording runs; end the recording first";
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
    d.effect = cell.effect;
    // A session effect outside Live may stop a recording, which parks the machine: hold it first, so the
    // operation never waits for the machine while holding the coordinator (machinecoordinator.cpp, Run)
    d.hold = row.hold || (d.effect != SessionEffect::None && mode != MachineMode::Live);
    d.machineResetAfter = row.machineResetAfter;
    return d;
}
}  // namespace coordinator
