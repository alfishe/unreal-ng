#pragma once

/// @file soundhaptics.h
/// @brief The generic haptics of haptics.md §4 from the core's frame summaries: `ay-noise` drives the low motor while
/// noise channels sound, `ay-envelope` turns an envelope restart with a decaying shape into a thump, `beeper-density`
/// turns dense EAR edges (beeper noise bursts, engines) into the high motor. The defaults without a game profile.

#include <functional>
#include <vector>

#include "emulator/io/controllers/ihapticfeed.h"
#include "haptics/hapticmixer.h"

namespace controllers
{

class SoundHaptics : public IHapticFeed
{
public:
    /// @p targets: the devices the instance's haptics play on (the ones that follow it, with motors)
    SoundHaptics(HapticMixer& mixer, std::function<std::vector<HostDeviceId>()> targets);

    void OnHapticFrame(const HapticFrame& frame) override;

    /// The levels the last frame asked for (tests, the Haptics tab's meters)
    const RumbleLevels& Last() const
    {
        return _last;
    }

    static constexpr float kNoiseFloor = 0.05f;
    static constexpr float kBeeperEdgesPerMs = 2.0f;  ///< below: a tune or silence; above: noise or an engine
    static constexpr uint32_t kContinuousMs = 45;     ///< a frame's continuous level lasts into the next frame
    static constexpr uint32_t kThumpMs = 220;

private:
    HapticMixer& _mixer;
    std::function<std::vector<HostDeviceId>()> _targets;
    float _noise = 0;  ///< ay-noise after its release
    RumbleLevels _last;
};

}  // namespace controllers
