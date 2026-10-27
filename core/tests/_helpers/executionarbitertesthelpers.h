#pragma once

// The pieces the execution arbiter tests share (unit: emulator/execution/executionarbiter_test.cpp, functional:
// functional/emulator/execution/executionarbitertiming_functional_test.cpp): the requesters, a monitor that counts
// concurrent drivers, a fake main loop and a command body that blocks until released

#include <atomic>
#include <functional>
#include <thread>

#include "_helpers/testthread.h"
#include "_helpers/testwaithelper.h"
#include "emulator/execution/executionarbiter.h"

namespace arbitertest
{
using namespace execution;

const Requester kGui{Origin::Gui, {}};
const Requester kApi{Origin::WebApi, {}};
const Requester kTtd{Origin::Ttd, {}};
const Requester kGdb{Origin::Gdb, "1"};
const Requester kDezog{Origin::DeZog, "a"};

/// Counts threads that execute "Z80 instructions" or a command body at once: more than one is the bug
struct DriveMonitor
{
    std::atomic<int> active{0};
    std::atomic<int> violations{0};

    void Enter()
    {
        if (active.fetch_add(1, std::memory_order_acq_rel) != 0)
            violations.fetch_add(1, std::memory_order_relaxed);
    }
    void Leave() { active.fetch_sub(1, std::memory_order_acq_rel); }
};

/// A main loop: executes "instructions" while the arbiter lets it, parks when told
class FakeLoop
{
public:
    FakeLoop(ExecutionArbiter& arbiter, DriveMonitor& monitor) : _arbiter(arbiter), _monitor(monitor)
    {
        _thread = std::thread([this]() { Run(); });
        TestWait::For([this]() { return _attached.load(); });
    }
    ~FakeLoop() { Stop(); }

    void Stop()
    {
        if (!_thread.joinable())
            return;
        _arbiter.BeginStop();
        _thread.join();
    }

    std::thread::id Id() const { return _thread.get_id(); }
    uint64_t Instructions() const { return _instructions.load(); }

    /// Runs on the loop's thread at its next instruction boundary (a "hook")
    std::function<void()> hook;
    std::atomic<bool> hookPending{false};

private:
    void Run()
    {
        _arbiter.AttachMainLoop();
        _attached = true;
        while (_arbiter.LoopWait())
        {
            while (!_arbiter.ShouldYield())
            {
                _monitor.Enter();
                _instructions.fetch_add(1, std::memory_order_relaxed);
                _monitor.Leave();
                if (hookPending.exchange(false) && hook)
                    hook();
                std::this_thread::yield();
            }
        }
        _arbiter.DetachMainLoop();
    }

    ExecutionArbiter& _arbiter;
    DriveMonitor& _monitor;
    std::thread _thread;
    std::atomic<bool> _attached{false};
    std::atomic<uint64_t> _instructions{0};
};

/// A command body that blocks until released (a long journal allocation, a slow media insert)
struct Gate
{
    std::atomic<bool> entered{false};
    std::atomic<bool> open{false};

    CommandBody Body(DriveMonitor& monitor)
    {
        return [this, &monitor](CommandContext& ctx) {
            monitor.Enter();
            entered = true;
            TestWait::For([this, &ctx]() { return open.load() || ctx.IsAborted(); });
            monitor.Leave();
        };
    }
};

}  // namespace arbitertest
