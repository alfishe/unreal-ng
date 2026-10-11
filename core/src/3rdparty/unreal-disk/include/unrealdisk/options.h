#pragma once

#include <cstdint>

namespace unrealdisk
{
/// What a format driver takes from its host instead of the emulator's context
struct LoaderOptions
{
    /// TR-DOS sector interleave pattern for new TRD / SCL layouts: 0 = 1:1, 1 = 1:2, 2 = 1:3 (the emulator's [BETA128] IL - 1)
    uint8_t trdosInterleave = 0;
};
}  // namespace unrealdisk
