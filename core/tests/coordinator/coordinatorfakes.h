#pragma once

/// @file coordinatorfakes.h
/// @brief Fakes for every port of the machine state coordinator (architecture §10.1): the coordinator runs
/// without an emulator. Every fake writes what it was asked into one shared call log, so a test is one
/// comparison of the expected sequence of calls across all of them.

#include <atomic>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "emulator/coordinator/ports.h"

namespace coordinator::fakes
{
class CallLog
{
public:
    void Add(std::string call)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _calls.push_back(std::move(call));
    }
    std::vector<std::string> Take()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return std::exchange(_calls, {});
    }

private:
    std::mutex _mutex;
    std::vector<std::string> _calls;
};

class FakeMachineThread : public IMachineThread
{
public:
    explicit FakeMachineThread(CallLog* log = nullptr) : _log(log) {}

    bool OnMachineThread() const override { return onMachineThread; }
    bool Park(const char* tag) override
    {
        ++parks;
        if (_log)
            _log->Add(std::string("park:") + tag);
        return parkSucceeds;
    }
    void Unpark() override
    {
        ++unparks;
        if (_log)
            _log->Add("unpark");
    }

    bool onMachineThread = false;
    bool parkSucceeds = true;
    std::atomic<int> parks{0};
    std::atomic<int> unparks{0};

private:
    CallLog* _log;
};

class FakeClock : public IMachineClock
{
public:
    MachineTime Now() const override { return {++_frame, 7, 1000 + _frame}; }

private:
    mutable std::atomic<uint64_t> _frame{0};
};

class FakeSession : public ISessionSink
{
public:
    explicit FakeSession(CallLog* log = nullptr) : _log(log) {}

    MachineMode Mode() const override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!modes.empty())
        {
            const MachineMode next = modes.front();
            modes.erase(modes.begin());
            return next;
        }
        return mode;
    }
    void Apply(SessionEffect effect, const std::string& reason) override
    {
        if (_log)
            _log->Add(std::string("session:") + SessionEffectName(effect) + ":" + reason);
    }
    void MachineReset() override
    {
        if (_log)
            _log->Add("session:machine-reset");
    }

    MachineMode mode = MachineMode::Live;
    /// Answers Mode() with these first, one per call (a mode that changes between two reads)
    mutable std::vector<MachineMode> modes;
    bool replaying = false;
    bool Replaying() const override { return replaying; }

private:
    CallLog* _log;
    mutable std::mutex _mutex;
};

/// A mode listener: each transition into the shared call log as "<name>:<from>-><to>[:replaying]"
class FakeListener : public IModeListener
{
public:
    FakeListener(CallLog* log, std::string name) : _log(log), _name(std::move(name)) {}

    void OnModeChanged(const ModeState& from, const ModeState& to) override
    {
        _log->Add(_name + ":" + MachineModeName(from.mode) + "->" + MachineModeName(to.mode) +
                  (to.replaying ? ":replaying" : ""));
    }

private:
    CallLog* _log;
    std::string _name;
};

/// An input playback (RZX): "playback:end:<kind>" into the shared call log
class FakePlayback : public IPlaybackSink
{
public:
    explicit FakePlayback(CallLog* log) : _log(log) {}
    void EndPlayback(const MachineOp& op) override { _log->Add(std::string("playback:end:") + OpKindName(op.kind)); }

private:
    CallLog* _log;
};

class FakeOwner : public IOpOwner
{
public:
    explicit FakeOwner(CallLog* log = nullptr, std::string name = "owner") : _log(log), _name(std::move(name)) {}

    std::string Refusal(const MachineOp&) const override { return refusal; }
    bool Prepare(const MachineOp&, OpStage& stage) override
    {
        Add("prepare");
        if (throwInPrepare)
            throw std::runtime_error("prepare threw");
        if (!prepareSucceeds)
            stage.error = "bad file";
        else
            stage.data = std::string("staged");
        return prepareSucceeds;
    }
    bool Commit(const MachineOp& op, OpStage& stage) override
    {
        Add("commit");
        lastAtFrameBoundary = stage.AtFrameBoundary();
        if (inCommit.exchange(true))
            overlappingCommits++;
        if (failBeforeCommitPoint)
        {
            inCommit = false;
            stage.error = "commit failed";
            return false;
        }
        if (changesNothing)
        {
            stage.NoChange();
            inCommit = false;
            return true;
        }
        if (marksCommitPoint)
            stage.CommitPoint();
        Add("change");
        if (onCommit)
            onCommit(op);
        stage.output = std::string("result");
        inCommit = false;
        if (throwAfterCommitPoint)
            throw std::runtime_error("commit threw");
        if (failAfterCommitPoint)
            stage.error = "commit failed late";
        return !failAfterCommitPoint;
    }
    void Abort(const MachineOp&, OpStage&) override { Add("abort"); }

    std::string refusal;
    bool prepareSucceeds = true;
    bool throwInPrepare = false;
    bool marksCommitPoint = true;
    bool changesNothing = false;
    bool failBeforeCommitPoint = false;
    bool failAfterCommitPoint = false;
    bool throwAfterCommitPoint = false;
    std::function<void(const MachineOp&)> onCommit;
    bool lastAtFrameBoundary = false;   ///< what the last Commit's stage said (OpStage::AtFrameBoundary)
    std::atomic<bool> inCommit{false};
    std::atomic<int> overlappingCommits{0};

private:
    void Add(const char* what)
    {
        if (_log)
            _log->Add(_name + ":" + what);
    }

    CallLog* _log;
    std::string _name;
};
}  // namespace coordinator::fakes
