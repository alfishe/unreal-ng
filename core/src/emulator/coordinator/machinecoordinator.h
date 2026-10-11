#pragma once

/// @file machinecoordinator.h
/// @brief The one place every operation on the machine as a whole goes through (requirements §0): it applies
/// the rules, parks the machine, marks the session effect at the commit point and logs the outcome. Callers
/// hand it an intent; owners implement the operation behind IOpOwner.
///
/// Design: docs/inprogress/2026-10-09-machine-state-coordinator/architecture.md. Wiring: the emulator's
/// WireCoordinator (the only place that knows both sides). Includes: its own directory and the standard
/// library only (requirements N7, enforced by the library's include path).
///
/// Threading (architecture §9): any thread may call Run, WouldRefuse, Mode and Log. Operations are serialized;
/// an operation on the machine thread runs without a park; a nested Run on the same thread (an owner's
/// composite operation) runs inline. Wiring calls (SetMachine, SetSession, Own, Disown) wait for a running
/// operation and are meant for start-up and teardown.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "machineop.h"
#include "machinepolicy.h"
#include "operationlog.h"
#include "ports.h"

namespace coordinator
{
class MachineCoordinator : public IMachineOps
{
public:
    MachineCoordinator() = default;
    MachineCoordinator(const MachineCoordinator&) = delete;
    MachineCoordinator& operator=(const MachineCoordinator&) = delete;

    // Wiring
    void SetMachine(IMachineThread* thread, IMachineClock* clock);
    void SetSession(ISessionSink* session);
    void SetPlayback(IPlaybackSink* playback);
    /// @p owner implements @p kind; false (and logged as a wiring error) when the kind has an owner already
    bool Own(OpKind kind, IOpOwner* owner);
    /// @p owner is never called again once this returns (a running operation finishes first)
    void Disown(IOpOwner* owner);
    /// @p listener hears every mode transition from now on, after the listeners registered before it; it hears
    /// nothing once Unlisten returns
    void Listen(IModeListener* listener);
    void Unlisten(IModeListener* listener);
    /// The machine was removed (Emulator::Retire): every operation is refused from now on - a late GUI action or a
    /// request in flight changes nothing and writes no file. One way
    void Retire() { _retired.store(true); }
    static constexpr const char* kRetiredReason = "the machine was removed";

    // Operations
    /// Runs @p op: the rules, the park, the owner's Prepare and Commit, the session effect at the commit point,
    /// the log. Blocks until done, refused or failed
    OpResult Run(MachineOp op) override;
    /// What Run would answer about refusing @p op, without side effects (status Done: it would go ahead;
    /// sessionEffect: what the session would see)
    OpResult WouldRefuse(const MachineOp& op) const override;
    /// WouldRefuse for a surface that asks before its own preparation; a refusal is logged like Run's
    OpResult Ask(MachineOp op) override;
    MachineMode Mode() const override;
    void SessionChanged() override;
    ModeState Published() const override;
    void Note(MachineOp op, OpStatus status, std::string detail) override;
    OpResult RunOwnLocked(MachineOp op, const std::function<bool(OpStage&)>& commit) override;
    const OperationLog& Log() const { return _log; }

private:
    MachineMode ModeLocked() const;
    OpResult WouldRefuseLocked(const MachineOp& op) const;
    OpResult Finish(const MachineOp& op, MachineMode mode, OpStatus status, std::string detail,
                    SessionEffect applied);

    mutable std::shared_mutex _portsMutex;   ///< the ports, for WouldRefuse and Mode; written with _opMutex held
    std::recursive_mutex _opMutex;           ///< one operation at a time; recursive for nested operations
    std::atomic<bool> _retired{false};
    IMachineThread* _thread = nullptr;
    IMachineClock* _clock = nullptr;
    ISessionSink* _session = nullptr;
    IPlaybackSink* _playback = nullptr;
    std::array<IOpOwner*, static_cast<size_t>(OpKind::Count)> _owners{};
    OperationLog _log;
    /// The published mode and its listeners. Recursive: a listener may report again (the transition it causes is
    /// published before it returns)
    mutable std::recursive_mutex _modeMutex;
    std::vector<IModeListener*> _listeners;
    ModeState _published;
    std::atomic<ISessionSink*> _modeSession{nullptr};   ///< _session, for SessionChanged
};
}  // namespace coordinator
