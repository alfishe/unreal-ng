#pragma once

#include <chrono>

#include "haptics/hapticmixer.h"

/// The time a test's HapticMixer lives in: one fixed instant that never moves by itself. A mixer reads the time when an
/// effect starts and when it is ticked; against the host's clock a stalled test (a loaded parallel run) would tick an
/// effect that is already over. Build the mixer with FixedMixerClock() and tick it with FixedMixerTime() + offset.
inline controllers::HapticMixer::Clock::time_point FixedMixerTime()
{
    return controllers::HapticMixer::Clock::time_point(std::chrono::hours(1));
}

inline controllers::HapticMixer::NowFunction FixedMixerClock()
{
    return [] { return FixedMixerTime(); };
}
