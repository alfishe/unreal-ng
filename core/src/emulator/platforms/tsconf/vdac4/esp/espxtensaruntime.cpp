#include "espxtensaruntime.h"

#include "espapphost.h"
#include "espxtensacpu.h"

#include <algorithm>
#include <chrono>

namespace vdac4
{

namespace
{
thread_local bool t_inJob = false;

uint64_t SteadyNs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
}  // namespace

EspXtensaRuntime::EspXtensaRuntime(std::function<void(const int16_t*, size_t, uint32_t)> audio,
                                   std::function<void(const std::string&)> log, uint32_t mhz)
    : _log(std::move(log)), _audio(std::move(audio))
{
    EspAppHostPorts ports;
    ports.reply = [this](uint8_t command, const uint8_t* data, size_t size) {
        if (_deferring)
            return static_cast<int>(kZmError);  // a job never runs inside a command
        return _appHost ? _appHost->Reply(command, data, size) : static_cast<int>(kZmError);
    };
    ports.event = [this](uint8_t code, const uint8_t* data, size_t size) {
        if (!_deferring)
            return _appHost ? _appHost->SendEvent(code, data, size) : static_cast<int>(kZmError);
        if (_appHost == nullptr)
            return static_cast<int>(kZmError);
        if (size > kMaxEventPayload || (size != 0 && data == nullptr) || _eventRoom == 0)
            return static_cast<int>(kZmBusy);
        --_eventRoom;
        ++_currentEvents;
        Defer([this, code, payload = std::vector<uint8_t>(data, data + size)]() {
            if (_appHost)
                _appHost->SendEvent(code, payload.data(), payload.size());
        });
        return static_cast<int>(kZmOk);
    };
    ports.declared = [this](const std::string& name, const std::string& version, uint8_t first, uint8_t last) {
        if (_info == nullptr)
            return;
        _info->name = name;
        _info->version = version;
        _info->commandFirst = first;
        _info->commandLast = last;
    };
    ports.mediaRange = [this](uint8_t first, uint8_t last) {
        if (!_deferring)
            return _appHost ? _appHost->SetMediaRange(first, last) : static_cast<int>(kZmError);
        Defer([this, first, last]() {
            if (_appHost)
                _appHost->SetMediaRange(first, last);
        });
        return static_cast<int>(kZmOk);
    };
    ports.appDone = [this]() {
        if (_appHost && !_deferring)
            _appHost->AppDone();
    };
    ports.ftOpened = [this]() {
        if (!_deferring)
        {
            if (_appHost)
                _appHost->FtOpened(true);
            return;
        }
        Defer([this]() {
            if (_appHost)
                _appHost->FtOpened(true);
        });
    };
    ports.linkRead = [this](uint32_t address) {
        uint32_t value = 0;
        IVdac4TangLink* tang = _appHost ? _appHost->Tang() : nullptr;
        if (tang == nullptr || !tang->LinkRead(address, value))
            return 0u;
        return value;
    };
    ports.linkWrite = [this](uint32_t address, uint32_t value) {
        IVdac4TangLink* tang = _appHost ? _appHost->Tang() : nullptr;
        if (tang != nullptr)
            tang->LinkWrite(address, value);
    };
    ports.audio = [this](const int16_t* frames, size_t count, uint32_t rate) {
        if (!_audio)
            return;
        if (!_deferring)
        {
            _audio(frames, count, rate);
            return;
        }
        Defer([this, rate, pcm = std::vector<int16_t>(frames, frames + count * 2)]() {
            _audio(pcm.data(), pcm.size() / 2, rate);
        });
    };
    ports.log = [this](const std::string& line) {
        if (!_log)
            return;
        if (!_deferring)
        {
            _log(line);
            return;
        }
        Defer([this, line]() { _log(line); });
    };
    _host = std::make_unique<EspAppHost>(CreateEspXtensaCpu(), std::move(ports), mhz);
}

EspXtensaRuntime::~EspXtensaRuntime()
{
    StopWorker();
}

// region <Jobs>

bool EspXtensaRuntime::InJob()
{
    return t_inJob;
}

void EspXtensaRuntime::SetJobHooks(EspJobHooks hooks)
{
    Flush();
    _hooks = std::move(hooks);
}

void EspXtensaRuntime::SetThreaded(bool on)
{
    Flush();
    _threaded = on;
    if (!on)
        StopWorker();
}

void EspXtensaRuntime::SetJit(bool on)
{
    _host->SetJit(on);
}

void EspXtensaRuntime::SetLatency(uint32_t passes)
{
    Flush();
    _latency = std::clamp<uint32_t>(passes, 1, kMaxLatency);
}

EspJobStats EspXtensaRuntime::Stats() const
{
    EspJobStats stats;
    stats.jobs = _jobs.load(std::memory_order_relaxed);
    stats.threadedJobs = _threadedJobs.load(std::memory_order_relaxed);
    stats.waits = _waits.load(std::memory_order_relaxed);
    stats.waitNs = _waitNs.load(std::memory_order_relaxed);
    stats.jobNs = _jobNs.load(std::memory_order_relaxed);
    return stats;
}

void EspXtensaRuntime::SetOutstanding(bool on)
{
    if (_outstanding == on)
        return;
    _outstanding = on;
    if (_hooks.outstanding)
        _hooks.outstanding(on);
}

void EspXtensaRuntime::RunJob(uint64_t index)
{
    const Job& job = _jobRing[index % kRing];
    // core->event: the room the firmware's queue had at the post, less what the jobs before it that were not yet
    // handed over then have taken (they all ran before this one, on this thread)
    size_t used = 0;
    for (uint64_t j = job.firstUnreplayed; j < index; ++j)
        used += _eventsUsed[j % kRing];
    _eventRoom = job.firmwareRoom > used ? job.firmwareRoom - used : 0;
    _current = &_outputs[index % kRing];
    _currentEvents = 0;
    t_inJob = true;
    _deferring = true;
    if (_hooks.begin)
        _hooks.begin(job.stamp);
    _host->Advance(job.us);
    if (_hooks.end)
        _hooks.end(job.stamp, index);
    _deferring = false;
    t_inJob = false;
    _eventsUsed[index % kRing] = _currentEvents;
    _current = nullptr;
}

void EspXtensaRuntime::Post(const Job& job)
{
    const uint64_t index = _posted;
    _jobRing[index % kRing] = job;
    _outputs[index % kRing].clear();
    _posted = index + 1;
    SetOutstanding(true);
    _jobs.fetch_add(1, std::memory_order_relaxed);
    if (!_threaded)
    {
        RunJob(index);
        _completed.store(index + 1, std::memory_order_relaxed);
        return;
    }
    _threadedJobs.fetch_add(1, std::memory_order_relaxed);
    if (!_worker.joinable())
    {
        _quit = false;
        _worker = std::thread([this]() { WorkerLoop(); });
    }
    {
        std::lock_guard<std::mutex> lock(_lock);
        _postedShared.store(index + 1, std::memory_order_release);
    }
    _wake.notify_one();
}

void EspXtensaRuntime::WorkerLoop()
{
    // A job comes about every emulated millisecond while the app draws: after one, the worker looks for the next
    // a little while before it sleeps (a wake through the condition variable costs tens of microseconds)
    constexpr uint64_t kSpinNs = 200'000;
    for (;;)
    {
        uint64_t index = _completed.load(std::memory_order_relaxed);
        if (_postedShared.load(std::memory_order_acquire) <= index)
        {
            const uint64_t until = SteadyNs() + kSpinNs;
            while (_postedShared.load(std::memory_order_acquire) <= index && SteadyNs() < until)
                std::this_thread::yield();
            std::unique_lock<std::mutex> lock(_lock);
            _wake.wait(lock, [&]() { return _quit || _postedShared.load(std::memory_order_acquire) > index; });
            if (_quit)
                return;
        }
        const uint64_t started = SteadyNs();
        RunJob(index);
        _jobNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(_lock);
            _completed.store(index + 1, std::memory_order_release);
        }
        _done.notify_all();
    }
}

