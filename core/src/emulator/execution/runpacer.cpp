#include "emulator/execution/runpacer.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>

#include "3rdparty/message-center/messagecenter.h"
#include "common/uuid.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace execution
{

namespace
{
std::mutex& PacersMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::shared_ptr<RunPacer>>& Pacers()
{
    static std::map<std::string, std::shared_ptr<RunPacer>> pacers;
    return pacers;
}

constexpr size_t kDefaultPreset = 0;  // full speed
constexpr int kDefaultInterval = 1000;

StateNode PresetNode(const PacePreset& preset, size_t index)
{
    StateNode node = StateNode::Object();
    node["id"] = preset.id;
    node["label"] = preset.label;
    node["index"] = static_cast<int64_t>(index);
    node["unit"] = PaceUnitName(preset.unit);
    node["count"] = static_cast<int64_t>(preset.count);
    if (preset.fixedIntervalMs > 0)
        node["interval_ms"] = static_cast<int64_t>(preset.fixedIntervalMs);
    else
        node["interval_ms"] = StateNode();
    node["adjustable"] = preset.fixedIntervalMs == 0;
    return node;
}

StateNode BodyOf(const PaceState& state, bool ladder)
{
    const std::vector<PacePreset>& presets = RunPacer::Presets();
    const PacePreset& preset = presets[std::min(state.preset, presets.size() - 1)];
    StateNode node = StateNode::Object();
    node["preset"] = preset.id;
    node["label"] = preset.label;
    node["index"] = static_cast<int64_t>(state.preset);
    node["unit"] = PaceUnitName(preset.unit);
    node["count"] = static_cast<int64_t>(preset.count);
    node["interval_ms"] = static_cast<int64_t>(state.intervalMs);
    node["effective_ms"] = static_cast<int64_t>(state.effectiveMs);
    node["adjustable"] = preset.fixedIntervalMs == 0;
    node["running"] = state.running;
    node["ticks"] = static_cast<int64_t>(state.ticks);
    node["stop_reason"] = state.stopReason;
    if (state.stopReason == "breakpoint")
    {
        StateNode breakpoint = StateNode::Object();
        breakpoint["id"] = static_cast<int64_t>(state.breakpointId);
        breakpoint["address"] = static_cast<int64_t>(state.breakpointAddress);
        node["breakpoint"] = std::move(breakpoint);
    }
    if (ladder)
    {
        StateNode list = StateNode::Array();
        for (size_t i = 0; i < presets.size(); i++)
            list.push(PresetNode(presets[i], i));
        node["presets"] = std::move(list);
    }
    return node;
}

int EffectiveMs(const PaceState& state)
{
    const PacePreset& preset = RunPacer::Presets()[state.preset];
    if (preset.fixedIntervalMs < 0)
        return 0;
    return preset.fixedIntervalMs > 0 ? preset.fixedIntervalMs : state.intervalMs;
}
}  // namespace

const char* PaceUnitName(PaceUnit unit)
{
    switch (unit)
    {
        case PaceUnit::Full: return "full";
        case PaceUnit::Frames: return "frames";
        case PaceUnit::Scanlines: return "scanlines";
        case PaceUnit::Instructions: return "instructions";
    }
    return "full";
}

const std::vector<PacePreset>& RunPacer::Presets()
{
    // The Qt speed control's slider, coarse to fine (unreal-qt/src/debugger/widgets/speedcontrolwidget.cpp)
    static const std::vector<PacePreset> presets = {
        //  id          label           unit                     n    fixed  min  max   step
        {"full",     "1x (50fps)", PaceUnit::Full,          0, -1,    0,  0,    0},
        {"25fps",    "25 fps",     PaceUnit::Frames,        1, 40,    0,  0,    0},
        {"5fps",     "5 fps",      PaceUnit::Frames,        1, 200,   0,  0,    0},
        {"1fps",     "1 fps",      PaceUnit::Frames,        1, 1000,  0,  0,    0},
        {"frame",    "1 frame",    PaceUnit::Frames,        1, 0,     10, 5000, 10},
        {"100lines", "100 lines",  PaceUnit::Scanlines,     100, 0,   10, 5000, 10},
        {"10lines",  "10 lines",   PaceUnit::Scanlines,     10, 0,    1,  5000, 1},
        {"1line",    "1 line",     PaceUnit::Scanlines,     1, 0,     1,  5000, 1},
        {"1op",      "1 Op",       PaceUnit::Instructions,  1, 0,     1,  5000, 1},
    };
    return presets;
}

bool RunPacer::PresetIndex(const std::string& name, size_t& index)
{
    const std::vector<PacePreset>& presets = Presets();
    for (size_t i = 0; i < presets.size(); i++)
        if (name == presets[i].id || name == presets[i].label)
        {
            index = i;
            return true;
        }
    if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos && name.size() < 4)
    {
        const size_t number = static_cast<size_t>(std::strtoul(name.c_str(), nullptr, 10));
        if (number < presets.size())
        {
            index = number;
            return true;
        }
    }
    return false;
}

