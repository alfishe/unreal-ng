#pragma once

#include <cstdint>

/// AY / SSG stereo panning layout: which of the three tone channels goes to
/// the left, the centre and the right
enum class AYStereoMode : uint8_t
{
    ABC = 0,    // A=Left, B=Center, C=Right (default)
    ACB = 1,    // A=Left, C=Center, B=Right
    Mono = 2    // All channels center
};
