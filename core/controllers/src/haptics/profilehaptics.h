#pragma once

/// @file profilehaptics.h
/// @brief A game's haptics (haptics.md §5, §7): the sources of its profile's `haptics` section read from the core's
/// frame summaries, and its rules played on the mixer. `write` (without `each_write`), `value` and `frame` read the
/// watched bytes at the frame end (IHapticFeed::Watches); `exec`, `read`, `port-in`, `port-out` and `write` with
/// `each_write` are breakpoint probes (IHapticFeed::Probes), which see every access, so a change undone within one frame
/// fires too. `envelope`, `lowband` and `onset` filter the sample streams the feed asks for (IHapticFeed::Streams).
/// The sources' levels are also the game's events (IHapticFeed::TakeGameEvents, gameevents.h): a profile without rules
/// or a machine without a haptic device still publishes them.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/controllers/ihapticfeed.h"
#include "haptics/audiofilters.h"
#include "haptics/hapticmixer.h"
#include "haptics/hapticprofile.h"

namespace controllers
{

class ProfileHaptics : public IHapticFeed
{
public:
    /// @p name: the profile id (the Haptics tab); @p targets: the devices the instance's haptics play on
    /// @p actuators: a device's RumbleActuator bits (the target fallbacks); none: every actuator
    ProfileHaptics(HapticProfile profile, std::string name, HapticMixer& mixer,
                   std::function<std::vector<HostDeviceId>()> targets,
                   std::function<uint8_t(const HostDeviceId&)> actuators = nullptr);

    void OnHapticFrame(const HapticFrame& frame) override;
    std::vector<HapticWatch> Watches() const override;
    std::vector<HapticStream> Streams() const override;
    std::vector<HapticProbe> Probes() const override;
    /// A replay or seek: the game effects on the targets stop now, frames are ignored until the hold ends
    void OnHapticHold(bool held) override;
    /// Every source is a game event (the profile's `events:` and `haptics.sources`), with or without a device
    std::vector<GameEventInfo> GameEvents() const override;
    std::string GameEventsProfile() const override;
    bool TakeGameEvents(GameEventFrame& frame) override;

    const std::string& Name() const
    {
        return _name;
    }
    /// Per source, its level the last frame (an event: 1 on the frame it fired). Any thread
    std::vector<std::pair<std::string, float>> SourceLevels() const;

    static constexpr uint32_t kContinuousMs = 45;  ///< a level's effect lasts into the next frame

private:
    struct State
    {
        int watch = -1;  ///< the probe's index in Watches()
        int probe = -1;  ///< in Probes()
        int stream = -1; ///< in Streams()
        EnvelopeFilter envelope;
        LowbandFilter lowband;
        OnsetFilter onset;
        bool seen = false;
        uint8_t last = 0;
        float level = 0;  ///< after the release (ay-noise)
        int32_t value = -1;  ///< the watched byte (a word: the word) the last frame; -1: not a watch
    };
    float Evaluate(const HapticSource& source, State& state, const HapticFrame& frame);

    HapticProfile _profile;
    std::string _name;
    HapticMixer& _mixer;
    std::function<std::vector<HostDeviceId>()> _targets;
    std::function<uint8_t(const HostDeviceId&)> _actuators;
    std::vector<HapticWatch> _watches;
    std::vector<HapticStream> _streams;
    std::vector<HapticProbe> _probes;
    std::atomic<bool> _held{false};
    std::vector<State> _states;  ///< per source
    std::vector<float> _frameLevels;  ///< per source, the last frame's (emulator thread)
    bool _frameReady = false;         ///< OnHapticFrame evaluated a frame TakeGameEvents has not taken
    mutable std::mutex _levelsMutex;
    std::vector<std::pair<std::string, float>> _levels;
};

}  // namespace controllers