std::shared_ptr<RunPacer> RunPacer::For(const std::shared_ptr<Emulator>& emulator)
{
    if (!emulator)
        return nullptr;
    std::lock_guard<std::mutex> lock(PacersMutex());
    std::shared_ptr<RunPacer>& pacer = Pacers()[emulator->GetId()];
    if (!pacer)
        pacer = std::make_shared<RunPacer>(emulator);
    return pacer;
}

std::shared_ptr<RunPacer> RunPacer::Find(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(PacersMutex());
    auto it = Pacers().find(instanceId);
    return it != Pacers().end() ? it->second : nullptr;
}

void RunPacer::Drop(const std::string& instanceId)
{
    std::shared_ptr<RunPacer> pacer;
    {
        std::lock_guard<std::mutex> lock(PacersMutex());
        auto it = Pacers().find(instanceId);
        if (it == Pacers().end())
            return;
        pacer = std::move(it->second);
        Pacers().erase(it);
    }
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(pacer->_mutex);
        pacer->_quit = true;
        if (pacer->_state.running)
            pacer->EndLocked("gone");
        thread = std::move(pacer->_thread);
    }
    pacer->_wake.notify_all();
    if (thread.joinable() && thread.get_id() != std::this_thread::get_id())
        thread.join();
    else if (thread.joinable())
        thread.detach();
}

StateNode RunPacer::DefaultStateBody(bool ladder)
{
    PaceState state;
    state.preset = kDefaultPreset;
    state.intervalMs = kDefaultInterval;
    return BodyOf(state, ladder);
}

RunPacer::RunPacer(std::shared_ptr<Emulator> emulator)
    : _emulator(emulator), _instanceId(emulator ? emulator->GetId() : std::string())
{
    _state.preset = kDefaultPreset;
    _state.intervalMs = kDefaultInterval;
}

RunPacer::~RunPacer()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _quit = true;
        thread = std::move(_thread);
    }
    _wake.notify_all();
    if (thread.joinable() && thread.get_id() != std::this_thread::get_id())
        thread.join();
    else if (thread.joinable())
        thread.detach();
}

PaceState RunPacer::State() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    PaceState state = _state;
    state.effectiveMs = EffectiveMs(_state);
    return state;
}

StateNode RunPacer::StateBody(bool ladder) const
{
    return BodyOf(State(), ladder);
}

int RunPacer::EffectiveMsLocked() const
{
    return EffectiveMs(_state);
}

void RunPacer::Post() const
{
    if (_instanceId.empty())
        return;
    MessageCenter::DefaultMessageCenter().Post(NC_RUN_PACE, new RunPacePayload(unreal::UUID(_instanceId)));
}

void RunPacer::EndLocked(const std::string& reason)
{
    _state.running = false;
    _state.stopReason = reason;
    _generation++;
    _wake.notify_all();
}

RunReply RunPacer::Missing() const
{
    RunReply reply;
    reply.status = RunReply::Status::NotFound;
    reply.error = "Not Found";
    reply.message = "Emulator with specified ID not found";
    return reply;
}

RunReply RunPacer::HeldReply() const
{
    RunReply reply;
    reply.status = RunReply::Status::Held;
    reply.error = "Run-control held";
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    const std::string label =
        emulator && emulator->GetContext() ? emulator->GetContext()->GetRunControlState().surfaceLabel : std::string();
    reply.message = "Run-control held by " + label + ". Use that surface to run the machine.";
    return reply;
}

RunReply RunPacer::Reply(const std::string& message) const
{
    RunReply reply;
    reply.message = message;
    reply.body["status"] = "success";
    reply.body["message"] = message;
    reply.body["pace"] = StateBody();
    if (std::shared_ptr<Emulator> emulator = _emulator.lock())
    {
        reply.body["emulator_id"] = emulator->GetId();
        reply.body["state"] = RunControl::StateName(emulator->GetState());
    }
    return reply;
}

bool RunPacer::PauseFor(const Requester& requester)
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator)
        return false;
    const RunReply paused = RunControl(emulator).Pause(requester, std::chrono::milliseconds(2000));
    return paused.status == RunReply::Status::Ok && emulator->IsPaused();
}

