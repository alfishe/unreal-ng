#include "executionarbiter.h"

#include <algorithm>

namespace execution
{

namespace detail
{
struct Request
{
    uint64_t seq = 0;
    bool command = false;
    bool posted = false;
    Requester requester;
    std::string tag;
    Clock::time_point submitted = Clock::now();

    // Intent
    Intent intent = Intent::Pause;
    IntentOptions intentOptions;

    // Command
    CommandBody body;
    CommandOptions commandOptions;
    std::atomic<bool> aborted{false};

    // Completion
    Outcome outcome = Outcome::Pending;
    Intent finalIntent = Intent::Pause;
    std::string detail;
};
}  // namespace detail

using detail::Request;

namespace
{
double AgeMs(Clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}
}  // namespace

const char* IntentName(Intent intent)
{
    return intent == Intent::Run ? "run" : "pause";
}

const char* OriginName(Origin origin)
{
    switch (origin)
    {
        case Origin::Gui: return "gui";
        case Origin::WebApi: return "webapi";
        case Origin::Cli: return "cli";
        case Origin::Mcp: return "mcp";
        case Origin::Lua: return "lua";
        case Origin::Python: return "python";
        case Origin::Gdb: return "gdb";
        case Origin::DeZog: return "dezog";
        case Origin::Companion: return "companion";
        case Origin::HostDevice: return "host-device";
        case Origin::Debugger: return "debugger";
        case Origin::Ttd: return "ttd";
        case Origin::Rzx: return "rzx";
        case Origin::Lifecycle: return "lifecycle";
        case Origin::Internal: return "internal";
    }
    return "?";
}

Origin OriginFromName(const std::string& name)
{
    static const std::pair<const char*, Origin> names[] = {
        {"gui", Origin::Gui},       {"qt", Origin::Gui},         {"qt-timeline", Origin::Gui},
        {"webapi", Origin::WebApi}, {"cli", Origin::Cli},        {"mcp", Origin::Mcp},
        {"lua", Origin::Lua},       {"python", Origin::Python}, {"gdb", Origin::Gdb},
        {"dezog", Origin::DeZog},   {"debugger", Origin::Debugger}, {"ttd", Origin::Ttd},
        {"rzx", Origin::Rzx},       {"companion", Origin::Companion}, {"host-device", Origin::HostDevice}};
    for (const auto& [text, origin] : names)
        if (name == text)
            return origin;
    return Origin::Internal;
}

const char* OutcomeName(Outcome outcome)
{
    switch (outcome)
    {
        case Outcome::Pending: return "pending";
        case Outcome::Applied: return "applied";
        case Outcome::Coalesced: return "coalesced";
        case Outcome::Refused: return "refused";
        case Outcome::Busy: return "busy";
        case Outcome::Aborted: return "aborted";
    }
    return "?";
}

const char* PhaseName(Phase phase)
{
    switch (phase)
    {
        case Phase::NotStarted: return "not_started";
        case Phase::Running: return "running";
        case Phase::Stopping: return "stopping";
        case Phase::Stopped: return "stopped";
    }
    return "?";
}

/// region <Ticket>

bool Ticket::Done() const
{
    return Peek().outcome != Outcome::Pending;
}

Result Ticket::Wait(Clock::time_point deadline)
{
    if (!_request || !_arbiter)
        return Result{Outcome::Refused, 0, Intent::Pause, "invalid ticket"};
    return _arbiter->WaitTicket(_request, deadline);
}

Result Ticket::Peek() const
{
    if (!_request || !_arbiter)
        return Result{Outcome::Refused, 0, Intent::Pause, "invalid ticket"};
    std::lock_guard<std::mutex> lock(_arbiter->_mutex);
    return _arbiter->ResultOf(*_request);
}

/// endregion </Ticket>

ExecutionArbiter::ExecutionArbiter() = default;

ExecutionArbiter::~ExecutionArbiter() = default;

/// region <Configuration>

void ExecutionArbiter::SetThrottle(std::chrono::milliseconds window)
{
    {
        Lock lock(_mutex);
        _throttle = std::max(window, std::chrono::milliseconds(0));
        PumpLocked(lock);
    }
    Released();
}

void ExecutionArbiter::SetEdgeListener(std::function<void(const IntentEdge&)> listener)
{
    std::lock_guard<std::mutex> deliver(_deliverMutex);
    _edgeListener = std::move(listener);
}

void ExecutionArbiter::SetEdgeGate(std::function<void(const IntentEdge&, bool parked)> gate)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _edgeGate = std::move(gate);
}

void ExecutionArbiter::SetDecisionLog(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _logEnabled = enabled;
    if (!enabled)
        _log.clear();
}

std::vector<Decision> ExecutionArbiter::TakeDecisions()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Decision> out;
    out.swap(_log);
    return out;
}

