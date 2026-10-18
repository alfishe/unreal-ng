#pragma once

/// @file profilehaptics.h
/// @brief A game's haptics (haptics.md §5, §7): the sources of its profile's `haptics` section read from the core's
/// frame summaries, and its rules played on the mixer. Probes are read at the frame end (the core reads the watched
/// bytes, IHapticFeed::Watches), so a `write` probe sees a byte that changed between two frame ends; a change undone
/// within one frame is not seen until the exec / write watches of the debugger come (TODO.md H3).

#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/controllers/ihapticfeed.h"
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
        bool seen = false;
        uint8_t last = 0;
        float level = 0;  ///< after the release (ay-noise)
    };
    float Evaluate(const HapticSource& source, State& state, const HapticFrame& frame);

    HapticProfile _profile;
    std::string _name;
    HapticMixer& _mixer;
    std::function<std::vector<HostDeviceId>()> _targets;
    std::function<uint8_t(const HostDeviceId&)> _actuators;
    std::vector<HapticWatch> _watches;
    std::vector<State> _states;  ///< per source
    mutable std::mutex _levelsMutex;
    std::vector<std::pair<std::string, float>> _levels;
};

}  // namespace controllers
