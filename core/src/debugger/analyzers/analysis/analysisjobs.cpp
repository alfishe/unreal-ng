#include "debugger/analyzers/analysis/analysisjobs.h"
#include "emulator/readoutschanged.h"

#include <chrono>
#include <fstream>

#include "debugger/analyzers/analysis/detectors/detectors.h"
#include "debugger/analyzers/analysis/l3session.h"
#include "emulator/jobs/jobregistry.h"

namespace analysis
{

StateNode JobView::ProgressNode() const
{
    StateNode node = StateNode::Object();
    node["job"] = id;
    node["state"] = state;
    node["frames_done"] = framesDone;
    node["frames_total"] = framesTotal;
    node["percent"] = static_cast<uint64_t>(percent);
    node["error"] = error;
    return node;
}

StateNode JobView::ReadyNode() const
{
    StateNode node = StateNode::Object();
    node["ready"] = ready;
    node["job"] = readyJob;
    node["routines"] = routines;
    node["blocks"] = blocks;
    node["loops"] = loops;
    node["instructions"] = instructions;
    node["frames"] = frames;
    node["checked"] = static_cast<uint64_t>(checked);
    node["diverged"] = static_cast<uint64_t>(diverged);
    node["regions"] = regions;
    node["conflicts"] = conflicts;
    node["path"] = path;
    return node;
}

L1Jobs& L1Jobs::Instance()
{
    static L1Jobs jobs;
    return jobs;
}

L1Jobs::L1Jobs()
{
    // The registry runs the builds: made first, it is destroyed after this (which waits for its builds)
    (void)jobs::Registry::Instance();
}

void L1Jobs::End(Job& job, const std::string& state, const std::string& error)
{
    {
        std::lock_guard<std::mutex> lock(job.mutex);
        if (job.finished)
            return;
        job.state = state;
        job.error = error;
        job.finished = true;
    }
    job.ended.notify_all();
}

void L1Jobs::WaitEnded(Job& job)
{
    std::unique_lock<std::mutex> lock(job.mutex);
    job.ended.wait(lock, [&job] { return job.finished.load(); });
}

L1Jobs::~L1Jobs()
{
    std::map<std::string, Slot> slots;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        slots.swap(_slots);
    }
    for (auto& [id, slot] : slots)
    {
        (void)id;
        if (slot.job && !slot.job->finished)
        {
            slot.job->cancel = true;
            if (jobs::Registry::Alive())
                (void)jobs::Registry::Instance().Cancel(slot.job->id);
            WaitEnded(*slot.job);
        }
    }
}