void ExecutionArbiter::SetInterleaveHook(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _interleaveHook = std::move(hook);
}

/// endregion </Configuration>

/// region <Requests>

std::shared_ptr<Request> ExecutionArbiter::NewRequestLocked(bool command, const Requester& requester, std::string tag)
{
    auto request = std::make_shared<Request>();
    request->seq = _nextSeq++;
    request->command = command;
    request->requester = requester;
    request->tag = std::move(tag);
    request->submitted = Clock::now();
    return request;
}

const char* ExecutionArbiter::LifecycleRefusalLocked(const detail::Request& request) const
{
    if (_phase == Phase::Stopping)
        return "stopping";
    // A retired machine (removed, kept alive by a holder) never executes again: no intent, no command that drives
    // the Z80. Commands that do not drive it (a reader, a save) still run, on the stopped machine
    if (_retired && (!request.command || request.commandOptions.drivesZ80))
        return "retired";
    return nullptr;
}

void ExecutionArbiter::Retire()
{
    {
        Lock lock(_mutex);
        _retired = true;
    }
    Released();
}

Ticket ExecutionArbiter::SubmitIntent(Intent intent, const Requester& requester, IntentOptions options)
{
    std::shared_ptr<Request> request;
    {
        Lock lock(_mutex);
        request = NewRequestLocked(false, requester, options.reason.empty() ? IntentName(intent) : options.reason);
        request->intent = intent;
        request->intentOptions = std::move(options);
        if (const char* refusal = LifecycleRefusalLocked(*request))
        {
            CompleteLocked(request, Outcome::Refused, refusal);
            LogLocked(Decision::Kind::IntentRefused, request.get(), Outcome::Refused, _intent, false);
        }
        else
        {
            _queue.push_back(request);
            PumpLocked(lock);
        }
    }
    Released();
    return Ticket(this, request);
}

Result ExecutionArbiter::Execute(const Requester& requester, std::string tag, const CommandBody& body,
                                 CommandOptions options, Clock::time_point deadline)
{
    std::shared_ptr<Request> request;
    {
        Lock lock(_mutex);
        request = NewRequestLocked(true, requester, std::move(tag));
        request->body = body;
        request->commandOptions = std::move(options);
        if (const char* refusal = LifecycleRefusalLocked(*request))
        {
            CompleteLocked(request, Outcome::Refused, refusal);
            return ResultOf(*request);
        }
        if (RefusedByClaimLocked(*request))
        {
            CompleteLocked(request, Outcome::Refused, "run-control claim held by " +
                                                          std::string(OriginName(_claim->origin)) + " " + _claim->client);
            return ResultOf(*request);
        }
        const std::thread::id self = std::this_thread::get_id();
        const bool holds = (_serving && _servingThread == self) ||
                           std::find(_inlineThreads.begin(), _inlineThreads.end(), self) != _inlineThreads.end() ||
                           (_loopAttached && _loopThread == self);
        if (!holds)
        {
            _queue.push_back(request);
            PumpLocked(lock);
            while (_serving != request && request->outcome == Outcome::Pending)
            {
                const Clock::time_point now = Clock::now();
                if (now >= deadline)
                {
                    const auto it = std::find(_queue.begin(), _queue.end(), request);
                    if (it != _queue.end())
                        _queue.erase(it);
                    CompleteLocked(request, Outcome::Busy, HolderLocked());
                    LogLocked(Decision::Kind::Busy, request.get(), Outcome::Busy, _intent, false);
                    RecomputeLocked();
                    PumpLocked(lock);
                    break;
                }
                const Clock::time_point wake = std::min(deadline, NextWakeLocked());
                if (wake == Clock::time_point::max())
                    _cv.wait(lock);
                else
                    _cv.wait_until(lock, wake);
                PumpLocked(lock);
            }
            if (_serving == request && request->outcome == Outcome::Pending)
            {
                RunBodyLocked(lock, request);
                PumpLocked(lock);
            }
            Result result = ResultOf(*request);
            lock.unlock();
            Released();
            return result;
        }
    }
    Released();
    return RunInline(request);
}

