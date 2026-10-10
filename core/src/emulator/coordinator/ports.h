#pragma once

/// @file ports.h
/// @brief Everything the coordinator knows about the rest of the emulator (architecture §3.1).
///
/// A participant includes this header and implements the port it has; it never includes
/// machinecoordinator.h and never calls another participant for a machine-level operation. Tests implement
/// every port with fakes, so the coordinator runs without an emulator (requirements N8).

#include <functional>
#include <string>
#include <utility>

#include "machineop.h"

namespace coordinator
{
/// The machine's thread: who may change the machine, and how to stop it for a while (the main loop and the
/// execution arbiter's CPU hold)
class IMachineThread
{
public:
    virtual ~IMachineThread() = default;
    /// The caller is the thread that executes the machine (traps, devices): it needs no park
    virtual bool OnMachineThread() const = 0;
    /// Bring the machine to a coherent point and keep it there; true: held, Unpark must follow. Nested holds on
    /// one thread are free
    virtual bool Park(const char* tag) = 0;
    virtual void Unpark() = 0;
};

/// The machine's time, for the operation log
class IMachineClock
{
public:
    virtual ~IMachineClock() = default;
    virtual MachineTime Now() const = 0;
};

/// What an owner carries from Prepare to Commit, and the commit point it marks
class OpStage
{
public:
    /// The owner's staged work (an opened medium, a parsed snapshot)
    std::any data;
    /// Failed: why, in one sentence
    std::string error;
    /// Handed back to the caller in OpResult::output
    std::any output;

    /// The owner calls this once, during Commit, right before it changes the machine. The coordinator applies
    /// the session effect here (D42: the session ends only when the operation really goes ahead)
    void CommitPoint()
    {
        if (_reached)
            return;
        _reached = true;
        if (_onCommitPoint)
            _onCommitPoint();
    }
    bool CommitPointReached() const { return _reached; }
    /// The owner found nothing to change after all (nothing to eject, a disk with nothing to boot), or a nested
    /// operation made the change and was logged itself: Commit succeeds without a commit point, and the session
    /// sees nothing
    void NoChange() { _noChange = true; }
    bool Unchanged() const { return _noChange && !_reached; }

private:
    friend class MachineCoordinator;
    std::function<void()> _onCommitPoint;
    bool _reached = false;
    bool _noChange = false;
};

/// The one owner of an operation kind: it implements the operation (architecture §2.1)
class IOpOwner
{
public:
    virtual ~IOpOwner() = default;
    /// Owner-specific refusal ("no slot 'fdd9'"); empty: allowed. No side effects
    virtual std::string Refusal(const MachineOp&) const { return {}; }
    /// Everything that can fail, before anything changes (open and identify the file, check the slot). False:
    /// stage.error says why; the machine and the session are untouched
    virtual bool Prepare(const MachineOp&, OpStage&) { return true; }
    /// Apply. Call stage.CommitPoint() right before the first change (or stage.NoChange()). False: failed,
    /// stage.error says why
    virtual bool Commit(const MachineOp& op, OpStage& stage) = 0;
    /// Prepare or Commit failed: drop what was staged
    virtual void Abort(const MachineOp&, OpStage&) {}
};

/// The TTD controller, as the rules see it
class ISessionSink
{
public:
    virtual ~ISessionSink() = default;
    /// What the machine is doing (any thread)
    virtual MachineMode Mode() const = 0;
    /// The policy's session effect, once per operation, at its commit point
    virtual void Apply(SessionEffect effect, const std::string& reason) = 0;
    /// The machine was reset: a machine seeked into recorded history leaves it (the timeline is kept)
    virtual void MachineReset() = 0;
};

/// The coordinator as callers see it: the surfaces, and an owner whose operation runs another one (a disk's
/// discard re-inserts it). Implemented by MachineCoordinator; a caller never needs its class
class IMachineOps
{
public:
    virtual ~IMachineOps() = default;
    /// Runs @p op: the rules, the park, the owner, the session effect, the log
    virtual OpResult Run(MachineOp op) = 0;
    /// What Run would answer about refusing @p op, without side effects
    virtual OpResult WouldRefuse(const MachineOp& op) const = 0;
    /// The same question from a surface about to prepare the operation itself (a file dialog, an upload, a staged
    /// file): a refusal is logged like Run's, so every refusal is in the log; a yes is not (Run logs the operation)
    virtual OpResult Ask(MachineOp op) = 0;
    virtual MachineMode Mode() const = 0;
};

/// A change a subsystem makes itself, run as an operation (the network settings, an IDE unit's drive, an RZX
/// restore): the emulator, the owner of these kinds, calls it as the Commit. It calls stage.CommitPoint() right
/// before its first change (or stage.NoChange()); false: failed, stage.error says why
using OpCommit = std::function<bool(OpStage&)>;

/// Run @p op with @p commit as its change through @p ops: the rules, the park, the session effect, the log. Without
/// a coordinator (a bare context in a unit test) the change runs alone: no rules, no log
inline OpResult RunCommit(IMachineOps* ops, MachineOp op, OpCommit commit)
{
    if (ops)
    {
        op.payload = std::move(commit);
        return ops->Run(std::move(op));
    }
    OpStage stage;
    OpResult result;
    if (!commit(stage))
    {
        result.status = OpStatus::Failed;
        result.reason = stage.error;
    }
    result.output = std::move(stage.output);
    return result;
}

}  // namespace coordinator
