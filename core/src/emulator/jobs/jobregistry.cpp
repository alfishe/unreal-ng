#include "emulator/jobs/jobregistry.h"

#include <algorithm>
#include <exception>
#include <sstream>

#include "emulator/readoutschanged.h"

namespace jobs
{
namespace
{
thread_local Context* tlsCurrent = nullptr;
std::atomic<bool> registryAlive{false};

int64_t WallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t SteadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// "1.2 s", "340 ms"
std::string Duration(int64_t ms)
{
    std::ostringstream out;
    if (ms < 1000)
        out << ms << " ms";
    else
        out << ms / 1000 << "." << (ms % 1000) / 100 << " s";
    return out.str();
}

/// The text kept with a job whose result went over the budget
constexpr size_t kKeptTextChars = 4096;
}  // namespace

const char* StateName(JobState state)
{
    switch (state)
    {
        case JobState::Queued:
            return "queued";
        case JobState::Running:
            return "running";
        case JobState::Done:
            return "done";
        case JobState::Failed:
            return "failed";
        case JobState::Cancelled:
            return "cancelled";
    }
    return "unknown";
}

Context* Current()
{
    return tlsCurrent;
}

void Context::Progress(uint64_t done, uint64_t total, const char* unit)
{
    _registry.ReportProgress(_id, done, total, unit);
}

size_t ApproxBytes(const StateNode& node)
{
    size_t bytes = sizeof(StateNode) + node.s.size();
    for (const auto& [name, member] : node.members)
        bytes += name.size() + ApproxBytes(member);
    for (const StateNode& item : node.items)
        bytes += ApproxBytes(item);
    return bytes;
}

StateNode Info::ToNode(bool withResult) const
{
    StateNode node = StateNode::Object();
    node["job"] = id;
    node["kind"] = kind;
    node["emulator_id"] = emulatorId;
    node["summary"] = summary;
    node["state"] = StateName(state);
    StateNode progress = StateNode::Object();
    progress["done"] = done;
    progress["total"] = total;
    progress["unit"] = unit;
    progress["percent"] = static_cast<uint64_t>(Percent());
    node["progress"] = std::move(progress);
    node["cancellable"] = cancellable;
    node["cancel_requested"] = cancelRequested;
    node["created_ms"] = createdMs;
    node["started_ms"] = startedMs;
    node["ended_ms"] = endedMs;
    const int64_t from = startedMs != 0 ? startedMs : createdMs;
    const int64_t to = endedMs != 0 ? endedMs : WallMs();
    node["elapsed_ms"] = to > from ? to - from : int64_t{0};
    if (state == JobState::Queued)
        node["queue_position"] = queuePosition;
    if (IsFinal(state))
    {
        node["status"] = outcome.status;
        node["error"] = outcome.error;
        node["text"] = outcome.text;
        node["result_dropped"] = resultDropped;
        if (withResult && !resultDropped)
            node["result"] = outcome.result;
    }
    return node;
}

std::string Info::Text(bool withResult) const
{
    std::ostringstream out;
    out << "job " << id << " " << kind;
    if (!emulatorId.empty())
        out << " on " << emulatorId;
    if (!summary.empty())
        out << " (" << summary << ")";
    out << ": " << StateName(state);
    const int64_t from = startedMs != 0 ? startedMs : createdMs;
    const int64_t to = endedMs != 0 ? endedMs : WallMs();
    const int64_t elapsed = to > from ? to - from : 0;
    switch (state)
    {
        case JobState::Queued:
            out << " (" << queuePosition << " ahead in its lane)";
            break;
        case JobState::Running:
            if (total != 0)
                out << " " << Percent() << "% (" << done << " of " << total << (unit.empty() ? "" : " ") << unit << ")";
            else if (done != 0)
                out << " (" << done << (unit.empty() ? "" : " ") << unit << ")";
            out << ", " << Duration(elapsed);
            if (cancelRequested)
                out << ", cancel requested";
            break;
        case JobState::Done:
        case JobState::Failed:
        case JobState::Cancelled:
            out << " in " << Duration(elapsed) << " (HTTP " << outcome.status << ")";
            if (!outcome.error.empty())
                out << ": " << outcome.error;
            if (resultDropped)
                out << "; its result was dropped (the retention budget)";
            if (withResult && !outcome.text.empty())
                out << "\n" << outcome.text;
            break;
    }
    return out.str();
}

struct Registry::Job
{
    uint64_t id = 0;
    Spec spec;
    std::string lane;
    JobState state = JobState::Queued;
    uint64_t done = 0;
    uint64_t total = 0;
    std::string unit;
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
    int64_t createdMs = 0;
    int64_t startedMs = 0;
    int64_t endedMs = 0;
    int64_t lastNotifyMs = 0;  ///< steady clock
    Outcome outcome;
    bool resultDropped = false;
    size_t resultBytes = 0;
    std::map<uint64_t, std::function<void(const Info&)>> watchers;
};

Registry& Registry::Instance()
{
    static Registry registry;
    static const bool marked = [] {
        registry._global = true;
        registryAlive.store(true, std::memory_order_release);
        return true;
    }();
    (void)marked;
    return registry;
}

bool Registry::Alive()
{
    return registryAlive.load(std::memory_order_acquire);
}

Registry::Registry()
{
    _notifier = [](const std::string& emulatorId) { NotifyReadoutsChanged(emulatorId, "jobs"); };
}

Registry::~Registry()
{
    if (_global)
        registryAlive.store(false, std::memory_order_release);
    std::vector<std::shared_ptr<Job>> queued;
    std::vector<std::function<void()>> stops;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        if (_global)
            _notifier = nullptr;  // the process exits: the message center may be gone
        queued.assign(_queue.begin(), _queue.end());
        _queue.clear();
        for (auto& [id, job] : _jobs)
        {
            (void)id;
            if (job->state == JobState::Running)
            {
                job->cancel->store(true);
                if (job->spec.onCancel)
                    stops.push_back(job->spec.onCancel);
            }
        }
    }
    for (const auto& stop : stops)
        stop();
    for (const auto& job : queued)
    {
        Outcome outcome;
        outcome.ok = false;
        outcome.cancelled = true;
        outcome.status = 503;
        outcome.error = "the job registry stopped before the job ran";
        Finish(job, std::move(outcome), false);
    }
    _wake.notify_all();
    for (std::thread& worker : _workers)
        if (worker.joinable())
            worker.join();
}