Result ExecutionArbiter::RunInline(const std::shared_ptr<Request>& request)
{
    const std::thread::id self = std::this_thread::get_id();
    {
        Lock lock(_mutex);
        _inlineThreads.push_back(self);
        LogLocked(Decision::Kind::CommandStarted, request.get(), Outcome::Pending, _intent, false, true);
        if (request->commandOptions.pauseFirst && _intent == Intent::Run)
        {
            const Intent before = _intent;
            SetIntentLocked(Intent::Pause, request->requester, "step", request->seq);
            LogLocked(Decision::Kind::IntentApplied, request.get(), Outcome::Applied, before, true, true);
        }
        RecomputeLocked();
    }
    Released();
    CommandContext context;
    context._seq = request->seq;
    context._aborted = &request->aborted;
    if (request->body)
        request->body(context);
    {
        Lock lock(_mutex);
        if (request->commandOptions.thenIntent && *request->commandOptions.thenIntent != _intent)
        {
            const Intent before = _intent;
            SetIntentLocked(*request->commandOptions.thenIntent, request->requester, "after " + request->tag,
                            request->seq);
            LogLocked(Decision::Kind::IntentApplied, request.get(), Outcome::Applied, before, true, true);
        }
        const auto it = std::find(_inlineThreads.begin(), _inlineThreads.end(), self);
        if (it != _inlineThreads.end())
            _inlineThreads.erase(it);
        CompleteLocked(request, Outcome::Applied);
        LogLocked(Decision::Kind::CommandFinished, request.get(), Outcome::Applied, _intent, false, true);
        RecomputeLocked();
        PumpLocked(lock);
    }
    Released();
    std::lock_guard<std::mutex> lock(_mutex);
    return ResultOf(*request);
}

Ticket ExecutionArbiter::Post(const Requester& requester, std::string tag, CommandBody body, CommandOptions options)
{
    std::shared_ptr<Request> request;
    {
        Lock lock(_mutex);
        request = NewRequestLocked(true, requester, std::move(tag));
        request->posted = true;
        request->body = std::move(body);
        request->commandOptions = std::move(options);
        if (const char* refusal = LifecycleRefusalLocked(*request))
            CompleteLocked(request, Outcome::Refused, refusal);
        else
        {
            _queue.push_back(request);
            PumpLocked(lock);
        }
    }
    Released();
    return Ticket(this, request);
}

bool HoldToken::Aborted() const
{
    return request && request->aborted.load(std::memory_order_acquire);
}

HoldToken ExecutionArbiter::BeginHold(const Requester& requester, std::string tag, Clock::time_point deadline,
                                      Result* result, const CommandOptions& options)
{
    HoldToken token;
    Result outcome;
    {
        Lock lock(_mutex);
        auto request = NewRequestLocked(true, requester, std::move(tag));
        request->commandOptions = options;
        const std::thread::id self = std::this_thread::get_id();
        if (const char* refusal = LifecycleRefusalLocked(*request))
        {
            CompleteLocked(request, Outcome::Refused, refusal);
            outcome = ResultOf(*request);
        }
        else if ((_serving && _servingThread == self) ||
                 std::find(_inlineThreads.begin(), _inlineThreads.end(), self) != _inlineThreads.end() ||
                 (_loopAttached && _loopThread == self))
        {
            // This thread already holds the CPU: nested
            _inlineThreads.push_back(self);
            LogLocked(Decision::Kind::CommandStarted, request.get(), Outcome::Pending, _intent, false, true);
            token.request = request;
            token.inlined = true;
            token.thread = self;
            outcome = ResultOf(*request);
        }
        else
        {
            _queue.push_back(request);
            PumpLocked(lock);
            while (_serving != request && request->outcome == Outcome::Pending)
            {
                if (Clock::now() >= deadline)
                {
                    const auto it = std::find(_queue.begin(), _queue.end(), request);
                    if (it != _queue.end())
                        _queue.erase(it);
                    CompleteLocked(request, Outcome::Busy, HolderLocked());
                    LogLocked(Decision::Kind::Busy, request.get(), Outcome::Busy, _intent, false);
                    RecomputeLocked();
                    PumpLocked(lock);
                    break;
                }
                const Clock::time_point wake = std::min(deadline, NextWakeLocked());
                if (wake == Clock::time_point::max())
                    _cv.wait(lock);
                else
                    _cv.wait_until(lock, wake);
                PumpLocked(lock);
            }
            if (_serving == request && request->outcome == Outcome::Pending)
            {
                _servingThread = self;
                LogLocked(Decision::Kind::CommandStarted, request.get(), Outcome::Pending, _intent, false);
                if (options.pauseFirst && _intent == Intent::Run)
                {
                    const Intent before = _intent;
                    SetIntentLocked(Intent::Pause, requester, "step", request->seq);
                    LogLocked(Decision::Kind::IntentApplied, request.get(), Outcome::Applied, before, true);
                }
                token.request = request;
                token.thread = self;
            }
            outcome = ResultOf(*request);
        }
    }
    Released();
    if (result)
        *result = outcome;
    return token;
}

