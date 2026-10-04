#pragma once

/// @file cleanupmanager.h
/// @brief Housekeeping at startup, off the start-up path: subsystems register
/// cleanup steps (TTD: recordings left behind by a crash), RunAsync() runs the
/// due ones on a background thread and returns at once.
///
/// - Every step is isolated: its exceptions are caught and logged, and a
///   failing step never stops the others. A file it cannot delete (in use, no
///   permission) is the step's to report and skip; the next run tries again.
/// - Each step has an interval (default: a week). The last successful run of
///   every step is kept in <user data>/cleanup-state.txt ("name<TAB>unix
///   seconds" per line), so a step runs at most once per interval however
///   often the emulator starts, and at least once per interval while it does.
/// - Stop() asks a running step to finish early (CleanupContext::StopRequested)
///   and waits; the destructor does it too.
///
/// Worked example: the TTD step is registered with a 7-day interval and last
/// ran 8 days ago; RunAsync() starts it on the cleanup thread, it deletes two
/// crashed recordings older than 7 days, cannot delete a third (a file held
/// open by another program), reports that, and finishes; its run time is
/// recorded and it is not due again for a week.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CleanupContext
{
public:
    explicit CleanupContext(const std::atomic<bool>& stop) : _stop(stop) {}

    /// The manager is stopping (shutdown): finish early, the next run goes on
    bool StopRequested() const { return _stop.load(std::memory_order_relaxed); }
    /// Something removed / something that stayed (with the reason), for the report and the log
    void Removed(const std::string& what);
    void Failed(const std::string& what, const std::string& reason);

    const std::vector<std::string>& RemovedItems() const { return _removed; }
    const std::vector<std::string>& FailedItems() const { return _failed; }

private:
    const std::atomic<bool>& _stop;
    std::vector<std::string> _removed;
    std::vector<std::string> _failed;
};

struct CleanupStep
{
    std::string name;   ///< stable: the key in the state file ("ttd-crashed-recordings")
    std::chrono::seconds interval = std::chrono::hours(24 * 7);
    std::function<void(CleanupContext&)> run;
};

struct CleanupStepReport
{
    std::string name;
    bool ran = false;          ///< due and started
    bool completed = false;    ///< returned without an exception
    std::string error;         ///< the exception's message
    std::vector<std::string> removed;
    std::vector<std::string> failed;
};

class CleanupManager
{
public:
    CleanupManager() = default;
    ~CleanupManager();
    CleanupManager(const CleanupManager&) = delete;
    CleanupManager& operator=(const CleanupManager&) = delete;

    /// The application's manager
    static CleanupManager& Instance();

    /// Add a step (a step with the same name replaces the earlier one)
    void AddStep(CleanupStep step);

    /// Run the due steps on a background thread; returns at once. Ignored while a run is in progress
    void RunAsync();
    /// Run the due steps on this thread (tests, tools)
    std::vector<CleanupStepReport> RunNow();
    /// Wait for a background run to finish
    void Wait();
    /// Ask a background run to finish early, and wait for it
    void Stop();

    /// The reports of the last finished run
    std::vector<CleanupStepReport> LastReports() const;

    /// The state file (default: <user data>/cleanup-state.txt); tests point it elsewhere
    void SetStatePath(const std::string& path);
    /// Clock for the interval check (tests); default: the system clock
    void SetClock(std::function<int64_t()> nowUnixSeconds);

private:
    std::vector<CleanupStepReport> RunDue();
    std::string StatePath() const;

    mutable std::mutex _mutex;     ///< steps, reports, state path, clock
    std::vector<CleanupStep> _steps;
    std::vector<CleanupStepReport> _lastReports;
    std::string _statePath;
    std::function<int64_t()> _now;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _stop{false};
};
