#include "emulator/actions/scrubactions.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "emulator/execution/automationgate.h"

namespace actions
{

float ScrubSpeed(float travel)
{
    struct Point
    {
        float travel;
        float speed;
    };
    static constexpr Point kPoints[] = {{0.08f, 1.0f}, {0.30f, 5.0f}, {0.60f, 50.0f}, {0.90f, 500.0f}, {1.0f, 5000.0f}};
    if (travel < kPoints[0].travel)
        return 0.0f;
    for (size_t i = 1; i < std::size(kPoints); i++)
    {
        const Point& from = kPoints[i - 1];
        const Point& to = kPoints[i];
        if (travel <= to.travel)
            return from.speed * std::pow(to.speed / from.speed, (travel - from.travel) / (to.travel - from.travel));
    }
    return kPoints[std::size(kPoints) - 1].speed;
}

ScrubClock::ScrubClock(Jog jog, bool threaded) : _jog(std::move(jog)), _threaded(threaded) {}

ScrubClock::~ScrubClock()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _quit = true;
    }
    _wake.notify_all();
    if (_thread.joinable())
        _thread.join();
}

uint64_t ScrubClock::NowNs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool ScrubClock::Moving(const State& state)
{
    return !state.stopped && (state.back > 0 || state.forward > 0);
}

void ScrubClock::Set(const std::string& instanceId, bool forward, double level, const ActionOrigin& origin)
{
    SetAt(instanceId, forward, level, origin, NowNs());
}

void ScrubClock::SetAt(const std::string& instanceId, bool forward, double level, const ActionOrigin& origin,
                       uint64_t nowNs)
{
    std::thread finished;  // a thread that left its loop: joined outside the lock
    {
        std::lock_guard<std::mutex> lock(_mutex);
        State& state = _states[instanceId];
        const bool was = Moving(state);
        (forward ? state.forward : state.back) = std::max(0.0, std::min(1.0, level));
        state.origin = origin;
        state.stopped = false;
        if (!Moving(state))
        {
            _states.erase(instanceId);
            return;
        }
        if (!was)
        {
            state.owed = 0;
            state.lastNs = nowNs;
        }
        if (!_threaded || _running || _quit)
            return;
        if (_thread.joinable())
            finished = std::move(_thread);
        _running = true;
        _thread = std::thread([this] { Run(); });
    }
    if (finished.joinable())
        finished.join();
}

void ScrubClock::Stop(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _states.find(instanceId);
    if (it != _states.end())
    {
        it->second.stopped = true;
        it->second.owed = 0;
    }
}

void ScrubClock::TickAt(uint64_t nowNs)
{
    struct Move
    {
        std::string instanceId;
        int64_t frames;
        double rate;
        ActionOrigin origin;
    };
    std::vector<Move> moves;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& [instanceId, state] : _states)
        {
            if (!Moving(state))
                continue;
            const double seconds = nowNs > state.lastNs ? static_cast<double>(nowNs - state.lastNs) / 1e9 : 0.0;
            state.lastNs = nowNs;
            state.owed += (ScrubSpeed(static_cast<float>(state.forward)) - ScrubSpeed(static_cast<float>(state.back))) *
                          seconds;
            const auto frames = static_cast<int64_t>(state.owed);  // toward zero: the rest waits for the next tick
            if (frames == 0)
                continue;
            state.owed -= static_cast<double>(frames);
            moves.push_back({instanceId, frames, static_cast<double>(frames) / std::max(seconds, 0.001), state.origin});
        }
    }
    for (const Move& move : moves)
        _jog(move.instanceId, move.frames, move.rate, move.origin);
}

size_t ScrubClock::Active() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    size_t active = 0;
    for (const auto& entry : _states)
        active += Moving(entry.second) ? 1 : 0;
    return active;
}

void ScrubClock::Run()
{
    std::unique_lock<std::mutex> lock(_mutex);
    while (!_quit)
    {
        bool any = false;
        for (const auto& entry : _states)
            any = any || Moving(entry.second);
        if (!any)
            break;
        _wake.wait_for(lock, kTick, [this] { return _quit; });
        if (_quit)
            break;
        lock.unlock();
        TickAt(NowNs());
        lock.lock();
    }
    _running = false;
}

std::vector<RegistrationHandle> RegisterScrubActions(ActionRegistry& registry, ScrubClock& clock)
{
    std::vector<RegistrationHandle> handles;
    ActionRules rules;
    rules.scope = ActionRules::Fixed(automation_gate::ScopeRewind);
    for (const bool forward : {false, true})
        handles.push_back(registry.Register(
            {forward ? "ttd.scrub_forward" : "ttd.scrub_back", forward ? "Scrub forward" : "Scrub back", "Time travel",
             ValueKind::Level, false, {}, {}, false},
            [&clock, forward](const ActionCall& call) {
                clock.Set(call.instanceId, forward, call.value.level, call.origin);
                return ActionResult::Success();
            },
            rules));
    return handles;
}

}  // namespace actions
