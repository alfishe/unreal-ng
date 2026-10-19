#pragma once

/// @file ihapticfeed.h
/// @brief The core's side of the haptic feed (docs/inprogress/2026-10-17-controller-manager/haptics.md §3, §4): one
/// summary of the emulated sound per frame, from register writes and port edges, never from samples. Host output only
/// (H-4): the feed reads state and changes nothing, so the emulation is the same with it on or off.

#include <array>
#include <cstdint>
#include <vector>

namespace controllers
{

/// One AY / YM chip over a frame
struct HapticAyFrame
{
    /// ay-noise (haptics.md §4): per channel, the volume (0..15, 16 = the envelope) where R7 enables its noise; 0 where
    /// it does not
    std::array<uint8_t, 3> noiseVolume{};
    /// ay-envelope: R13 writes this frame (each restarts the envelope) and the shape last written
    uint8_t envelopeRestarts = 0;
    uint8_t envelopeShape = 0;
    /// a channel takes its volume from the envelope (R8-R10 bit 4)
    bool envelopeUsed = false;
};

/// A byte a game profile's probe reads at the frame end (haptics.md §5): a CPU address, or an offset in a RAM page
struct HapticWatch
{
    uint16_t address = 0;
    uint16_t page = 0xFFFF;  ///< 0xFFFF: `address` through the current mapping; else the RAM page, `address & #3FFF`
};

struct HapticFrame
{
    uint64_t frame = 0;
    static constexpr unsigned kMaxAy = 3;  ///< TurboSound 2, the Next 3
    std::array<HapticAyFrame, kMaxAy> ay{};
    unsigned ayCount = 0;
    /// beeper-density: EAR (bit 4 of #FE) edges this frame
    uint32_t beeperEdges = 0;
    /// the frame's length in T-states (edges per ms from it)
    uint32_t frameTStates = 0;
    uint32_t cpuHz = 3500000;
    /// The watched bytes at the frame end, in the order of IHapticFeed::Watches
    static constexpr unsigned kMaxWatches = 16;
    std::array<uint8_t, kMaxWatches> watched{};
    unsigned watchCount = 0;
};

/// Where a machine's ControllerManager sends its frames (HostInputService::HapticFeedFor). Called on the emulator
/// thread at the frame boundary: it must not block
class IHapticFeed
{
public:
    virtual ~IHapticFeed() = default;
    virtual void OnHapticFrame(const HapticFrame& frame) = 0;
    /// The bytes to read at every frame end (at most HapticFrame::kMaxWatches); asked once, when the feed is set
    virtual std::vector<HapticWatch> Watches() const
    {
        return {};
    }
};

}  // namespace controllers
