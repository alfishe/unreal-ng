#pragma once

/// @file ihostinputsource.h
/// @brief What the controllers library (or a test) gives the core: the retro controls host devices hold, per
/// connector, sampled once per frame (architecture.md §7, late-latch sampling at the frame start).

#include <cstdint>
#include <vector>

#include "controllertypes.h"

namespace controllers
{

class IHostInputSource
{
public:
    virtual ~IHostInputSource() = default;

    /// Called on the machine's thread at each frame boundary, after the TTD checkpoint (host input is journaled
    /// input). Append one sample per connector the source drives; a connector left out keeps its state.
    /// Must not block: read the source's own latest state (a seqlock table in the library)
    virtual void Sample(uint64_t frame, std::vector<ConnectorSample>& out) = 0;
};

}  // namespace controllers
