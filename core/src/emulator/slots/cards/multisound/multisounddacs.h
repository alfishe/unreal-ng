#pragma once

// ZX-MultiSound: the four DAC channels shared by the General Sound and the SounDrive (hardware-reference.md §4.3,
// architecture.md §4.4, tdd-card-logic.md rule L14 and finding F7 / F9).
//
// Input: strobe events with the time their strobe ends on the card time axis (hostTickRate ticks per second, the
// emulator's audio T-states for a 3.5 MHz axis):
//   GsSample       GS memory read at #6000-#7FFF (channel = A9-A8): the channel's sample
//   GsVolume       GS port 6-9 write: the channel's 6-bit volume
//   SoundriveWrite host port write (#0F / #1F / #4F / #5F family): the channel's sample and volume 63
// The CPLD rewrites a register on every 32 MHz edge its strobe is active, so the strobe that ends later wins. The GS and
// the host run on separate timelines and hand their events over out of order, so events wait in a queue sorted by
// strobe end and are applied by Run(t), which the card calls once nothing earlier than t can still arrive (both
// timelines have reached t). Equal end times count as the same last edge, where the RTL's priority decides: GS sample
// over SounDrive sample, SounDrive volume over GS volume. An event that ends before the time already run to is applied
// at that time (counted in LateEvents()).
//
// Output (HiFi): the measured transfer of each channel, mean pin level 0.5 + 0.5 x level / 128 x gain / 64
// (MultiSoundLogic::SampleLevel / VolumeGain64: level -127..+127 with #7F and #80 both 0, gain = volume except 63 = 64),
// without the midpoint (the board's coupling capacitors remove it) and without the sigma-delta noise. A channel
// contributes level x gain units (full scale kChannelFullScale = 128 x 64, never reached); channels 0 and 1 sum into
// the left output, 2 and 3 into the right, no cross-feed. Steps go into a blip_buf pair at their exact time.
// Authentic adds each channel's 1-pole RC (1k, 10n, loaded by 47k: 16.25 kHz) after the band-limited synthesis; the
// network is linear and identical on both channels of a side, so filtering the side's sum is the same.
// The board weight (0.208) and the absolute level are MultiSoundMixer's.
//
// The output is not muted by any state: a volume of 0 or a sample of #7F / #80 is silence.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/slots/cards/multisound/multisoundanalog.h"
#include "emulator/slots/cards/multisound/multisoundlogic.h"

struct blip_t;

struct MultiSoundDacsConfig
{
    uint32_t hostTickRate = 3500000;    ///< ticks per second of every timestamp passed in
    uint32_t outputRate = 44100;        ///< stereo frames per second out of EndFrame
    MultiSoundRenderMode renderMode = MultiSoundRenderMode::HiFi;
};

/// Kind of a DAC strobe; the numeric order is the priority on an equal end time (higher applies later and wins)
enum class MultiSoundDacStrobe : uint8_t
{
    GsVolume = 0,       ///< loses the volume to a SounDrive write on the same edge
    SoundriveWrite = 1, ///< loses the sample to a GS sample on the same edge
    GsSample = 2
};

class MultiSoundDacs : public ttd::TTDSerializable
{
public:
    static constexpr int kChannels = 4;
    /// Output units of one channel at the transfer's full scale (level 128, gain 64; reached by neither)
    static constexpr int32_t kChannelFullScale = 128 * 64;
    /// Events that can wait for Run at once; a full queue applies its earliest event early
    static constexpr size_t kMaxPendingEvents = 1024;
    /// TTD blob layout version (first byte of the blob)
    static constexpr uint8_t kStateVersion = 1;

    struct Event
    {
        uint64_t strobeEnd = 0;
        MultiSoundDacStrobe kind = MultiSoundDacStrobe::GsSample;
        uint8_t channel = 0;
        uint8_t value = 0;      ///< the raw bus byte (sample) or the volume
    };