size_t ExecutionArbiter::LendCpu(const HoldToken& token)
{
    if (!token.Valid() || token.inlined)
        return 0;
    size_t lent = 0;
    {
        Lock lock(_mutex);
        const std::thread::id self = std::this_thread::get_id();
        if (_serving != token.request || _servingThread != self || _phase != Phase::Running)
            return 0;
        // Lent only to those that have waited a while: a run with holds arriving all the time (a busy debugger, a
        // fuzz) still makes progress in long stretches instead of a frame per thread wake-up; a hold waits at most
        // this long plus a frame, not the whole run
        constexpr std::chrono::milliseconds kLendAfter{20};
        // Not one that changes the intent (thenIntent, pauseFirst): the run's pause stays as it is
        const auto lendable = [this](const std::shared_ptr<Request>& r) {
            return r->command && !r->posted && !r->commandOptions.drivesZ80 && !r->commandOptions.pauseFirst &&
                   !r->commandOptions.thenIntent && r->outcome == Outcome::Pending && !RefusedByClaimLocked(*r);
        };
        const Clock::time_point now = Clock::now();
        if (std::none_of(_queue.begin(), _queue.end(), [&](const std::shared_ptr<Request>& r) {
                return lendable(r) && now - r->submitted >= kLendAfter;
            }))
            return 0;
        for (;;)
        {
            const auto it = std::find_if(_queue.begin(), _queue.end(), lendable);
            if (it == _queue.end())
                break;
            const std::shared_ptr<Request> borrower = *it;
            _queue.erase(it);
            _lender = _serving;
            StartServingLocked(borrower);  // its submitter's thread wakes and runs it (BeginHold / Execute)
            _cv.notify_all();
            while (_serving == borrower)
                _cv.wait(lock);
            // Back to the lender, where it stood: nothing else was served meanwhile (PumpLocked waits for it)
            _serving = _lender;
            _servingThread = self;
            _servingAborted.store(_lender->aborted.load(std::memory_order_acquire), std::memory_order_relaxed);
            _lender.reset();
            ++lent;
            if (_phase != Phase::Running)
                break;
        }
        RecomputeLocked();
    }
    Released();
    return lent;
}

void ExecutionArbiter::EndHold(HoldToken& token)
{
    if (!token.Valid())
        return;
    {
        Lock lock(_mutex);
        const std::shared_ptr<Request> request = token.request;
        if (token.inlined)
        {
            const auto it = std::find(_inlineThreads.begin(), _inlineThreads.end(), token.thread);
            if (it != _inlineThreads.end())
                _inlineThreads.erase(it);
            CompleteLocked(request, Outcome::Applied);
            LogLocked(Decision::Kind::CommandFinished, request.get(), Outcome::Applied, _intent, false, true);
        }
        else if (_serving == request)
        {
            FinishServingLocked(request, request->aborted.load(std::memory_order_acquire) ? Outcome::Aborted
                                                                                          : Outcome::Applied);
        }
        RecomputeLocked();
        PumpLocked(lock);
    }
    token.request.reset();
    Released();
}

/// endregion </Requests>

/// region <Priority lane>

void ExecutionArbiter::PauseFromDriver(const Requester& requester, std::string reason)
{
    {
        Lock lock(_mutex);
        auto request = NewRequestLocked(false, requester, reason);
        request->intent = Intent::Pause;
        // Newer than every queued intent: those complete as replaced by this pause
        for (auto it = _queue.begin(); it != _queue.end();)
        {
            if (!(*it)->command)
            {
                (*it)->finalIntent = Intent::Pause;
                CompleteLocked(*it, Outcome::Coalesced);
                LogLocked(Decision::Kind::IntentCoalesced, it->get(), Outcome::Coalesced, _intent, false, false, true);
                it = _queue.erase(it);
            }
            else
                ++it;
        }
        const Intent before = _intent;
        SetIntentLocked(Intent::Pause, requester, reason, request->seq);
        CompleteLocked(request, Outcome::Applied);
        LogLocked(Decision::Kind::PriorityPause, request.get(), Outcome::Applied, before, before != Intent::Pause, false,
                  true);
        RecomputeLocked();
        PumpLocked(lock);
    }
    Released();
}

bool ExecutionArbiter::AbortCurrent(const Requester& requester, const std::string& tag)
{
    (void)requester;
    (void)tag;
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_serving)
        return false;
    _serving->aborted.store(true, std::memory_order_release);
    _servingAborted.store(true, std::memory_order_relaxed);
    _cv.notify_all();
    return true;
}

bool ExecutionArbiter::AbortDriving()
{
    std::lock_guard<std::mutex> lock(_mutex);
    // A direct run lending the CPU (LendCpu) is still the driving command: it stops when it has the CPU back
    if (_lender && _lender->commandOptions.drivesZ80)
    {
        _lender->aborted.store(true, std::memory_order_release);
        return true;
    }
    if (!_serving || !_serving->commandOptions.drivesZ80)
        return false;
    _serving->aborted.store(true, std::memory_order_release);
    _servingAborted.store(true, std::memory_order_relaxed);
    _cv.notify_all();
    return true;
}

