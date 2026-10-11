#include "machinecoordinator.h"

#include <exception>
#include <string>
#include <utility>

namespace coordinator
{
namespace
{
/// The one park primitive: held for the operation, released on every path
class ParkLease
{
public:
    ParkLease() = default;
    ParkLease(const ParkLease&) = delete;
    ParkLease& operator=(const ParkLease&) = delete;
    ~ParkLease()
    {
        if (_held)
            _thread->Unpark();
    }

    void Take(IMachineThread* thread, const char* tag)
    {
        _tried = true;
        _thread = thread;
        _held = thread && thread->Park(tag);
    }
    /// A park was attempted (a time-out leaves it not held; the operation goes on, as the old brackets did)
    bool Tried() const { return _tried; }

private:
    IMachineThread* _thread = nullptr;
    bool _tried = false;
    bool _held = false;
};

/// How many operations this thread is inside (Run, any coordinator): a nested one (an owner's composite operation)
/// leaves the outermost one's preparations alone
thread_local int t_runDepth = 0;

class RunDepth
{
public:
    RunDepth() { ++t_runDepth; }
    ~RunDepth() { --t_runDepth; }
    RunDepth(const RunDepth&) = delete;
    RunDepth& operator=(const RunDepth&) = delete;
    /// The operation is the outermost one on its thread
    static bool Outermost() { return t_runDepth == 1; }
};
}  // namespace

void MachineCoordinator::SetMachine(IMachineThread* thread, IMachineClock* clock)
{
    std::lock_guard<std::recursive_mutex> op(_opMutex);
    std::unique_lock<std::shared_mutex> ports(_portsMutex);
    _thread = thread;
    _clock = clock;
}

void MachineCoordinator::SetSession(ISessionSink* session)
{
    std::lock_guard<std::recursive_mutex> op(_opMutex);
    std::unique_lock<std::shared_mutex> ports(_portsMutex);
    _session = session;
    _modeSession.store(session);
}

void MachineCoordinator::SetPlayback(IPlaybackSink* playback)
{
    std::lock_guard<std::recursive_mutex> op(_opMutex);
    std::unique_lock<std::shared_mutex> ports(_portsMutex);
    _playback = playback;
}

bool MachineCoordinator::Own(OpKind kind, IOpOwner* owner)
{
    const size_t index = static_cast<size_t>(kind);
    std::lock_guard<std::recursive_mutex> op(_opMutex);
    std::unique_lock<std::shared_mutex> ports(_portsMutex);
    if (index >= _owners.size() || owner == nullptr || (_owners[index] && _owners[index] != owner))
    {
        ports.unlock();
        OperationLogEntry entry;
        entry.time = _clock ? _clock->Now() : MachineTime{};
        entry.kind = index < _owners.size() ? kind : OpKind::Reset;
        entry.source = OpSource::Core;
        entry.status = OpStatus::Refused;
        entry.summary = "wiring";
        entry.detail = std::string("a second owner for ") + OpKindName(kind);
        _log.Append(std::move(entry));
        return false;
    }
    _owners[index] = owner;
    return true;
}

void MachineCoordinator::Disown(IOpOwner* owner)
{
    std::lock_guard<std::recursive_mutex> op(_opMutex);
    std::unique_lock<std::shared_mutex> ports(_portsMutex);
    for (IOpOwner*& slot : _owners)
        if (slot == owner)
            slot = nullptr;
}

MachineMode MachineCoordinator::Mode() const
{
    std::shared_lock<std::shared_mutex> ports(_portsMutex);
    return ModeLocked();
}

MachineMode MachineCoordinator::ModeLocked() const
{
    return _session ? _session->Mode() : MachineMode::Live;
}

void MachineCoordinator::Listen(IModeListener* listener)
{
    std::lock_guard<std::recursive_mutex> lock(_modeMutex);
    if (listener && std::find(_listeners.begin(), _listeners.end(), listener) == _listeners.end())
        _listeners.push_back(listener);
}

void MachineCoordinator::Unlisten(IModeListener* listener)
{
    std::lock_guard<std::recursive_mutex> lock(_modeMutex);
    _listeners.erase(std::remove(_listeners.begin(), _listeners.end(), listener), _listeners.end());
}

void MachineCoordinator::SessionChanged()
{
    // Not under the ports' lock: TTD reports from inside an operation's session effect as well
    std::lock_guard<std::recursive_mutex> lock(_modeMutex);
    ModeState now;
    if (ISessionSink* session = _modeSession.load())
    {
        now.mode = session->Mode();
        now.replaying = session->Replaying();
    }
    if (now == _published)
        return;
    const ModeState from = _published;
    _published = now;
    // A copy: a listener may unregister itself (it is still told this once)
    const std::vector<IModeListener*> listeners = _listeners;
    for (IModeListener* listener : listeners)
        listener->OnModeChanged(from, now);
}

void MachineCoordinator::Note(MachineOp op, OpStatus status, std::string detail)
{
    if (op.source == OpSource::Unknown)
        op.source = CurrentSource();
    OperationLogEntry entry;
    {
        std::shared_lock<std::shared_mutex> ports(_portsMutex);
        entry.time = _clock ? _clock->Now() : MachineTime{};
        entry.mode = ModeLocked();
    }
    entry.kind = op.kind;
    entry.source = op.source;
    entry.status = status;
    entry.summary = std::move(op.summary);
    entry.detail = std::move(detail);
    _log.Append(std::move(entry));
}

OpResult MachineCoordinator::RunOwnLocked(MachineOp op, const std::function<bool(OpStage&)>& commit)
{
    if (op.source == OpSource::Unknown)
        op.source = CurrentSource();
    MachineMode mode = MachineMode::Live;
    std::string refusal;
    {
        std::shared_lock<std::shared_mutex> ports(_portsMutex);
        mode = ModeLocked();
        const PolicyDecision d = Decide(op, mode);
        if (_retired.load())
            refusal = kRetiredReason;
        else if (d.refused)
            refusal = d.reason;
        else if (d.effect != SessionEffect::None || d.hold || d.atFrameBoundary || d.endsPlayback)
            refusal = std::string("'") + OpKindName(op.kind) + "' needs the coordinator's park or session effect";
    }
    if (!refusal.empty())
        return Finish(op, mode, OpStatus::Refused, std::move(refusal), SessionEffect::None);

    OpStage stage;
    bool ok = false;
    try
    {
        ok = commit(stage);
    }
    catch (const std::exception& e)
    {
        stage.error = e.what();
    }
    catch (...)
    {
        stage.error = "the operation threw";
    }
    OpResult result;
    if (!ok)
        result = Finish(op, mode, OpStatus::Failed, stage.error.empty() ? std::string("the operation failed") : stage.error,
                        SessionEffect::None);
    else
        result = Finish(op, mode, OpStatus::Done, stage.Unchanged() ? std::string("no change") : stage.detail,
                        SessionEffect::None);
    result.output = std::move(stage.output);
    return result;
}

ModeState MachineCoordinator::Published() const
{
    std::lock_guard<std::recursive_mutex> lock(_modeMutex);
    return _published;
}

OpResult MachineCoordinator::WouldRefuse(const MachineOp& op) const
{
    std::shared_lock<std::shared_mutex> ports(_portsMutex);
    return WouldRefuseLocked(op);
}

OpResult MachineCoordinator::Ask(MachineOp op)
{
    if (op.source == OpSource::Unknown)
        op.source = CurrentSource();
    std::shared_lock<std::shared_mutex> ports(_portsMutex);
    OpResult result = WouldRefuseLocked(op);
    if (result.status == OpStatus::Refused)
        result = Finish(op, ModeLocked(), OpStatus::Refused, std::move(result.reason), SessionEffect::None);
    return result;
}

OpResult MachineCoordinator::WouldRefuseLocked(const MachineOp& op) const
{
    OpResult result;
    const PolicyDecision d = Decide(op, ModeLocked());
    const size_t index = static_cast<size_t>(op.kind);
    IOpOwner* owner = index < _owners.size() ? _owners[index] : nullptr;
    if (_retired.load())
        result.reason = kRetiredReason;
    else if (d.refused)
        result.reason = d.reason;
    else if (owner == nullptr)
        result.reason = std::string("nothing on this machine does '") + OpKindName(op.kind) + "'";
    else
        result.reason = owner->Refusal(op);
    result.status = result.reason.empty() ? OpStatus::Done : OpStatus::Refused;
    if (result.status == OpStatus::Done)
        result.sessionEffect = d.effect;
    return result;
}

OpResult MachineCoordinator::Run(MachineOp op)
{
    if (op.source == OpSource::Unknown)
        op.source = CurrentSource();
    const RunDepth depth;
    IMachineThread* thread = nullptr;
    IPlaybackSink* playback = nullptr;
    {
        std::shared_lock<std::shared_mutex> ports(_portsMutex);
        thread = _thread;
        playback = _playback;
    }
    const bool onMachine = thread == nullptr || thread->OnMachineThread();

    // Park BEFORE taking the coordinator: an operation that held the coordinator while waiting for the machine
    // would wait forever on a machine-thread operation waiting for the coordinator. The rules and the owner are
    // asked once without the lock (a refusal parks nothing) and again under it (a concurrent operation may have
    // changed the mode); a hold found missing then is taken with the coordinator released
    MachineMode mode = MachineMode::Live;
    PolicyDecision d;
    IOpOwner* owner = nullptr;
    std::string refusal;
    auto assess = [&]() {
        mode = ModeLocked();
        d = Decide(op, mode);
        owner = d.refused ? nullptr : _owners[static_cast<size_t>(op.kind)];   // Decide refuses a kind out of range
        if (_retired.load())
            refusal = kRetiredReason;
        else if (d.refused)
            refusal = d.reason;
        else if (owner == nullptr)
            refusal = std::string("nothing on this machine does '") + OpKindName(op.kind) + "'";
        else
            refusal = owner->Refusal(op);
    };
    auto needsPark = [&]() { return refusal.empty() && d.hold && !onMachine; };

    ParkLease lease;
    {
        std::shared_lock<std::shared_mutex> ports(_portsMutex);
        assess();
    }
    // A playback the operation ends stops first, holding the machine itself: never inside the park or the lock (its
    // own lock may be held by a thread waiting for the machine). A nested operation leaves it to the outermost one
    if (refusal.empty() && d.endsPlayback && playback && RunDepth::Outermost())
        playback->EndPlayback(op);
    if (needsPark())
        lease.Take(thread, OpKindName(op.kind));
    std::unique_lock<std::recursive_mutex> lock(_opMutex);
    assess();
    while (needsPark() && !lease.Tried())
    {
        lock.unlock();
        lease.Take(thread, OpKindName(op.kind));
        lock.lock();
        assess();
    }
    if (!refusal.empty())
        return Finish(op, mode, OpStatus::Refused, std::move(refusal), SessionEffect::None);

    ISessionSink* session = _session;
    const std::string reason = op.sessionReason.empty() ? OpKindName(op.kind) : op.sessionReason;
    SessionEffect applied = SessionEffect::None;
    OpStage stage;
    stage._atFrameBoundary = d.atFrameBoundary;
    stage._onCommitPoint = [&]() {
        if (d.effect != SessionEffect::None && session)
        {
            session->Apply(d.effect, reason);
            applied = d.effect;
        }
    };

    auto failed = [&](const char* fallback) {
        try
        {
            owner->Abort(op, stage);
        }
        catch (const std::exception&)
        {
        }
        OpResult result =
            Finish(op, mode, OpStatus::Failed, stage.error.empty() ? std::string(fallback) : stage.error, applied);
        result.output = std::move(stage.output);
        return result;
    };

    std::string note;
    try
    {
        if (!owner->Prepare(op, stage))
            return failed("the operation could not be prepared");
        if (stage.CommitPointReached())
            note = "the owner marked its commit point in Prepare";
        if (!owner->Commit(op, stage))
            return failed("the operation failed");
    }
    catch (const std::exception& e)
    {
        stage.error = e.what();
        return failed("the operation threw");
    }
    catch (...)
    {
        return failed("the operation threw");
    }

    // A session effect is never lost: an owner that changed the machine without marking its commit point gets
    // it now, after the change (a contract test catches such an owner)
    if (stage.Unchanged())
        note = "no change";
    else if (!stage.CommitPointReached())
    {
        stage.CommitPoint();
        note = "the owner did not mark its commit point: the session effect came after the change";
    }
    if (note.empty())
        note = stage.detail;
    if (d.machineResetAfter && session && !stage.Unchanged())
        session->MachineReset();

    OpResult result = Finish(op, mode, OpStatus::Done, std::move(note), applied);
    result.output = std::move(stage.output);
    return result;
}

OpResult MachineCoordinator::Finish(const MachineOp& op, MachineMode mode, OpStatus status, std::string detail,
                                    SessionEffect applied)
{
    OperationLogEntry entry;
    entry.time = _clock ? _clock->Now() : MachineTime{};
    entry.kind = op.kind;
    entry.source = op.source;
    entry.mode = mode;
    entry.status = status;
    entry.effect = applied;
    entry.summary = op.summary;
    entry.detail = detail;

    OpResult result;
    result.status = status;
    if (status != OpStatus::Done)
        result.reason = std::move(detail);
    result.sessionEffect = applied;
    result.logSeq = _log.Append(std::move(entry));
    return result;
}
}  // namespace coordinator