    MultiSoundDacs() = default;
    ~MultiSoundDacs() override;
    MultiSoundDacs(const MultiSoundDacs&) = delete;
    MultiSoundDacs& operator=(const MultiSoundDacs&) = delete;

    /// Rates and mode; allocates the output buffers (the only allocation), then a reset at time 0
    void Configure(const MultiSoundDacsConfig& cfg);
    const MultiSoundDacsConfig& Config() const { return _cfg; }

    /// Bus /RESET at time t: every channel {sample 0, volume 0} (the CPLD reset branch), pending events dropped
    void Reset(uint64_t t);

    // Strobe events (any order; applied by Run in strobe-end order)
    void GsSample(uint64_t strobeEnd, int channel, uint8_t value);
    void GsVolume(uint64_t strobeEnd, int channel, uint8_t volume);
    void SoundriveWrite(uint64_t strobeEnd, int channel, uint8_t value);
    void Submit(const Event& event);

    /// Applies every pending event that ends at or before t and advances the time axis to t
    void Run(uint64_t t);

    /// Run to t, close the output frame and read `frames` stereo frames (interleaved L / R) into `stereo`. Frames the
    /// band-limited buffer cannot supply repeat the last sample. Returns the frames written
    size_t EndFrame(uint64_t t, int16_t* stereo, size_t frames);

    /// Output rate change at a frame boundary; channel state is kept, pending output dropped
    void SetOutputRate(uint32_t rate);
    /// Render mode change at a frame boundary
    void SetRenderMode(MultiSoundRenderMode mode);

    /// A channel's registers after the events applied so far (the CPLD's dac* / vol*)
    MultiSoundDacState Channel(int channel) const { return _s.channels[static_cast<size_t>(channel & 3)]; }
    /// A channel's output in level x gain units
    static int32_t Transfer(const MultiSoundDacState& state)
    {
        return MultiSoundLogic::SampleLevel(state.sample) * MultiSoundLogic::VolumeGain64(state.volume);
    }
    int32_t OutputLeft() const { return Transfer(_s.channels[0]) + Transfer(_s.channels[1]); }
    int32_t OutputRight() const { return Transfer(_s.channels[2]) + Transfer(_s.channels[3]); }

    size_t PendingEvents() const { return _s.pendingCount; }
    uint64_t LateEvents() const { return _s.lateEvents; }
    uint64_t Time() const { return _s.time; }

    /// region <TTDSerializable>
    /// Fixed-size blob: version, the four channels, the time axis, the pending queue (count + events, unused slots
    /// zero), the late-event counter. No PeripheralId: the card's blob set carries it. The output buffers and the
    /// Authentic filter history are host-side: a load drops them and restarts the frame with one step from silence to
    /// the restored level
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "MultiSoundDacs"; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable>

private:
    struct State
    {
        std::array<MultiSoundDacState, kChannels> channels{};
        uint64_t time = 0;          // last time Run reached
        uint64_t frameStart = 0;    // time at the start of the output frame
        std::array<Event, kMaxPendingEvents> pending{};
        size_t pendingCount = 0;
        uint64_t lateEvents = 0;
        int32_t emittedLeft = 0;    // level the output buffer holds
        int32_t emittedRight = 0;
    };

    void Apply(const Event& event, uint64_t at);
    void Emit(uint64_t at);
    void DesignFilters();
    static bool Before(const Event& a, const Event& b)
    {
        return a.strobeEnd < b.strobeEnd ||
               (a.strobeEnd == b.strobeEnd && static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind));
    }

    MultiSoundDacsConfig _cfg;
    State _s;

    blip_t* _blipLeft = nullptr;
    blip_t* _blipRight = nullptr;
    uint64_t _frameCapacityTicks = 0;
    int16_t _lastSample[2] = {};
    MultiSoundRcFilter _filterLeft;
    MultiSoundRcFilter _filterRight;
};
