#include "haptics/hapticmixer.h"

#include <algorithm>

namespace controllers
{

float HapticEffect::Envelope(uint32_t ms) const
{
    if (ms >= durationMs)
        return 0;
    switch (shape)
    {
        case HapticShape::Pulse:
            return 1.0f;
        case HapticShape::Adsr:
        {
            if (ms < attackMs)
                return static_cast<float>(ms) / static_cast<float>(attackMs);
            ms -= attackMs;
            if (ms < decayMs)
                return 1.0f - (1.0f - sustain) * static_cast<float>(ms) / static_cast<float>(decayMs);
            const uint32_t left = durationMs - std::min(durationMs, attackMs + ms);  // ms is from the decay's start
            if (left < releaseMs)
                return sustain * static_cast<float>(left) / static_cast<float>(releaseMs);
            return sustain;
        }
        case HapticShape::Pattern:
        {
            uint32_t cycle = 0;
            for (const auto& [length, level] : pattern)
                cycle += length;
            if (cycle == 0)
                return 0;
            ms %= cycle;
            for (const auto& [length, level] : pattern)
            {
                if (ms < length)
                    return level;
                ms -= length;
            }
            return 0;
        }
    }
    return 0;
}

HapticMixer::HapticMixer(Writer writer, bool threaded, NowFunction now) : _writer(std::move(writer)), _now(std::move(now))
{
    if (threaded)
        _thread = std::thread([this] { Run(); });
}

HapticMixer::~HapticMixer()
{
    {
        std::lock_guard lock(_mutex);
        _stopping = true;
    }
    _wake.notify_all();
    if (_thread.joinable())
        _thread.join();
    StopAll();
}

void HapticMixer::Play(const HostDeviceId& device, const HapticEffect& effect)
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    {
        std::lock_guard lock(_mutex);
        if (!_gate && !effect.feedback)
            return;
        const Clock::time_point now = Now();
        _active[device].push_back({effect, now, now + std::chrono::milliseconds(effect.durationMs)});
        TickLocked(now, writes);
    }
    Write(writes);
    _wake.notify_all();
}

void HapticMixer::SetGate(bool open)
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    {
        std::lock_guard lock(_mutex);
        if (_gate == open)
            return;
        _gate = open;
        if (!open)
        {
            for (auto& [device, effects] : _active)
                effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Active& a) { return !a.effect.feedback; }),
                              effects.end());
            TickLocked(Now(), writes);
        }
    }
    Write(writes);
}

bool HapticMixer::GateOpen() const
{
    std::lock_guard lock(_mutex);
    return _gate;
}

void HapticMixer::SetStrength(float strength)
{
    std::lock_guard lock(_mutex);
    _strength = std::clamp(strength, 0.0f, 1.0f);
}

void HapticMixer::Forget(const HostDeviceId& device)
{
    std::lock_guard lock(_mutex);
    _active.erase(device);
    _written.erase(device);
}

void HapticMixer::StopGameEffects(const HostDeviceId& device)
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    {
        std::lock_guard lock(_mutex);
        auto found = _active.find(device);
        if (found == _active.end())
            return;
        auto& effects = found->second;
        effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Active& a) { return !a.effect.feedback; }),
                      effects.end());
        TickLocked(Now(), writes);
    }
    Write(writes);
}

void HapticMixer::StopAll()
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    {
        std::lock_guard lock(_mutex);
        _active.clear();
        TickLocked(Now(), writes);
    }
    Write(writes);
}

void HapticMixer::Tick(Clock::time_point now)
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    {
        std::lock_guard lock(_mutex);
        TickLocked(now, writes);
    }
    Write(writes);
}

RumbleLevels HapticMixer::Written(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    auto it = _written.find(device);
    return it == _written.end() ? RumbleLevels{} : it->second;
}

void HapticMixer::TickLocked(Clock::time_point now, std::vector<std::pair<HostDeviceId, RumbleLevels>>& writes)
{
    // Every device written or playing: the loudest effect per channel at its envelope, lower priorities ducked under
    // the highest playing, times the master strength
    std::map<HostDeviceId, RumbleLevels> wanted;
    for (const auto& [device, levels] : _written)
        wanted[device] = {};
    for (auto it = _active.begin(); it != _active.end();)
    {
        std::vector<Active>& effects = it->second;
        effects.erase(std::remove_if(effects.begin(), effects.end(), [now](const Active& a) { return a.end <= now; }),
                      effects.end());
        RumbleLevels& mix = wanted[it->first];
        uint8_t top = 0;
        for (const Active& active : effects)
            top = std::max(top, active.effect.priority);
        for (const Active& active : effects)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - active.start).count();
            float scale = active.effect.Envelope(static_cast<uint32_t>(std::max<int64_t>(0, elapsed))) * _strength;
            if (active.effect.priority < top)
                scale *= kDuck;
            const RumbleLevels& peak = active.effect.levels;
            // HD haptics: each band plays at the frequency of the effect loudest in it (0 while the band is silent)
            if (peak.low * scale > mix.low)
            {
                mix.low = peak.low * scale;
                mix.lowHz = peak.lowHz;
            }
            if (peak.high * scale > mix.high)
            {
                mix.high = peak.high * scale;
                mix.highHz = peak.highHz;
            }
            mix.leftTrigger = std::max(mix.leftTrigger, peak.leftTrigger * scale);
            mix.rightTrigger = std::max(mix.rightTrigger, peak.rightTrigger * scale);
        }
        it = effects.empty() ? _active.erase(it) : std::next(it);
    }
    for (const auto& [device, levels] : wanted)
    {
        auto written = _written.find(device);
        if (written != _written.end() && written->second == levels)
            continue;
        _written[device] = levels;
        writes.emplace_back(device, levels);
    }
}

void HapticMixer::Write(const std::vector<std::pair<HostDeviceId, RumbleLevels>>& writes)
{
    for (const auto& [device, levels] : writes)
        if (_writer && !_writer(device, levels))
            Forget(device);
}

void HapticMixer::Run()
{
    std::unique_lock lock(_mutex);
    while (!_stopping)
    {
        Clock::time_point next = Clock::time_point::max();
        for (const auto& [device, effects] : _active)
        {
            for (const Active& active : effects)
            {
                next = std::min(next, active.end);
                if (active.effect.shape != HapticShape::Pulse)
                    next = std::min(next, Now() + kEnvelopePeriod);
            }
        }
        if (next == Clock::time_point::max())
            _wake.wait(lock);
        else
            _wake.wait_until(lock, next);
        if (_stopping)
            break;
        std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
        TickLocked(Now(), writes);
        lock.unlock();
        Write(writes);
        lock.lock();
    }
}

}  // namespace controllers