void Registry::SetLimits(const Limits& limits)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _limits = limits;
    if (_limits.workers == 0)
        _limits.workers = 1;
}

Limits Registry::GetLimits() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _limits;
}

void Registry::SetNotifier(std::function<void(const std::string&)> notifier)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _notifier = std::move(notifier);
}

void Registry::Notify(const std::string& emulatorId)
{
    if (emulatorId.empty())
        return;
    std::function<void(const std::string&)> notifier;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        notifier = _notifier;
    }
    if (notifier)
        notifier(emulatorId);
}

void Registry::EnsureWorkersLocked()
{
    // One more worker while none waits and the limit allows it: a lane that could run does not wait for a busy one
    if (_idle == 0 && _workers.size() < _limits.workers)
        _workers.emplace_back([this] { WorkerLoop(); });
}

uint64_t Registry::Submit(Spec spec, std::string& error)
{
    if (!spec.work)
    {
        error = "a job needs its work";
        return 0;
    }
    auto job = std::make_shared<Job>();
    uint64_t id = 0;
    std::string emulatorId = spec.emulatorId;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_stopping)
        {
            error = "the job registry is stopping";
            return 0;
        }
        if (_queue.size() >= _limits.maxQueued)
        {
            error = "too many jobs waiting (" + std::to_string(_queue.size()) + ", at most " +
                    std::to_string(_limits.maxQueued) + "): wait for some to end or cancel them";
            return 0;
        }
        id = _nextId++;
        job->id = id;
        job->lane = spec.lane.empty() ? spec.kind + "@" + spec.emulatorId : spec.lane;
        job->createdMs = WallMs();
        job->spec = std::move(spec);
        _jobs[id] = job;
        _queue.push_back(job);
        EnsureWorkersLocked();
    }
    _wake.notify_all();
    Notify(emulatorId);
    return id;
}

std::shared_ptr<Registry::Job> Registry::NextRunnableLocked()
{
    for (auto it = _queue.begin(); it != _queue.end(); ++it)
    {
        // The first queued job of a free lane: a lane's jobs leave the queue in their order
        if (_busyLanes.count((*it)->lane) == 0)
        {
            std::shared_ptr<Job> job = *it;
            _queue.erase(it);
            return job;
        }
    }
    return nullptr;
}

void Registry::WorkerLoop()
{
    for (;;)
    {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ++_idle;
            while (!_stopping && !(job = NextRunnableLocked()))
                _wake.wait(lock);
            --_idle;
            if (!job)
                return;  // stopping
            job->state = JobState::Running;
            job->startedMs = WallMs();
            job->lastNotifyMs = SteadyMs();
            _busyLanes.insert(job->lane);
        }
        Notify(job->spec.emulatorId);

        Context context(*this, job->id, job->cancel);
        tlsCurrent = &context;
        Outcome outcome;
        try
        {
            outcome = job->spec.work(context);
        }
        catch (const std::exception& e)
        {
            outcome = Outcome{};
            outcome.ok = false;
            outcome.status = 500;
            outcome.error = std::string("the job threw: ") + e.what();
        }
        catch (...)
        {
            outcome = Outcome{};
            outcome.ok = false;
            outcome.status = 500;
            outcome.error = "the job threw";
        }
        tlsCurrent = nullptr;
        Finish(job, std::move(outcome), true);
    }
}

