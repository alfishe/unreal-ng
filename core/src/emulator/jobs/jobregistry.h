#pragma once

/// @file jobregistry.h
/// @brief Jobs: the long verbs (an analysis build, a TTD dump / load, probe-frames, memory-series, find-event, ...)
/// run off the caller's thread, so a client never loses work to an HTTP timeout and never sleep-polls (PLAN #122 S2,
/// docs/emulator/design/control-interfaces/jobs.md). One registry for every kind: a verb hands a Spec (its work as a
/// function) to Submit and answers {job, state} at once; any plane then reads the job (Get / List), waits for it
/// (Wait blocks a script's thread; Watch calls back without a thread, for the WebAPI long-poll) or cancels it.
///
/// - States: queued -> running -> done | failed | cancelled (a queued job cancelled never runs).
/// - Lanes: the jobs of one lane run one at a time in submission order (a TTD job of an instance waits for the one
///   before it); lanes run side by side on a small pool of workers (Limits::workers).
/// - Progress: the work reports done / total (Context::Progress, or CurrentProgress from deep inside a verb), a
///   search also the hits it has so far; the
///   instance's `jobs` readouts are told at most every kProgressNotifyMs per job, state changes at once (the
///   WebSocket topics jobs.progress / jobs.done read List, published state only: a short lock, never the worker).
/// - Cancel: a queued job ends cancelled; a running one is asked (CancelRequested, Spec::onCancel) and ends when its
///   work returns. Work that cannot stop says so (Spec::cancellable false): its cancel is refused.
/// - Retention: finished jobs stay readable, at most Limits::keepFinished of them and for Limits::keepMs; results
///   over Limits::keepResultBytes in all lose their payload oldest first (the job says result_dropped).
/// - Bounded: at most Limits::maxQueued jobs waiting, Limits::maxWatches long-polls at once.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "emulator/state/statenode.h"

namespace jobs
{

enum class JobState : uint8_t
{
    Queued,
    Running,
    Done,
    Failed,
    Cancelled,
};

const char* StateName(JobState state);
inline bool IsFinal(JobState state) { return state != JobState::Queued && state != JobState::Running; }

/// What a job's work returns
struct Outcome
{
    bool ok = true;          ///< done; false: failed
    bool cancelled = false;  ///< the work stopped early on a cancel: the job ends cancelled (result: what it got)
    int status = 200;        ///< the HTTP status the call would have answered at once
    std::string error;       ///< the failure, one sentence (failed / cancelled)
    std::string text;        ///< the readable answer (what the CLI prints, what MCP's text carries)
    StateNode result;        ///< the verb's answer
};

class Registry;

/// Handed to the work: the cancel request and the progress
class Context
{
public:
    uint64_t Id() const { return _id; }
    bool CancelRequested() const { return _cancel->load(std::memory_order_relaxed); }
    /// @p done of @p total units (frames, rows; @p unit names them). Cheap: stores, the readouts are told at most
    /// every kProgressNotifyMs
    void Progress(uint64_t done, uint64_t total, const char* unit = nullptr);
    /// The same for a search: @p hits found so far (the job's progress shows them)
    void Progress(uint64_t done, uint64_t total, const char* unit, uint64_t hits);

private:
    friend class Registry;
    Context(Registry& registry, uint64_t id, std::shared_ptr<std::atomic<bool>> cancel)
        : _registry(registry), _id(id), _cancel(std::move(cancel))
    {
    }
    Registry& _registry;
    uint64_t _id;
    std::shared_ptr<std::atomic<bool>> _cancel;
};

/// The job the calling thread runs (null on any other thread): a verb's inner loop polls it without knowing it runs
/// as a job
Context* Current();
inline bool CurrentCancelRequested()
{
    const Context* context = Current();
    return context != nullptr && context->CancelRequested();
}
inline void CurrentProgress(uint64_t done, uint64_t total, const char* unit = nullptr)
{
    if (Context* context = Current())
        context->Progress(done, total, unit);
}

struct Spec
{
    std::string kind;          ///< "ttd.dump", "analysis.build", ...
    std::string emulatorId;    ///< the instance it works on ("" : none)
    std::string lane;          ///< one at a time per lane ("" : the kind and the instance)
    std::string summary;       ///< what it does, one line ("dump path=/tmp/a.ttd")
    bool cancellable = true;   ///< the work polls CancelRequested (or onCancel stops it)
    std::function<void()> onCancel;   ///< a cancel of the running job: tell the work (any thread, no registry lock)
    std::function<void()> onSkipped;  ///< the job ended before it ran (cancelled while queued, the registry stops)
    std::function<Outcome(Context&)> work;
};

/// A job as every plane shows it
struct Info
{
    uint64_t id = 0;
    std::string kind;
    std::string emulatorId;
    std::string summary;
    JobState state = JobState::Queued;
    uint64_t done = 0;
    uint64_t total = 0;
    std::string unit;
    bool hasHits = false;   ///< the work is a search that reports its hits so far
    uint64_t hits = 0;
    bool cancellable = true;
    bool cancelRequested = false;
    int64_t createdMs = 0;  ///< wall clock, ms since the epoch
    int64_t startedMs = 0;  ///< 0: not started
    int64_t endedMs = 0;    ///< 0: not ended
    uint64_t queuePosition = 0;  ///< queued: jobs ahead of it in its lane (0: next)
    bool resultDropped = false;  ///< the result's payload went over the retention budget
    Outcome outcome;             ///< final jobs only