void ExecutionArbiter::BeginStop()
{
    {
        Lock lock(_mutex);
        if (_phase == Phase::Running)
            _phase = Phase::Stopping;
        else if (_phase == Phase::NotStarted)
            _phase = Phase::Stopped;
        LogLocked(Decision::Kind::Stop, nullptr, Outcome::Applied, _intent, false);
        for (const auto& request : _queue)
        {
            CompleteLocked(request, Outcome::Refused, "stopping");
            LogLocked(request->command ? Decision::Kind::Stop : Decision::Kind::IntentRefused, request.get(),
                      Outcome::Refused, _intent, false);
        }
        _queue.clear();
        if (_serving)
        {
            _serving->aborted.store(true, std::memory_order_release);
            _servingAborted.store(true, std::memory_order_relaxed);
        }
        if (_lender)
            _lender->aborted.store(true, std::memory_order_release);
        RecomputeLocked();
        _cv.notify_all();
    }
    Released();
}

/// endregion </Priority lane>

/// region <Run-control claim>

bool ExecutionArbiter::TakeClaim(const Requester& owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_claim && !(*_claim == owner))
        return false;
    _claim = owner;
    return true;
}

void ExecutionArbiter::ReleaseClaim(const Requester& owner)
{
    {
        Lock lock(_mutex);
        if (_claim && *_claim == owner)
            _claim.reset();
        PumpLocked(lock);
    }
    Released();
}

bool ExecutionArbiter::RefusedByClaimLocked(const Request& request) const
{
    if (!_claim || request.requester.origin == Origin::Internal || *_claim == request.requester)
        return false;
    if (request.command)
        return request.commandOptions.drivesZ80;
    return request.intent == Intent::Run;
}

/// endregion </Run-control claim>

/// region <Main loop>

void ExecutionArbiter::ExpectMainLoop()
{
    {
        Lock lock(_mutex);
        _loopAttached = true;
        _loopParked = true;  // nothing executes until the loop's first LoopWait
        _loopThread = std::thread::id();
        _loopThreadAtomic.store(std::thread::id(), std::memory_order_release);
        _phase = Phase::Running;
        RecomputeLocked();
        PumpLocked(lock);
    }
    Released();
}

void ExecutionArbiter::AttachMainLoop()
{
    {
        Lock lock(_mutex);
        if (!_loopAttached)
            _loopParked = true;  // not executing until LoopWait lets it
        _loopAttached = true;
        _loopThread = std::this_thread::get_id();
        _loopThreadAtomic.store(_loopThread, std::memory_order_release);
        _phase = Phase::Running;
        RecomputeLocked();
        PumpLocked(lock);
    }
    Released();
}

void ExecutionArbiter::DetachMainLoop()
{
    {
        Lock lock(_mutex);
        _loopAttached = false;
        _loopParked = false;
        _loopThread = std::thread::id();
        _loopThreadAtomic.store(std::thread::id(), std::memory_order_release);
        _phase = Phase::Stopped;
        // A posted command handed to the loop that never ran it: run (or skip, if aborted) it here
        if (_serving && _serving->posted && _servingThread == std::thread::id())
        {
            auto request = _serving;
            RunBodyLocked(lock, request);
        }
        RecomputeLocked();
        PumpLocked(lock);
        _cv.notify_all();
    }
    Released();
}

bool ExecutionArbiter::IsLoopThread() const
{
    return _loopThreadAtomic.load(std::memory_order_acquire) == std::this_thread::get_id();
}

bool ExecutionArbiter::LoopWait()
{
    bool run = false;
    {
        Lock lock(_mutex);
        for (;;)
        {
            if (_phase == Phase::Stopping || _phase == Phase::Stopped)
            {
                _loopParked = true;
                _cv.notify_all();
                break;
            }
            _loopParked = true;
            PumpLocked(lock);
            if (_serving && _serving->posted && _servingThread == std::thread::id())
            {
                auto request = _serving;
                RunBodyLocked(lock, request);
                continue;
            }
            if (_mayRun)
            {
                _loopParked = false;
                run = true;
                break;
            }
            _cv.notify_all();  // the park is confirmed
            const Clock::time_point wake = NextWakeLocked();
            if (wake == Clock::time_point::max())
                _cv.wait(lock);
            else
                _cv.wait_until(lock, wake);
        }
    }
    Released();
    return run;
}

/// endregion </Main loop>

/// region <Queries>

Intent ExecutionArbiter::CurrentIntent() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _intent;
}

Phase ExecutionArbiter::CurrentPhase() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _phase;
}

bool ExecutionArbiter::IsParked() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const bool driving = _serving && _serving->commandOptions.drivesZ80 && _servingThread != std::thread::id();
    return (!_loopAttached || _loopParked) && !driving;
}

bool ExecutionArbiter::WaitParked(Clock::time_point deadline)
{
    Lock lock(_mutex);
    for (;;)
    {
        const bool driving = _serving && _serving->commandOptions.drivesZ80 && _servingThread != std::thread::id();
        if ((!_loopAttached || _loopParked) && !driving)
            return true;
        if (Clock::now() >= deadline)
            return false;
        _cv.wait_until(lock, deadline);
    }
}