void Registry::Finish(const std::shared_ptr<Job>& job, Outcome outcome, bool ran)
{
    std::map<uint64_t, std::function<void(const Info&)>> watchers;
    std::vector<std::shared_ptr<Job>> dropped;
    std::function<Outcome(Context&)> work;
    std::function<void()> onSkipped;
    Info info;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (outcome.cancelled)
            job->state = JobState::Cancelled;
        else
            job->state = outcome.ok ? JobState::Done : JobState::Failed;
        job->endedMs = WallMs();
        if (job->startedMs == 0)
            job->startedMs = job->endedMs;
        job->resultBytes = ApproxBytes(outcome.result) + outcome.text.size() + outcome.error.size();
        job->outcome = std::move(outcome);
        if (ran)
            _busyLanes.erase(job->lane);
        watchers.swap(job->watchers);
        _watches -= std::min(_watches, watchers.size());
        // The closures go: what they hold (an instance, a bundle) is released outside the lock
        work = std::move(job->spec.work);
        job->spec.work = nullptr;
        job->spec.onCancel = nullptr;
        onSkipped = std::move(job->spec.onSkipped);
        job->spec.onSkipped = nullptr;
        info = InfoLocked(*job);
        PruneLocked(dropped);
    }
    _ended.notify_all();
    _wake.notify_all();
    if (!ran && onSkipped)
        onSkipped();
    for (auto& [token, notify] : watchers)
    {
        (void)token;
        if (notify)
            notify(info);
    }
    Notify(info.emulatorId);
}

void Registry::PruneLocked(std::vector<std::shared_ptr<Job>>& dropped)
{
    std::vector<std::shared_ptr<Job>> finished;
    for (const auto& [id, job] : _jobs)
    {
        (void)id;
        if (IsFinal(job->state))
            finished.push_back(job);
    }
    std::sort(finished.begin(), finished.end(), [](const auto& a, const auto& b) {
        return a->endedMs != b->endedMs ? a->endedMs < b->endedMs : a->id < b->id;
    });
    const int64_t now = WallMs();
    size_t drop = 0;
    while (drop < finished.size() &&
           (finished.size() - drop > _limits.keepFinished ||
            now - finished[drop]->endedMs > static_cast<int64_t>(_limits.keepMs)))
        ++drop;
    for (size_t i = 0; i < drop; ++i)
    {
        dropped.push_back(finished[i]);
        _jobs.erase(finished[i]->id);
    }
    // The results' budget: the oldest lose their payload first
    size_t bytes = 0;
    for (size_t i = drop; i < finished.size(); ++i)
        bytes += finished[i]->resultBytes;
    for (size_t i = drop; i < finished.size() && bytes > _limits.keepResultBytes; ++i)
    {
        Job& job = *finished[i];
        if (job.resultDropped)
            continue;
        bytes -= std::min(bytes, job.resultBytes);
        job.outcome.result = StateNode();
        if (job.outcome.text.size() > kKeptTextChars)
            job.outcome.text = job.outcome.text.substr(0, kKeptTextChars) + "...";
        job.resultBytes = job.outcome.text.size() + job.outcome.error.size();
        bytes += job.resultBytes;
        job.resultDropped = true;
    }
}

Info Registry::InfoLocked(const Job& job) const
{
    Info info;
    info.id = job.id;
    info.kind = job.spec.kind;
    info.emulatorId = job.spec.emulatorId;
    info.summary = job.spec.summary;
    info.state = job.state;
    info.done = job.done;
    info.total = job.total;
    info.unit = job.unit;
    info.cancellable = job.spec.cancellable;
    info.cancelRequested = job.cancel->load();
    info.createdMs = job.createdMs;
    info.startedMs = job.state == JobState::Queued ? 0 : job.startedMs;
    info.endedMs = job.endedMs;
    info.resultDropped = job.resultDropped;
    if (job.state == JobState::Queued)
    {
        for (const auto& queued : _queue)
        {
            if (queued.get() == &job)
                break;
            if (queued->lane == job.lane)
                ++info.queuePosition;
        }
    }
    if (IsFinal(job.state))
        info.outcome = job.outcome;
    return info;
}

std::optional<Info> Registry::Get(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _jobs.find(id);
    if (it == _jobs.end())
        return std::nullopt;
    return InfoLocked(*it->second);
}

