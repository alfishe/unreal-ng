#pragma once

/// @file ihapticfeed.h
/// @brief The core's side of the haptic feed (docs/inprogress/2026-10-17-controller-manager/haptics.md §3-§5): one
/// summary per frame - the sound's register writes and port edges, the sample streams a feed asked for (mono, 8 kHz,
/// before the mixer's volume and mute), the bytes its frame probes read and the hits of its breakpoint probes (exec,
/// read, write, port-in, port-out: hidden breakpoints that never stop). Host output only (H-4): the feed reads state and
/// changes nothing, so the emulation is the same with it on or off.

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

/// A sample stream the DSP filters read (haptics.md §4 source selectors)
enum class HapticStream : uint8_t
{
    Mix,      ///< `mix`: the machine's master mix before the host's volume and mute
    Beeper,   ///< `beeper`
    Ay0,      ///< `ay:0` .. `ay:2`: the AY socket's chips (TurboSound: two)
    Ay1,
    Ay2,
    Covox,    ///< `covox`: the Covox / Soundrive DAC, a machine's own DAC (the Sprinter's), the Next's DAC part
    Gs,       ///< `gs`, `neogs`: the General Sound slot's card
    NextDac,  ///< `next-dac`: the ZX Next's four-channel DAC alone
    Saa,        ///< `saa`: the SAA1099 (the ZX-MultiSound card's)
    Moonsound,  ///< `moonsound`: the OPL4's FM and PCM, summed
    Soundrive0, ///< `soundrive:0` .. `soundrive:3`: one Soundrive channel alone (left A, left B, right A, right B)
    Soundrive1,
    Soundrive2,
    Soundrive3,
};

/// One stream over a frame: mono at kRate, `count` samples
struct HapticStreamFrame
{
    static constexpr uint32_t kRate = 8000;
    static constexpr unsigned kMaxSamples = 256;  ///< a 20 ms frame is 160; the rest is slack for long frames
    HapticStream source = HapticStream::Mix;
    uint16_t count = 0;
    std::array<int16_t, kMaxSamples> samples{};
};

/// A breakpoint probe (haptics.md §5): a hidden breakpoint with the non-breaking "notify feed" action
enum class HapticProbeKind : uint8_t
{
    Exec,
    Read,
    Write,
    PortIn,
    PortOut,
};

struct HapticProbe
{
    HapticProbeKind kind = HapticProbeKind::Exec;
    uint16_t address = 0;     ///< a CPU address or a port
    uint16_t end = 0;         ///< inclusive; == address: one address
    uint16_t page = 0xFFFF;   ///< memory: 0xFFFF the CPU address; else the RAM page, `address & #3FFF` in it
    uint16_t portMask = 0xFFFF;  ///< ports: matches when (port & portMask) == (address & portMask)
};

/// One write (the byte before and after it) or OUT (the value) a probe saw this frame, in order
struct HapticProbeEvent
{
    uint8_t probe = 0;
    uint8_t before = 0;
    uint8_t value = 0;
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
    /// The streams of IHapticFeed::Streams, in its order (a source the machine lacks has count 0)
    static constexpr unsigned kMaxStreams = 4;
    std::array<HapticStreamFrame, kMaxStreams> streams{};
    unsigned streamCount = 0;
    /// Per probe of IHapticFeed::Probes (in its order) its hits this frame; the writes and OUTs as events, the first
    /// kMaxProbeEvents of the frame
    static constexpr unsigned kMaxProbes = 16;
    std::array<uint16_t, kMaxProbes> probeHits{};
    unsigned probeCount = 0;
    static constexpr unsigned kMaxProbeEvents = 64;
    std::array<HapticProbeEvent, kMaxProbeEvents> probeEvents{};
    unsigned probeEventCount = 0;
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
    /// The sample streams to send (at most HapticFrame::kMaxStreams); asked once, when the feed is set
    virtual std::vector<HapticStream> Streams() const
    {
        return {};
    }
    /// The breakpoint probes to arm (at most HapticFrame::kMaxProbes); asked once, when the feed is set. An exec, read
    /// or write probe runs the CPU on the debug path while it is armed (the cost of a probe, none without one)
    virtual std::vector<HapticProbe> Probes() const
    {
        return {};
    }
    /// The machine replays or seeks (a TTD seek, scrub or history replay, RZX playback): held = true at once, on the
    /// thread that starts it, before the first replayed frame; false when live play resumes (haptics.md §8, H-5)
    virtual void OnHapticHold(bool held)
    {
        (void)held;
    }
};

}  // namespace controllers