    uint32_t Percent() const { return total == 0 ? 0 : static_cast<uint32_t>(done >= total ? 100 : done * 100 / total); }
    /// {job, kind, emulator_id, summary, state, progress {done, total, unit, percent, hits?}, cancellable, cancel_requested,
    /// created_ms, started_ms, ended_ms, elapsed_ms, queue_position?; final: status, error, text, result?}
    StateNode ToNode(bool withResult = true) const;
    /// One line ("job 12 ttd.dump on <id>: running 45% (450 of 1000 frames)"); final: the outcome's text below
    std::string Text(bool withResult = true) const;
};

struct Limits
{
    size_t workers = 4;         ///< jobs running at once (lanes side by side)
    size_t maxQueued = 64;      ///< jobs waiting to run; a Submit over it is refused
    size_t keepFinished = 64;   ///< finished jobs kept readable
    uint64_t keepMs = 30ull * 60 * 1000;  ///< and for at most this long
    size_t keepResultBytes = 64ull * 1024 * 1024;  ///< results kept in all (approximate), oldest dropped first
    size_t maxWatches = 256;    ///< long-polls waiting at once
};

/// The longest wait / long-poll one call may ask for (ms): under the MCP bridge's 300 s and every client's timeout
inline constexpr uint64_t kMaxWaitMs = 60000;
/// The readouts of a job's progress are told at most this often
inline constexpr int64_t kProgressNotifyMs = 100;

class Registry
{
public:
    static Registry& Instance();
    /// False once the process-wide registry is destroyed (an instance removal at exit must not reach it)
    static bool Alive();

    Registry();
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    /// Queue a job: its id, or 0 with @p error (the queue is full, the registry stops, no work)
    uint64_t Submit(Spec spec, std::string& error);
    std::optional<Info> Get(uint64_t id) const;
    /// The jobs of @p emulatorId ("" : all), oldest first; @p activeOnly: queued and running only
    std::vector<Info> List(const std::string& emulatorId = {}, bool activeOnly = false) const;

    enum class CancelResult : uint8_t
    {
        Cancelled,       ///< it was queued: cancelled, it never runs
        Requested,       ///< it runs: asked to stop, it ends when its work returns
        NotCancellable,  ///< it runs and its work cannot stop
        Finished,        ///< it had ended already
        NotFound,
    };
    CancelResult Cancel(uint64_t id);
    /// The removal of an instance: its queued jobs cancelled, its running ones asked to stop
    void CancelInstance(const std::string& emulatorId);

    /// Block up to @p timeout for the job to end; its info then (nullopt: no such job)
    std::optional<Info> Wait(uint64_t id, std::chrono::milliseconds timeout) const;

    enum class WatchResult : uint8_t
    {
        Watching,  ///< @p notify runs once when the job ends (on the thread that ends it)
        Ended,     ///< it has ended: answer now (notify is not called)
        Busy,      ///< Limits::maxWatches long-polls wait already: answer now
        NotFound,
    };
    /// A long-poll without a thread: @p notify runs once when the job ends. @p token for Unwatch (the timeout)
    WatchResult Watch(uint64_t id, std::function<void(const Info&)> notify, uint64_t& token);
    /// The long-poll timed out: true when the watch was still there (notify will not run), false when it fired
    bool Unwatch(uint64_t id, uint64_t token);
    size_t WatchCount() const;

    void SetLimits(const Limits& limits);
    Limits GetLimits() const;
    /// Who hears that an instance's jobs changed (default: NotifyReadoutsChanged(id, "jobs")); tests count
    void SetNotifier(std::function<void(const std::string& emulatorId)> notifier);

private:
    friend class Context;
    struct Job;
    using Clock = std::chrono::steady_clock;

    void WorkerLoop();
    std::shared_ptr<Job> NextRunnableLocked();
    /// Remove a queued job from the queue so no worker starts it (false: not in it, another call took it)
    bool TakeQueuedLocked(const std::shared_ptr<Job>& job);
    void FinishCancelledQueued(const std::shared_ptr<Job>& job);
    void Finish(const std::shared_ptr<Job>& job, Outcome outcome, bool ran);
    void PruneLocked(std::vector<std::shared_ptr<Job>>& dropped);
    Info InfoLocked(const Job& job) const;
    void ReportProgress(uint64_t id, uint64_t done, uint64_t total, const char* unit, const uint64_t* hits);
    void Notify(const std::string& emulatorId);
    void EnsureWorkersLocked();

    mutable std::mutex _mutex;
    std::condition_variable _wake;          ///< workers: a job to run, or stop
    mutable std::condition_variable _ended; ///< Wait: a job ended
    std::map<uint64_t, std::shared_ptr<Job>> _jobs;
    std::deque<std::shared_ptr<Job>> _queue;
    std::set<std::string> _busyLanes;
    std::vector<std::thread> _workers;
    size_t _watches = 0;
    size_t _idle = 0;  ///< workers waiting for a job
    uint64_t _nextId = 1;
    uint64_t _nextToken = 1;
    bool _stopping = false;
    bool _global = false;  ///< the process-wide registry (Instance)
    Limits _limits;
    std::function<void(const std::string&)> _notifier;
};

/// The approximate memory a StateNode holds (retention budget)
size_t ApproxBytes(const StateNode& node);

}  // namespace jobs
