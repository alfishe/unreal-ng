#pragma once

/// @file analysisjobs.h
/// @brief The long operation of the analysis layers: `analysis build` replays a TraceBundle on a worker thread
/// (ReplayBuild, replaytracer.h) and keeps the L1 it made, one job and one result per source instance. The verbs
/// (analysiscontrol.h) start it and read it; the WebSocket topics `analysis.progress` and `analysis.ready`
/// (monitoringapi.h) read JobView, which is published state only (a short lock, no wait for the worker).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "debugger/analyzers/analysis/replaytracer.h"

namespace analysis
{

struct JobView
{
    uint64_t id = 0;              ///< 0: no build yet
    std::string state = "none";   ///< none, running, done, failed, cancelled
    uint64_t framesDone = 0;
    uint64_t framesTotal = 0;
    uint32_t percent = 0;
    std::string error;
    std::string path;             ///< where the L1 JSON was written ("" : not written)
    uint64_t hostMicros = 0;
    bool ready = false;           ///< an L1 result is there (this job's or an earlier one's)
    uint64_t readyJob = 0;
    uint64_t routines = 0;
    uint64_t blocks = 0;
    uint64_t loops = 0;
    uint64_t instructions = 0;
    uint64_t frames = 0;
    uint32_t checked = 0;
    uint32_t diverged = 0;
    StateNode bundle;             ///< TraceBundle::ToStateNode of the job

    /// The `analysis.progress` / `analysis.ready` values
    StateNode ProgressNode() const;
    StateNode ReadyNode() const;
};

class L1Jobs
{
public:
    static L1Jobs& Instance();
    ~L1Jobs();

    /// Start a build of @p bundle for @p emulatorId; false with @p error when one is running
    bool Start(const std::string& emulatorId, TraceBundle bundle, ReplayOptions options, const std::string& path,
               uint64_t& id, std::string& error);
    JobView View(const std::string& emulatorId) const;
    std::shared_ptr<const L1Result> Result(const std::string& emulatorId) const;
    /// Wait for the running build (true: it ended within @p timeoutMs)
    bool Wait(const std::string& emulatorId, int timeoutMs) const;
    bool Cancel(const std::string& emulatorId);
    /// Cancel, join and drop what an instance has (its removal)
    void Forget(const std::string& emulatorId);

    /// `analysis start record=true` started the TTD recording of this instance (stop ends it)
    void NoteRecording(const std::string& emulatorId, bool owned);
    bool OwnsRecording(const std::string& emulatorId) const;

private:
    struct Job
    {
        uint64_t id = 0;
        std::thread thread;
        std::atomic<uint64_t> done{0};
        std::atomic<uint64_t> total{0};
        std::atomic<bool> cancel{false};
        std::atomic<bool> finished{false};
        mutable std::mutex mutex;
        mutable std::condition_variable ended;
        std::string state = "running";  ///< under mutex
        std::string error;
        std::string path;
        uint64_t hostMicros = 0;
        StateNode bundle;
    };
    struct Slot
    {
        std::shared_ptr<Job> job;
        std::shared_ptr<const L1Result> result;
        uint64_t resultJob = 0;
    };

    void Run(const std::string& emulatorId, std::shared_ptr<Job> job, TraceBundle bundle, ReplayOptions options);

    mutable std::mutex _mutex;
    std::map<std::string, Slot> _slots;
    std::set<std::string> _recordings;
    std::atomic<uint64_t> _nextId{1};
};

}  // namespace analysis