std::vector<Info> Registry::List(const std::string& emulatorId, bool activeOnly) const
{
    std::vector<Info> list;
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& [id, job] : _jobs)
    {
        (void)id;
        if (!emulatorId.empty() && job->spec.emulatorId != emulatorId)
            continue;
        if (activeOnly && IsFinal(job->state))
            continue;
        list.push_back(InfoLocked(*job));
    }
    return list;
}

Registry::CancelResult Registry::Cancel(uint64_t id)
{
    std::shared_ptr<Job> job;
    std::function<void()> stop;
    std::string emulatorId;
    bool running = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _jobs.find(id);
        if (it == _jobs.end())
            return CancelResult::NotFound;
        job = it->second;
        if (IsFinal(job->state))
            return CancelResult::Finished;
        if (job->state == JobState::Running)
        {
            if (!job->spec.cancellable)
                return CancelResult::NotCancellable;
            job->cancel->store(true);
            stop = job->spec.onCancel;
            emulatorId = job->spec.emulatorId;
            running = true;
        }
        else if (!TakeQueuedLocked(job))
        {
            return CancelResult::Finished;  // another cancel took it and is ending it
        }
    }
    if (running)
    {
        if (stop)
            stop();
        Notify(emulatorId);
        return CancelResult::Requested;
    }
    FinishCancelledQueued(job);
    return CancelResult::Cancelled;
}

bool Registry::TakeQueuedLocked(const std::shared_ptr<Job>& job)
{
    const auto it = std::find(_queue.begin(), _queue.end(), job);
    if (it == _queue.end())
        return false;
    _queue.erase(it);
    job->cancel->store(true);
    return true;
}

void Registry::FinishCancelledQueued(const std::shared_ptr<Job>& job)
{
    Outcome outcome;
    outcome.ok = false;
    outcome.cancelled = true;
    outcome.status = 409;
    outcome.error = "cancelled before it ran";
    Finish(job, std::move(outcome), false);
}

void Registry::CancelInstance(const std::string& emulatorId)
{
    std::vector<std::shared_ptr<Job>> queued;
    std::vector<uint64_t> running;
    {
        // The queued ones leave the queue in one step, before any running one is asked to stop: a lane freed by that
        // stop must find nothing of this instance to start
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [id, job] : _jobs)
        {
            if (job->spec.emulatorId != emulatorId || IsFinal(job->state))
                continue;
            if (job->state == JobState::Running)
                running.push_back(id);
            else if (TakeQueuedLocked(job))
                queued.push_back(job);
        }
    }
    for (const auto& job : queued)
        FinishCancelledQueued(job);
    for (uint64_t id : running)
        (void)Cancel(id);
}

std::optional<Info> Registry::Wait(uint64_t id, std::chrono::milliseconds timeout) const
{
    std::unique_lock<std::mutex> lock(_mutex);
    const auto it = _jobs.find(id);
    if (it == _jobs.end())
        return std::nullopt;
    const std::shared_ptr<Job> job = it->second;
    _ended.wait_for(lock, timeout, [&job, this] { return IsFinal(job->state) || _stopping; });
    return InfoLocked(*job);
}

Registry::WatchResult Registry::Watch(uint64_t id, std::function<void(const Info&)> notify, uint64_t& token)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _jobs.find(id);
    if (it == _jobs.end())
        return WatchResult::NotFound;
    if (IsFinal(it->second->state))
        return WatchResult::Ended;
    if (_watches >= _limits.maxWatches)
        return WatchResult::Busy;
    token = _nextToken++;
    it->second->watchers[token] = std::move(notify);
    ++_watches;
    return WatchResult::Watching;
}

bool Registry::Unwatch(uint64_t id, uint64_t token)
{
    std::function<void(const Info&)> released;  // destroyed outside the lock
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _jobs.find(id);
    if (it == _jobs.end())
        return false;
    auto& watchers = it->second->watchers;
    const auto w = watchers.find(token);
    if (w == watchers.end())
        return false;
    released = std::move(w->second);
    watchers.erase(w);
    if (_watches > 0)
        --_watches;
    return true;
}

size_t Registry::WatchCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _watches;
}

void Registry::ReportProgress(uint64_t id, uint64_t done, uint64_t total, const char* unit)
{
    std::string emulatorId;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _jobs.find(id);
        if (it == _jobs.end())
            return;
        Job& job = *it->second;
        job.done = done;
        job.total = total;
        if (unit != nullptr)
            job.unit = unit;
        const int64_t now = SteadyMs();
        if (now - job.lastNotifyMs < kProgressNotifyMs)
            return;
        job.lastNotifyMs = now;
        emulatorId = job.spec.emulatorId;
    }
    Notify(emulatorId);
}

}  // namespace jobs