bool ExecutionArbiter::HoldsCpu() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const std::thread::id self = std::this_thread::get_id();
    return (_serving && _servingThread == self) ||
           std::find(_inlineThreads.begin(), _inlineThreads.end(), self) != _inlineThreads.end() ||
           (_loopAttached && _loopThread == self);
}

std::optional<std::string> ExecutionArbiter::ServingCommand() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_serving || _servingThread == std::this_thread::get_id())
        return std::nullopt;
    // A direct run is Internal: its tag says enough ("run_frames")
    if (_serving->requester.origin == Origin::Internal)
        return "'" + _serving->tag + "'";
    return "'" + _serving->tag + "' (" + OriginName(_serving->requester.origin) + ")";
}

Snapshot ExecutionArbiter::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    Snapshot s;
    s.phase = _phase;
    s.intent = _intent;
    s.intentSetBy = _intentBy;
    s.intentReason = _intentReason;
    s.intentSeq = _intentSeq;
    s.intentAgeMs = AgeMs(_intentAt);
    s.loopAttached = _loopAttached;
    s.loopParked = _loopParked;
    s.mayRun = _mayRun;
    auto entry = [](const Request& r) {
        SnapshotEntry e;
        e.seq = r.seq;
        e.command = r.command;
        e.intent = r.intent;
        e.requester = r.requester;
        e.tag = r.tag;
        e.ageMs = AgeMs(r.submitted);
        return e;
    };
    if (_serving)
    {
        s.serving = entry(*_serving);
        s.servingDrivesZ80 = _serving->commandOptions.drivesZ80;
    }
    for (const auto& r : _queue)
        s.queue.push_back(entry(*r));
    s.claim = _claim;
    s.applied = _applied;
    s.coalesced = _coalesced;
    s.refused = _refused;
    s.busy = _busy;
    s.aborted = _aborted;
    s.edges = _edgeCount;
    return s;
}

/// endregion </Queries>

/// region <Internals>

void ExecutionArbiter::PumpLocked(Lock& lock)
{
    for (;;)
    {
        if (_serving || _lender || _queue.empty())
            break;
        const std::shared_ptr<Request> head = _queue.front();
        if (!head->command)
        {
            if (!ApplyIntentRunLocked(Clock::now()))
                break;
            continue;
        }
        if (RefusedByClaimLocked(*head))
        {
            _queue.pop_front();
            CompleteLocked(head, Outcome::Refused,
                           "run-control claim held by " + std::string(OriginName(_claim->origin)) + " " + _claim->client);
            LogLocked(Decision::Kind::CommandFinished, head.get(), Outcome::Refused, _intent, false);
            continue;
        }
        if (!GrantedLocked())
            break;
        _queue.pop_front();
        StartServingLocked(head);
        if (head->posted)
        {
            // The parked loop runs posted work; with no loop, whoever pumps runs it
            if (_loopAttached && std::this_thread::get_id() != _loopThread)
            {
                _cv.notify_all();
                break;
            }
            RunBodyLocked(lock, head);
            continue;
        }
        _cv.notify_all();  // the submitter's thread runs it
        break;
    }
    RecomputeLocked();
}

bool ExecutionArbiter::ApplyIntentRunLocked(Clock::time_point now)
{
    size_t count = 0;
    while (count < _queue.size() && !_queue[count]->command)
        ++count;
    const bool commandFollows = count < _queue.size();
    if (_phase == Phase::Running && _throttle.count() > 0 && !commandFollows &&
        now < _queue.front()->submitted + _throttle)
        return false;

    // Evaluate in order, as if applied one by one; then apply only the last one that is allowed. The decisions are
    // logged in queue order, so the log reads as the sequential evaluation
    Intent current = _intent;
    Requester currentBy = _intentBy;
    std::vector<std::shared_ptr<Request>> batch(_queue.begin(), _queue.begin() + static_cast<std::ptrdiff_t>(count));
    std::vector<std::string> refusals(count);
    std::shared_ptr<Request> winner;
    for (size_t i = 0; i < count; ++i)
    {
        const std::shared_ptr<Request>& r = batch[i];
        if (RefusedByClaimLocked(*r))
            refusals[i] = "run-control claim held by " + std::string(OriginName(_claim->origin)) + " " + _claim->client;
        else if (r->intent == Intent::Run && r->intentOptions.onlyIfOwner && current == Intent::Pause &&
                 !(currentBy == r->requester))
            refusals[i] = std::string("not the owner of the pause (set by ") + OriginName(currentBy.origin) + ")";
        if (!refusals[i].empty())
            continue;
        if (r->intent != current)
        {
            current = r->intent;
            currentBy = r->requester;
        }
        winner = r;
    }
    _queue.erase(_queue.begin(), _queue.begin() + static_cast<std::ptrdiff_t>(count));

    const Intent before = _intent;
    if (winner)
        SetIntentLocked(winner->intent, winner->requester, winner->intentOptions.reason, winner->seq);
    _logBatch = _nextBatch++;
    for (size_t i = 0; i < count; ++i)
    {
        const std::shared_ptr<Request>& r = batch[i];
        if (!refusals[i].empty())
        {
            r->finalIntent = _intent;
            CompleteLocked(r, Outcome::Refused, refusals[i]);
            LogLocked(Decision::Kind::IntentRefused, r.get(), Outcome::Refused, before, false);
        }
        else if (r != winner)
        {
            r->finalIntent = winner->intent;
            CompleteLocked(r, Outcome::Coalesced);
            LogLocked(Decision::Kind::IntentCoalesced, r.get(), Outcome::Coalesced, before, false);
        }
        else
        {
            r->finalIntent = _intent;
            CompleteLocked(r, Outcome::Applied);
            LogLocked(Decision::Kind::IntentApplied, r.get(), Outcome::Applied, before, before != _intent);
        }
    }
    _logBatch = 0;
    return true;
}