void EspXtensaRuntime::StopWorker()
{
    if (!_worker.joinable())
        return;
    WaitFor(_posted);
    {
        std::lock_guard<std::mutex> lock(_lock);
        _quit = true;
    }
    _wake.notify_all();
    _worker.join();
}

void EspXtensaRuntime::WaitFor(uint64_t count)
{
    if (_completed.load(std::memory_order_acquire) >= count)
        return;
    const uint64_t started = SteadyNs();
    // Most waits are short (the worker is on the last part of a millisecond): spin first, then sleep
    constexpr uint64_t kSpinNs = 100'000;
    while (_completed.load(std::memory_order_acquire) < count && SteadyNs() - started < kSpinNs)
        std::this_thread::yield();
    if (_completed.load(std::memory_order_acquire) < count)
    {
        std::unique_lock<std::mutex> lock(_lock);
        _done.wait(lock, [&]() { return _completed.load(std::memory_order_acquire) >= count; });
    }
    _waits.fetch_add(1, std::memory_order_relaxed);
    _waitNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
}

void EspXtensaRuntime::ReplayUpTo(uint64_t count)
{
    for (; _replayed < count; ++_replayed)
    {
        std::vector<std::function<void()>> outputs;
        outputs.swap(_outputs[_replayed % kRing]);
        for (auto& output : outputs)
            output();
    }
}

