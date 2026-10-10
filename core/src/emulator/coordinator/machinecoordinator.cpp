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

OpResult MachineCoordinator::WouldRefuse(const MachineOp& op) const
{
    std::shared_lock<std::shared_mutex> ports(_portsMutex);
    OpResult result;
    const PolicyDecision d = Decide(op, ModeLocked());
    const size_t index = static_cast<size_t>(op.kind);
    IOpOwner* owner = index < _owners.size() ? _owners[index] : nullptr;
    if (d.refused)
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
    IMachineThread* thread = nullptr;
    {
        std::shared_lock<std::shared_mutex> ports(_portsMutex);
        thread = _thread;
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
        if (d.refused)
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