bool ExecutionArbiter::GrantedLocked() const
{
    return !_serving && !_lender && (!_loopAttached || _loopParked);
}

void ExecutionArbiter::StartServingLocked(const std::shared_ptr<Request>& request)
{
    _servingAborted.store(request->aborted.load(std::memory_order_acquire), std::memory_order_relaxed);
    _serving = request;
    _servingThread = std::thread::id();
    RecomputeLocked();
}

void ExecutionArbiter::RunBodyLocked(Lock& lock, const std::shared_ptr<Request>& request)
{
    _servingThread = std::this_thread::get_id();
    LogLocked(Decision::Kind::CommandStarted, request.get(), Outcome::Pending, _intent, false);
    if (request->commandOptions.pauseFirst && _intent == Intent::Run)
    {
        const Intent before = _intent;
        SetIntentLocked(Intent::Pause, request->requester, "step", request->seq);
        LogLocked(Decision::Kind::IntentApplied, request.get(), Outcome::Applied, before, true);
    }
    const bool skip = request->aborted.load(std::memory_order_acquire);
    if (!skip && request->body)
    {
        Unlock(lock);
        CommandContext context;
        context._seq = request->seq;
        context._aborted = &request->aborted;
        request->body(context);
        lock.lock();
    }
    const bool aborted = request->aborted.load(std::memory_order_acquire);
    if (!aborted && request->commandOptions.thenIntent && *request->commandOptions.thenIntent != _intent)
    {
        const Intent before = _intent;
        SetIntentLocked(*request->commandOptions.thenIntent, request->requester, "after " + request->tag,
                        request->seq);
        LogLocked(Decision::Kind::IntentApplied, request.get(), Outcome::Applied, before, true);
    }
    FinishServingLocked(request, aborted ? Outcome::Aborted : Outcome::Applied);
}

void ExecutionArbiter::FinishServingLocked(const std::shared_ptr<Request>& request, Outcome outcome)
{
    if (_serving == request)
    {
        _serving.reset();
        _servingThread = std::thread::id();
        _servingAborted.store(false, std::memory_order_relaxed);
    }
    CompleteLocked(request, outcome);
    LogLocked(Decision::Kind::CommandFinished, request.get(), outcome, _intent, false);
    RecomputeLocked();
    _cv.notify_all();
}

void ExecutionArbiter::SetIntentLocked(Intent intent, const Requester& requester, const std::string& reason,
                                       uint64_t seq)
{
    // An intent equal to the one in force keeps its owner: a second pause does not take over the first one
    if (intent == _intent)
        return;
    _intent = intent;
    _intentAtomic.store(intent, std::memory_order_relaxed);
    _intentBy = requester;
    _intentReason = reason;
    _intentSeq = seq;
    _intentAt = Clock::now();
    _edges.push_back(IntentEdge{intent, requester, reason, seq});
    ++_edgeCount;
    if (_edgeGate)
    {
        const bool driving = _serving && _serving->commandOptions.drivesZ80 && _servingThread != std::thread::id();
        _edgeGate(_edges.back(), (!_loopAttached || _loopParked) && !driving);
    }
    RecomputeLocked();
}

void ExecutionArbiter::CompleteLocked(const std::shared_ptr<Request>& request, Outcome outcome, std::string detail)
{
    request->outcome = outcome;
    request->detail = std::move(detail);
    if (!request->command && outcome == Outcome::Applied)
        request->finalIntent = _intent;
    switch (outcome)
    {
        case Outcome::Applied: ++_applied; break;
        case Outcome::Coalesced: ++_coalesced; break;
        case Outcome::Refused: ++_refused; break;
        case Outcome::Busy: ++_busy; break;
        case Outcome::Aborted: ++_aborted; break;
        case Outcome::Pending: break;
    }
    _cv.notify_all();
}