bool L1Jobs::Start(const std::string& emulatorId, TraceBundle bundle, ReplayOptions options, const std::string& path,
                   uint64_t& id, std::string& error)
{
    std::shared_ptr<Job> previous;
    std::shared_ptr<Job> job = std::make_shared<Job>();
    uint64_t frames = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Slot& slot = _slots[emulatorId];
        if (slot.job && !slot.job->finished)
        {
            error = "a build is running (job " + std::to_string(slot.job->id) + "): wait for analysis.ready or cancel it";
            return false;
        }
        previous = slot.job;
        job->path = path;
        job->bundle = bundle.ToStateNode();
        frames = bundle.endFrame > bundle.firstFrame ? bundle.endFrame - bundle.firstFrame : 0;
        job->total = frames;
        if (options.maxFrames != 0 && job->total > options.maxFrames)
            job->total = options.maxFrames;
        slot.job = job;  // the slot is taken before the job can start
    }

    // One job of the registry (S2): queued in the instance's analysis lane, run on a registry worker
    auto kept = std::make_shared<const TraceBundle>(std::move(bundle));
    jobs::Spec spec;
    spec.kind = "analysis.build";
    spec.emulatorId = emulatorId;
    spec.lane = "analysis@" + emulatorId;
    spec.summary = "build " + std::to_string(job->total.load()) + " of " + std::to_string(frames) + " frames" +
                   (path.empty() ? std::string() : " path=" + path);
    spec.cancellable = true;
    spec.onCancel = [job] { job->cancel = true; };
    spec.onSkipped = [job, emulatorId] {
        End(*job, "cancelled", "cancelled before it ran");
        NotifyReadoutsChanged(emulatorId, "analysis");
    };
    spec.work = [this, emulatorId, job, kept, options](jobs::Context& context) -> jobs::Outcome {
        job->id = context.Id();
        Run(emulatorId, job, kept, options, context);
        jobs::Outcome outcome;
        const JobView view = View(emulatorId);
        std::lock_guard<std::mutex> lock(job->mutex);
        outcome.ok = job->state == "done";
        outcome.cancelled = job->state == "cancelled";
        outcome.status = outcome.ok ? 200 : 409;
        outcome.error = job->error;
        StateNode result = StateNode::Object();
        result["progress"] = view.ProgressNode();
        result["ready"] = view.ReadyNode();
        outcome.result = std::move(result);
        outcome.text = outcome.ok ? "L1 built: " + std::to_string(view.routines) + " routines, " +
                                        std::to_string(view.blocks) + " blocks, " + std::to_string(view.loops) +
                                        " loops, " + std::to_string(view.instructions) + " instructions, " +
                                        std::to_string(view.regions) + " L2 regions" +
                                        (job->path.empty() ? "" : ", written to " + job->path)
                                  : "L1 build " + job->state + (job->error.empty() ? "" : ": " + job->error);
        return outcome;
    };
    id = jobs::Registry::Instance().Submit(std::move(spec), error);
    if (id == 0)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Slot& slot = _slots[emulatorId];
        if (slot.job == job)
            slot.job = previous;
        error = "the build was not queued: " + error;
        return false;
    }
    job->id = id;
    NotifyReadoutsChanged(emulatorId, "analysis");  // analysis.progress / analysis.ready: on the job, not on frames
    return true;
}

void L1Jobs::Run(const std::string& emulatorId, std::shared_ptr<Job> job, std::shared_ptr<const TraceBundle> kept,
                 ReplayOptions options, jobs::Context& context)
{
    Job& j = *job;
    {
        std::lock_guard<std::mutex> lock(j.mutex);
        j.state = "running";
    }
    NotifyReadoutsChanged(emulatorId, "analysis");
    // A whole percent more: told (the job runs on its thread, beside or without the machine's frames)
    uint64_t percent = 0;
    options.progress = [&j, &percent, &emulatorId, &context](uint64_t done, uint64_t total) {
        j.done = done;
        j.total = total;
        context.Progress(done, total, "frames");
        const uint64_t now = total != 0 ? done * 100 / total : 0;
        if (now != percent)
        {
            percent = now;
            NotifyReadoutsChanged(emulatorId, "analysis");
        }
        return !j.cancel.load() && !context.CancelRequested();
    };
    auto result = std::make_shared<L1Result>();
    std::string error;
    const bool ok = !j.cancel.load() && ReplayBuild(*kept, options, *result, error);
    if (!ok && error.empty())
        error = "cancelled";
    std::shared_ptr<const L2Result> structure;
    uint64_t conflicts = 0;
    if (ok)
    {
        structure = std::make_shared<const L2Result>(RunDetectors(*result));
        conflicts = structure->store.Conflicts().size();
    }
    if (ok && !j.path.empty())
    {
        std::ofstream file(j.path, std::ios::binary | std::ios::trunc);
        if (file)
            file << result->ToJson();
        if (!file)
            error = "the L1 was built but could not be written to " + j.path;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _slots.find(emulatorId);
        if (ok && it != _slots.end() && it->second.job == job)
        {
            it->second.result = result;
            it->second.structure = structure;
            it->second.bundle = kept;
            it->second.regions = structure->store.Size();
            it->second.conflicts = conflicts;
            it->second.resultJob = j.id;
        }
    }
    {
        std::lock_guard<std::mutex> lock(j.mutex);
        j.hostMicros = result->replay.hostMicros;
        if (ok)
        {
            j.state = "done";
            j.done = j.total.load();
            j.error = error;  // a write failure: the result is there all the same
        }
        else
        {
            j.state = error == "cancelled" ? "cancelled" : "failed";
            j.error = error;
        }
        j.finished = true;
    }
    j.ended.notify_all();
    NotifyReadoutsChanged(emulatorId, "analysis");
}

