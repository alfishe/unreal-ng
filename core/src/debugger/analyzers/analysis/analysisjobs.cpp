#include "debugger/analyzers/analysis/analysisjobs.h"

#include <chrono>
#include <fstream>

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
    node["path"] = path;
    return node;
}

L1Jobs& L1Jobs::Instance()
{
    static L1Jobs jobs;
    return jobs;
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
        if (slot.job)
        {
            slot.job->cancel = true;
            if (slot.job->thread.joinable())
                slot.job->thread.join();
        }
    }
}

bool L1Jobs::Start(const std::string& emulatorId, TraceBundle bundle, ReplayOptions options, const std::string& path,
                   uint64_t& id, std::string& error)
{
    std::shared_ptr<Job> previous;
    std::shared_ptr<Job> job = std::make_shared<Job>();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Slot& slot = _slots[emulatorId];
        if (slot.job && !slot.job->finished)
        {
            error = "a build is running (job " + std::to_string(slot.job->id) + "): wait for analysis.ready or cancel it";
            return false;
        }
        previous = slot.job;
        job->id = _nextId++;
        job->path = path;
        job->bundle = bundle.ToStateNode();
        job->total = bundle.endFrame > bundle.firstFrame ? bundle.endFrame - bundle.firstFrame : 0;
        if (options.maxFrames != 0 && job->total > options.maxFrames)
            job->total = options.maxFrames;
        slot.job = job;
    }
    if (previous && previous->thread.joinable())
        previous->thread.join();
    id = job->id;
    job->thread = std::thread(&L1Jobs::Run, this, emulatorId, job, std::move(bundle), std::move(options));
    return true;
}

void L1Jobs::Run(const std::string& emulatorId, std::shared_ptr<Job> job, TraceBundle bundle, ReplayOptions options)
{
    Job& j = *job;
    options.progress = [&j](uint64_t done, uint64_t total) {
        j.done = done;
        j.total = total;
        return !j.cancel.load();
    };
    auto result = std::make_shared<L1Result>();
    std::string error;
    const bool ok = ReplayBuild(bundle, options, *result, error);
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
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _slots.find(emulatorId);
    if (it == _slots.end() || !it->second.job || it->second.job->finished)
        return false;
    it->second.job->cancel = true;
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
    if (slot.job)
    {
        slot.job->cancel = true;
        if (slot.job->thread.joinable())
            slot.job->thread.join();
    }
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
