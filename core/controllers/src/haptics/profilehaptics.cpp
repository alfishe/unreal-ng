#include "haptics/profilehaptics.h"

#include <algorithm>

namespace controllers
{

namespace
{
bool Decays(uint8_t shape)
{
    shape &= 0x0F;
    return shape <= 3 || shape == 8 || shape == 9;
}

bool Meets(HapticWhen when, uint8_t before, uint8_t now, uint8_t value)
{
    switch (when)
    {
        case HapticWhen::Changed:
            return true;
        case HapticWhen::Decreased:
            return now < before;
        case HapticWhen::Increased:
            return now > before;
        case HapticWhen::Equal:
            return now == value;
        case HapticWhen::NotEqual:
            return now != value;
        case HapticWhen::Less:
            return now < value;
        case HapticWhen::Greater:
            return now > value;
    }
    return false;
}

RumbleLevels Scaled(const RumbleLevels& levels, float scale)
{
    auto clamp = [scale](float level) { return std::min(1.0f, level * scale); };
    return {clamp(levels.low), clamp(levels.high), clamp(levels.leftTrigger), clamp(levels.rightTrigger)};
}
}  // namespace

ProfileHaptics::ProfileHaptics(HapticProfile profile, std::string name, HapticMixer& mixer,
                               std::function<std::vector<HostDeviceId>()> targets,
                               std::function<uint8_t(const HostDeviceId&)> actuators)
    : _profile(std::move(profile)),
      _name(std::move(name)),
      _mixer(mixer),
      _targets(std::move(targets)),
      _actuators(std::move(actuators))
{
    _states.resize(_profile.sources.size());
    for (size_t index = 0; index < _profile.sources.size(); index++)
    {
        const HapticSource& source = _profile.sources[index];
        if (!source.IsProbe() || _watches.size() >= HapticFrame::kMaxWatches)
            continue;
        _states[index].watch = static_cast<int>(_watches.size());
        _watches.push_back({source.address, source.page});
    }
    for (const HapticSource& source : _profile.sources)
        _levels.emplace_back(source.name, 0.0f);
}

std::vector<HapticWatch> ProfileHaptics::Watches() const
{
    return _watches;
}

float ProfileHaptics::Evaluate(const HapticSource& source, State& state, const HapticFrame& frame)
{
    constexpr float kRelease = 0.7f;
    constexpr float kFloor = 0.05f;
    constexpr float kBeeperFull = 10.0f;
    const unsigned chips = std::min<unsigned>(frame.ayCount, HapticFrame::kMaxAy);
    switch (source.kind)
    {
        case HapticSourceKind::AyNoise:
        {
            float noise = 0;
            for (unsigned chip = 0; chip < chips; chip++)
            {
                if (!source.anyChip && chip != source.chip)
                    continue;
                for (uint8_t volume : frame.ay[chip].noiseVolume)
                    noise = std::max(noise, volume >= 16 ? 0.8f : volume / 15.0f);
            }
            state.level = std::max(noise, state.level * kRelease);
            if (state.level < kFloor)
                state.level = 0;
            return state.level;
        }
        case HapticSourceKind::AyEnvelope:
            for (unsigned chip = 0; chip < chips; chip++)
            {
                const HapticAyFrame& ay = frame.ay[chip];
                if ((source.anyChip || chip == source.chip) && ay.envelopeRestarts > 0 && ay.envelopeUsed &&
                    Decays(ay.envelopeShape))
                    return 1.0f;
            }
            return 0;
        case HapticSourceKind::BeeperDensity:
        {
            if (frame.frameTStates == 0 || frame.cpuHz == 0)
                return 0;
            const float ms = frame.frameTStates * 1000.0f / static_cast<float>(frame.cpuHz);
            const float rate = frame.beeperEdges / ms;  // edges per ms = kHz
            return rate > source.minRateKhz ? std::min(1.0f, rate / kBeeperFull) : 0.0f;
        }
        case HapticSourceKind::Write:
        case HapticSourceKind::Value:
        case HapticSourceKind::Frame:
        {
            if (state.watch < 0 || static_cast<unsigned>(state.watch) >= frame.watchCount)
                return 0;
            const uint8_t now = frame.watched[static_cast<size_t>(state.watch)];
            const bool seen = state.seen;
            const uint8_t before = state.last;
            state.seen = true;
            state.last = now;
            if (source.kind == HapticSourceKind::Value)
                return std::min(1.0f, now * source.scale / 255.0f);
            if (source.kind == HapticSourceKind::Frame)
                return (now & source.mask) == (source.value & source.mask) ? 1.0f : 0.0f;
            // The first frame only learns the byte: a profile attached mid-game does not fire on its old value
            return seen && now != before && Meets(source.when, before, now, source.value) ? 1.0f : 0.0f;
        }
    }
    return 0;
}

void ProfileHaptics::OnHapticFrame(const HapticFrame& frame)
{
    std::vector<float> levels(_profile.sources.size(), 0.0f);
    for (size_t index = 0; index < _profile.sources.size(); index++)
        levels[index] = Evaluate(_profile.sources[index], _states[index], frame);
    {
        std::lock_guard<std::mutex> lock(_levelsMutex);
        for (size_t index = 0; index < levels.size(); index++)
            _levels[index].second = levels[index];
    }

    std::vector<std::pair<const HapticRule*, HapticEffect>> effects;
    for (const HapticRule& rule : _profile.rules)
    {
        size_t index = 0;
        while (index < _profile.sources.size() && _profile.sources[index].name != rule.on)
            index++;
        if (index == _profile.sources.size() || levels[index] <= 0)
            continue;
        HapticEffect effect;
        if (!BuiltinHapticEffect(rule.play, effect))
            continue;
        const HapticSource& source = _profile.sources[index];
        effect.levels = Scaled(effect.levels, rule.scale * levels[index]);
        if (rule.priority > 0)
            effect.priority = rule.priority;
        if (!source.IsEvent())
        {
            // A level holds the effect at its peak while it lasts: the envelope would restart every frame
            effect.shape = HapticShape::Pulse;
            effect.durationMs = kContinuousMs;
        }
        effects.emplace_back(&rule, std::move(effect));
    }
    if (effects.empty())
        return;
    for (const HostDeviceId& device : _targets())
    {
        const uint8_t actuators = _actuators ? _actuators(device) : static_cast<uint8_t>(kTargetAll);
        for (const auto& [rule, effect] : effects)
        {
            HapticEffect played = effect;
            played.levels = RetargetLevels(effect.levels, rule->TargetFor(actuators));
            _mixer.Play(device, played);
        }
    }
}

std::vector<std::pair<std::string, float>> ProfileHaptics::SourceLevels() const
{
    std::lock_guard<std::mutex> lock(_levelsMutex);
    return _levels;
}

}  // namespace controllers