void EspXtensaRuntime::WaitIdle()
{
    WaitFor(_posted);
    SetOutstanding(false);
}

void EspXtensaRuntime::Flush()
{
    WaitIdle();
    ReplayUpTo(_posted);
}

// endregion

int EspXtensaRuntime::StartApp(const std::string& module, const std::vector<uint8_t>& elf, IVdac4AppHost& host,
                               Vdac4AppInfo& info)
{
    Flush();
    _appHost = &host;
    _info = &info;
    // The ESP's clock follows the firmware's: start from its time
    const uint64_t nowUs = static_cast<uint64_t>(host.NowMs()) * 1000;
    if (nowUs > _host->NowUs())
        _host->Advance(nowUs);

    bool answered = false;
    int result = kZmError;
    std::string name = module;
    const size_t dot = name.find('.');
    if (dot != std::string::npos)
        name.resize(dot);
    _host->StartApp(name, elf, [&](int r, const std::string&, const std::string&) {
        answered = true;
        result = r;
    });
    if (!_host->RunUntilLoopIdle(kCallLimitUs) || !answered)
    {
        if (_log)
            _log(module + ": start did not finish in 5 s");
        result = kZmError;
    }
    _info = nullptr;
    if (result != kZmOk)
    {
        _host->StopApp();
        _appHost = nullptr;
    }
    return result;
}

void EspXtensaRuntime::StopApp()
{
    Flush();
    _host->StopApp();
    _appHost = nullptr;
}

int EspXtensaRuntime::Command(uint8_t command, const uint8_t* data, size_t size)
{
    Flush();
    if (!_host->OwnsCommand(command))
        return kZmError;
    _host->Command(command, data, size);
    if (!_host->RunUntilLoopIdle(kCallLimitUs))
    {
        if (_log)
            _log("app command did not finish in 5 s");
        return kZmError;
    }
    return _host->LastCommandResult();
}

void EspXtensaRuntime::Poll(uint32_t nowMs)
{
    const uint64_t nowUs = static_cast<uint64_t>(nowMs) * 1000;
    if (_posted > _replayed)
    {
        // Jobs in flight: hand over the ones older than the latency; the newest of them says whether the app still
        // owns the board (its own end state: the same whichever thread ran it, whenever). No more: all handed over,
        // and the ESP runs directly again
        const uint64_t need = _posted >= _latency ? _posted - _latency + 1 : 0;
        if (need > _replayed)
        {
            WaitFor(need);
            ReplayUpTo(need);
            if (!(_hooks.hostedAfter && _hooks.hostedAfter(_replayed - 1)))
                Flush();
        }
    }
    if (_posted == _replayed)
    {
        WaitIdle();
        if (nowUs <= _host->NowUs())
            return;
        if (!_hooks.hosted || !_hooks.hosted())
        {
            _host->Advance(nowUs);
            return;
        }
    }
    else if (nowUs <= _jobRing[(_posted - 1) % kRing].us)
        return;
    Job job;
    job.us = nowUs;
    job.stamp = _hooks.stamp ? _hooks.stamp() : 0;
    job.firmwareRoom = _appHost ? _appHost->EventQueueRoom() : 0;
    job.firstUnreplayed = _replayed;
    Post(job);
}

}  // namespace vdac4