JobView L1Jobs::View(const std::string& emulatorId) const
{
    JobView view;
    std::shared_ptr<Job> job;
    std::shared_ptr<const L1Result> result;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _slots.find(emulatorId);
        if (it == _slots.end())
            return view;
        job = it->second.job;
        result = it->second.result;
        view.regions = it->second.regions;
        view.conflicts = it->second.conflicts;
        view.readyJob = it->second.resultJob;
    }
    if (job)
    {
        view.id = job->id;
        view.framesDone = job->done;
        view.framesTotal = job->total;
        view.percent = view.framesTotal == 0 ? 0 : static_cast<uint32_t>(view.framesDone * 100 / view.framesTotal);
        std::lock_guard<std::mutex> lock(job->mutex);
        view.state = job->state;
        view.error = job->error;
        view.path = job->path;
        view.hostMicros = job->hostMicros;
        view.bundle = job->bundle;
    }
    if (result)
    {
        view.ready = true;
        view.routines = result->routines.size();
        view.blocks = result->blocks.size();
        view.loops = result->loops.size();
        view.instructions = result->replay.instructions;
        view.frames = result->replay.frames;
        view.checked = result->replay.checked;
        view.diverged = result->replay.diverged;
    }
    return view;
}

std::shared_ptr<const L1Result> L1Jobs::Result(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _slots.find(emulatorId);
    return it != _slots.end() ? it->second.result : nullptr;
}

std::shared_ptr<const L2Result> L1Jobs::Structure(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _slots.find(emulatorId);
    return it != _slots.end() ? it->second.structure : nullptr;
}

std::shared_ptr<const TraceBundle> L1Jobs::Bundle(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _slots.find(emulatorId);
    return it != _slots.end() ? it->second.bundle : nullptr;
}

uint64_t L1Jobs::ResultJob(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _slots.find(emulatorId);
    return it != _slots.end() ? it->second.resultJob : 0;
}

bool L1Jobs::Wait(const std::string& emulatorId, int timeoutMs) const
{
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _slots.find(emulatorId);
        if (it == _slots.end() || !it->second.job)
            return true;
        job = it->second.job;
    }
    std::unique_lock<std::mutex> lock(job->mutex);
    return job->ended.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&job] { return job->finished.load(); });
}

bool L1Jobs::Cancel(const std::string& emulatorId)
{
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _slots.find(emulatorId);
        if (it == _slots.end() || !it->second.job || it->second.job->finished)
            return false;
        it->second.job->cancel = true;
        id = it->second.job->id;
    }
    // A queued build ends at once (onSkipped); a running one stops at its next frame
    if (id != 0)
        (void)jobs::Registry::Instance().Cancel(id);
    return true;
}

void L1Jobs::Forget(const std::string& emulatorId)
{
    Slot slot;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _slots.find(emulatorId);
        if (it != _slots.end())
        {
            slot = it->second;
            _slots.erase(it);
        }
        _recordings.erase(emulatorId);
    }
    L3Sessions::Instance().Forget(emulatorId);
    if (slot.job && !slot.job->finished)
    {
        slot.job->cancel = true;
        if (slot.job->id != 0)
            (void)jobs::Registry::Instance().Cancel(slot.job->id);
        WaitEnded(*slot.job);
    }
    NotifyReadoutsChanged(emulatorId, "analysis");
}

void L1Jobs::NoteRecording(const std::string& emulatorId, bool owned)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (owned)
        _recordings.insert(emulatorId);
    else
        _recordings.erase(emulatorId);
}

bool L1Jobs::OwnsRecording(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _recordings.count(emulatorId) != 0;
}

}  // namespace analysis