RunReply RunPacer::Set(const Requester& requester, std::optional<size_t> preset, std::optional<int> intervalMs)
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator)
        return Missing();
    if (preset && *preset >= Presets().size())
    {
        RunReply reply;
        reply.status = RunReply::Status::BadRequest;
        reply.error = "Bad Request";
        reply.message = "preset: one of the ladder (0.." + std::to_string(Presets().size() - 1) + ")";
        return reply;
    }
    if (intervalMs && (*intervalMs < kIntervalMin || *intervalMs > kIntervalMax))
    {
        RunReply reply;
        reply.status = RunReply::Status::BadRequest;
        reply.error = "Bad Request";
        reply.message = "interval_ms: " + std::to_string(kIntervalMin) + ".." + std::to_string(kIntervalMax);
        return reply;
    }
    size_t previous = 0;
    size_t now = 0;
    bool running = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        previous = _state.preset;
        running = _state.running;
        if (preset)
            _state.preset = *preset;
        if (intervalMs)
            _state.intervalMs = *intervalMs;
        now = _state.preset;
        _wake.notify_all();  // a sleeping tick takes the new interval
    }
    Post();
    const bool full = Presets()[now].unit == PaceUnit::Full;
    if (now != previous)
    {
        // As the slider: off full speed while the machine runs starts the pace; onto full speed resumes it
        if (running && full)
        {
            Stop();
            if (emulator->IsPaused())
            {
                RunReply resumed = RunControl(emulator).Resume(requester);
                if (!resumed.Ok())
                    return resumed;
            }
            return Reply("Pace " + std::string(Presets()[now].id) + ": the machine runs at its speed");
        }
        if (!running && !full && emulator->IsRunning() && !emulator->IsPaused())
            return Start(requester);
    }
    return Reply("Pace " + std::string(Presets()[now].id));
}

RunReply RunPacer::Shift(const Requester& requester, int delta)
{
    size_t index = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        index = _state.preset;
    }
    const int last = static_cast<int>(Presets().size()) - 1;
    const int target = std::clamp(static_cast<int>(index) + delta, 0, last);
    if (static_cast<size_t>(target) == index)
    {
        RunReply reply = Reply(target == 0 ? "Pace: the coarsest already" : "Pace: the finest already");
        reply.body["edge"] = true;
        return reply;
    }
    return Set(requester, static_cast<size_t>(target), std::nullopt);
}

RunReply RunPacer::Start(const Requester& requester)
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator)
        return Missing();
    if (RunControl::HeldByOther(emulator->GetContext(), requester))
        return HeldReply();
    size_t preset = 0;
    bool running = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        preset = _state.preset;
        running = _state.running;
    }
    if (running)
        return Reply("Pace runs already");
    if (Presets()[preset].unit == PaceUnit::Full)
    {
        if (!emulator->IsPaused())
            return Reply("The machine runs at its speed already");
        RunReply resumed = RunControl(emulator).Resume(requester);
        if (!resumed.Ok())
            return resumed;
        return Reply("The machine runs at its speed");
    }
    if (emulator->IsRunning() && !emulator->IsPaused() && !PauseFor(requester))
    {
        RunReply reply;
        reply.status = RunReply::Status::Failed;
        reply.error = "Conflict";
        reply.message = "The machine did not pause for the pace";
        return reply;
    }
    std::thread finished;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        finished = std::move(_thread);
    }
    if (finished.joinable())
        finished.join();  // a run that ended: its loop exits at once
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_quit)
            return Missing();
        running = _state.running;
        if (!running)
        {
            _requester = requester;
            _state.running = true;
            _state.ticks = 0;
            _state.stopReason.clear();
            _state.breakpointId = 0xFFFF;
            _state.breakpointAddress = 0;
            _generation++;
            _thread = std::thread(&RunPacer::Loop, this);
        }
    }
    if (running)
        return Reply("Pace runs already");
    Post();
    return Reply("Pace started: " + std::string(Presets()[preset].label));
}

RunReply RunPacer::Stop()
{
    bool was = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        was = _state.running;
        if (was)
            EndLocked("stopped");
    }
    if (!was)
        return Reply("Pace does not run");
    Post();
    return Reply("Pace stopped");
}

RunReply RunPacer::Toggle(const Requester& requester)
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator)
        return Missing();
    bool running = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        running = _state.running;
    }
    if (running)
        return Stop();
    if (emulator->IsRunning() && !emulator->IsPaused())
    {
        RunReply paused = RunControl(emulator).Pause(requester);
        if (!paused.Ok())
            return paused;
        RunReply reply = Reply("Paused");
        reply.status = paused.status;
        return reply;
    }
    return Start(requester);
}