void ExecutionArbiter::RecomputeLocked()
{
    const bool headCommand = !_queue.empty() && _queue.front()->command;
    const bool mayRun = _phase == Phase::Running && _intent == Intent::Run && !_serving && !headCommand;
    if (mayRun != _mayRun)
    {
        _mayRun = mayRun;
        _cv.notify_all();
    }
    // Only an attached loop parks on it: without one (not started, stopped, ZX-Poly slaves stepped by their master)
    // nothing executes on its own, and the per-instruction check stays a single false read
    const bool yield = _loopAttached && !mayRun;
    const bool raised = yield && !_yield.load(std::memory_order_relaxed);
    _yield.store(yield, std::memory_order_relaxed);
    if (raised)
        _loopWake.Notify();
}

std::string ExecutionArbiter::HolderLocked() const
{
    if (_serving)
        return "command #" + std::to_string(_serving->seq) + " '" + _serving->tag + "' (" +
               OriginName(_serving->requester.origin) + ")";
    if (_loopAttached && !_loopParked)
        return "the main loop has not parked";
    if (!_queue.empty())
        return "queued behind #" + std::to_string(_queue.front()->seq) + " '" + _queue.front()->tag + "'";
    return "nothing";
}

void ExecutionArbiter::LogLocked(Decision::Kind kind, const Request* request, Outcome outcome, Intent before,
                                 bool edge, bool inlined, bool priority)
{
    if (!_logEnabled)
        return;
    Decision d;
    d.kind = kind;
    d.seq = request ? request->seq : 0;
    d.outcome = outcome;
    d.intentBefore = before;
    d.intentAfter = _intent;
    d.edge = edge;
    d.inlined = inlined;
    d.priority = priority;
    d.batch = _logBatch;
    if (request)
    {
        d.command = request->command;
        d.requested = request->intent;
        d.onlyIfOwner = request->intentOptions.onlyIfOwner;
        d.requester = request->requester;
        d.tag = request->tag;
        d.detail = request->detail;
    }
    _log.push_back(std::move(d));
}

Clock::time_point ExecutionArbiter::NextWakeLocked() const
{
    // Only a throttle window that can close now is a reason to wake. Behind a served command the head intents wait
    // for its end (which notifies); a past window time there would make every waiter spin on the mutex and starve
    // the thread that runs the command (found by the fuzz, seed 14)
    if (!_serving && _phase == Phase::Running && _throttle.count() > 0 && !_queue.empty() && !_queue.front()->command)
        return _queue.front()->submitted + _throttle;
    return Clock::time_point::max();
}

void ExecutionArbiter::Unlock(Lock& lock)
{
    lock.unlock();
    Released();
}

void ExecutionArbiter::DeliverEdges()
{
    std::lock_guard<std::mutex> deliver(_deliverMutex);
    for (;;)
    {
        IntentEdge edge;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_edges.empty())
                return;
            edge = std::move(_edges.front());
            _edges.pop_front();
        }
        if (_edgeListener)
            _edgeListener(edge);
    }
}

void ExecutionArbiter::Released()
{
    DeliverEdges();
    std::function<void()> hook;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        hook = _interleaveHook;
    }
    if (hook)
        hook();
}

Result ExecutionArbiter::WaitTicket(const std::shared_ptr<Request>& request, Clock::time_point deadline)
{
    Result result;
    {
        Lock lock(_mutex);
        for (;;)
        {
            PumpLocked(lock);
            if (request->outcome != Outcome::Pending)
                break;
            const std::thread::id self = std::this_thread::get_id();
            const bool holds = (_serving && _servingThread == self) ||
                               std::find(_inlineThreads.begin(), _inlineThreads.end(), self) != _inlineThreads.end() ||
                               (_loopAttached && _loopThread == self && !_loopParked);
            if (holds)
            {
                // Queued behind this thread's own work: waiting would never end
                result = ResultOf(*request);
                result.outcome = Outcome::Busy;
                result.detail = "self-wait: queued behind the caller's own command";
                return result;
            }
            if (Clock::now() >= deadline)
            {
                // Not withdrawn: an intent is never lost, a posted command still runs. The caller learns who holds
                result = ResultOf(*request);
                result.detail = HolderLocked();
                lock.unlock();
                Released();
                return result;
            }
            const Clock::time_point wake = std::min(deadline, NextWakeLocked());
            _cv.wait_until(lock, wake);
        }
        result = ResultOf(*request);
    }
    Released();
    return result;
}

Result ExecutionArbiter::ResultOf(const Request& request) const
{
    Result r;
    r.outcome = request.outcome;
    r.seq = request.seq;
    r.intent = request.command ? _intent : request.finalIntent;
    r.detail = request.detail;
    return r;
}

/// endregion </Internals>

}  // namespace execution