RunReply RunPacer::Step(const Requester& requester)
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator)
        return Missing();
    if (RunControl::HeldByOther(emulator->GetContext(), requester))
        return HeldReply();
    size_t preset = 0;
    std::thread finished;
    bool stopped = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        preset = _state.preset;
        if (_state.running)
        {
            EndLocked("stopped");
            stopped = true;
        }
        if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id())
            finished = std::move(_thread);
    }
    if (stopped)
        Post();
    if (finished.joinable())
        finished.join();  // the last tick ends before this one runs
    if (Presets()[preset].unit == PaceUnit::Full)
    {
        RunReply reply;
        reply.status = RunReply::Status::BadRequest;
        reply.error = "Bad Request";
        reply.message = "No step at full speed: choose a paced preset (25fps .. 1op) first";
        return reply;
    }
    const PacePreset& chosen = Presets()[preset];
    switch (chosen.unit)
    {
        case PaceUnit::Frames: emulator->RunFrame(false); break;
        case PaceUnit::Scanlines: emulator->RunNScanlines(chosen.count, false); break;
        case PaceUnit::Instructions: emulator->RunNCPUCycles(chosen.count, false); break;
        case PaceUnit::Full: break;
    }
    if (emulator->LastDirectRunAborted())
    {
        RunReply reply;
        reply.status = RunReply::Status::Revoked;
        reply.error = "revoked_by_user";
        reply.message = "The user took control back; this session has ended. Do not retry.";
        reply.body["retry"] = false;
        return reply;
    }
    RunReply reply = Reply("Stepped " + std::string(chosen.label));
    const Emulator::BreakpointStop& stop = emulator->LastDirectStop();
    StateNode stopNode = StateNode::Object();
    stopNode["cpu"] = DebugCpuName(stop.cpu);
    stopNode["reason"] = stop.hit ? "breakpoint" : "step";
    if (stop.hit)
    {
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
        if (stop.cpu != DebugCpuId::Main)
            stopNode["cpu_pc"] = static_cast<int>(stop.cpuPc);
    }
    reply.body["stop"] = std::move(stopNode);
    if (Z80State* z80 = emulator->GetZ80State())
    {
        reply.body["pc"] = static_cast<int>(z80->pc);
        reply.body["sp"] = static_cast<int>(z80->sp);
    }
    return reply;
}

bool RunPacer::Tick()
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    size_t preset = 0;
    Requester requester;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        preset = _state.preset;
        requester = _requester;
    }
    auto end = [&](const std::string& reason, const Emulator::BreakpointStop* stop = nullptr) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (stop != nullptr)
            {
                _state.breakpointId = stop->breakpointId;
                _state.breakpointAddress = stop->address;
            }
            if (_state.running)
                EndLocked(reason);
        }
        Post();
        return false;
    };
    if (!emulator || emulator->IsDestroying())
        return end("gone");
    const PacePreset& chosen = Presets()[preset];
    if (chosen.unit == PaceUnit::Full)
        return end("full");
    if (RunControl::HeldByOther(emulator->GetContext(), requester))
        return end("held");
    // Somebody continued the machine (the toolbar, a client): the pace gives way
    if (emulator->IsRunning() && !emulator->IsPaused())
        return end("resumed");
    switch (chosen.unit)
    {
        case PaceUnit::Frames: emulator->RunFrame(false); break;
        case PaceUnit::Scanlines: emulator->RunNScanlines(chosen.count, false); break;
        case PaceUnit::Instructions: emulator->RunNCPUCycles(chosen.count, false); break;
        case PaceUnit::Full: break;
    }
    if (emulator->LastDirectRunAborted())
        return end("revoked");
    const Emulator::BreakpointStop stop = emulator->LastDirectStop();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _state.ticks++;
    }
    if (stop.hit)
        return end("breakpoint", &stop);
    return true;
}

void RunPacer::Loop()
{
    std::unique_lock<std::mutex> lock(_mutex);
    const uint64_t generation = _generation;
    auto over = [&] { return _quit || !_state.running || _generation != generation; };
    auto last = std::chrono::steady_clock::now();
    bool first = true;
    while (!over())
    {
        if (!first)
        {
            // The deadline from the last tick with the interval in force now: a new interval applies at once
            const auto deadline = last + std::chrono::milliseconds(EffectiveMsLocked());
            if (std::chrono::steady_clock::now() < deadline)
            {
                const int interval = EffectiveMsLocked();
                _wake.wait_until(lock, deadline, [&] { return over() || EffectiveMsLocked() != interval; });
                continue;
            }
        }
        first = false;
        last = std::chrono::steady_clock::now();
        lock.unlock();
        const bool go = Tick();
        lock.lock();
        if (!go)
            break;
    }
}

}  // namespace execution
