#include "multisounddacs.h"

#include <algorithm>
#include <cmath>

#include "3rdparty/blip_buf/blip_buf.h"
#include "emulator/sound/audio.h"

namespace
{
constexpr int kBlipSamples = MAX_SAMPLES_PER_FRAME + 64;
constexpr size_t kEventBlobSize = 8 + 1 + 1 + 1;
constexpr size_t kBlobSize = 1 + MultiSoundDacs::kChannels * 2 + 8 + 4 + MultiSoundDacs::kMaxPendingEvents * kEventBlobSize + 8;

struct BlobWriter
{
    uint8_t* p;
    void U8(uint8_t v) { *p++ = v; }
    void U32(uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
    void U64(uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
};

struct BlobReader
{
    const uint8_t* p;
    uint8_t U8() { return *p++; }
    uint32_t U32()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
            v |= static_cast<uint32_t>(*p++) << (8 * i);
        return v;
    }
    uint64_t U64()
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<uint64_t>(*p++) << (8 * i);
        return v;
    }
};

int16_t ClampSample(double value)
{
    return static_cast<int16_t>(std::clamp<long>(std::lround(value), -32768L, 32767L));
}
} // namespace

MultiSoundDacs::~MultiSoundDacs()
{
    blip_delete(_blipLeft);
    blip_delete(_blipRight);
}

void MultiSoundDacs::Configure(const MultiSoundDacsConfig& cfg)
{
    _cfg = cfg;
    _cfg.hostTickRate = std::max<uint32_t>(_cfg.hostTickRate, 1);
    _cfg.outputRate = std::max<uint32_t>(_cfg.outputRate, 1);

    if (!_blipLeft)
        _blipLeft = blip_new(kBlipSamples);
    if (!_blipRight)
        _blipRight = blip_new(kBlipSamples);
    SetOutputRate(_cfg.outputRate);
    Reset(0);
}

void MultiSoundDacs::Reset(uint64_t t)
{
    _s.channels = {};
    _s.pendingCount = 0;
    _s.time = t;
    _s.frameStart = t;
    _s.emittedLeft = 0;
    _s.emittedRight = 0;
    _lastSample[0] = _lastSample[1] = 0;
    if (_blipLeft)
        blip_clear(_blipLeft);
    if (_blipRight)
        blip_clear(_blipRight);
    _filterLeft.Reset();
    _filterRight.Reset();
}

void MultiSoundDacs::SetOutputRate(uint32_t rate)
{
    _cfg.outputRate = std::max<uint32_t>(rate, 1);
    if (_blipLeft)
    {
        blip_set_rates(_blipLeft, _cfg.hostTickRate, _cfg.outputRate);
        blip_clear(_blipLeft);
    }
    if (_blipRight)
    {
        blip_set_rates(_blipRight, _cfg.hostTickRate, _cfg.outputRate);
        blip_clear(_blipRight);
    }
    // Tick span the buffer can hold before EndFrame must drain it (32 samples of headroom)
    _frameCapacityTicks = static_cast<uint64_t>(kBlipSamples - 32) * _cfg.hostTickRate / _cfg.outputRate;
    _s.frameStart = _s.time;
    // blip_clear dropped the level the buffer held: the next step starts from silence
    _s.emittedLeft = 0;
    _s.emittedRight = 0;
    DesignFilters();
    Emit(_s.time);
}

void MultiSoundDacs::SetRenderMode(MultiSoundRenderMode mode)
{
    _cfg.renderMode = mode;
    DesignFilters();
}

void MultiSoundDacs::DesignFilters()
{
    const double corner = MultiSoundBoard::DacCornerHz();
    _filterLeft = MultiSoundRcFilter::LowPass1(corner, _cfg.outputRate);
    _filterRight = MultiSoundRcFilter::LowPass1(corner, _cfg.outputRate);
}

/// region <Events>

void MultiSoundDacs::GsSample(uint64_t strobeEnd, int channel, uint8_t value)
{
    Submit(Event{ strobeEnd, MultiSoundDacStrobe::GsSample, static_cast<uint8_t>(channel & 3), value });
}

void MultiSoundDacs::GsVolume(uint64_t strobeEnd, int channel, uint8_t volume)
{
    Submit(Event{ strobeEnd, MultiSoundDacStrobe::GsVolume, static_cast<uint8_t>(channel & 3), volume });
}

void MultiSoundDacs::SoundriveWrite(uint64_t strobeEnd, int channel, uint8_t value)
{
    Submit(Event{ strobeEnd, MultiSoundDacStrobe::SoundriveWrite, static_cast<uint8_t>(channel & 3), value });
}

void MultiSoundDacs::Submit(const Event& event)
{
    if (event.strobeEnd < _s.time)
    {
        // Ends before the time already run to: every pending event ends later, so applying it now keeps the order
        _s.lateEvents++;
        Apply(event, _s.time);
        return;
    }

    if (_s.pendingCount == kMaxPendingEvents)
        Run(_s.pending[0].strobeEnd);     // full: run to the earliest event (this event is not earlier than it)

    // Insert after every event that does not sort after it (stable for equal keys)
    auto begin = _s.pending.begin();
    auto end = begin + static_cast<std::ptrdiff_t>(_s.pendingCount);
    auto at = std::upper_bound(begin, end, event, &MultiSoundDacs::Before);
    std::move_backward(at, end, end + 1);
    *at = event;
    _s.pendingCount++;
}

void MultiSoundDacs::Run(uint64_t t)
{
    size_t applied = 0;
    while (applied < _s.pendingCount && _s.pending[applied].strobeEnd <= t)
    {
        const Event& event = _s.pending[applied];
        Apply(event, std::max(event.strobeEnd, _s.time));
        applied++;
    }
    if (applied)
    {
        auto begin = _s.pending.begin();
        std::move(begin + static_cast<std::ptrdiff_t>(applied), begin + static_cast<std::ptrdiff_t>(_s.pendingCount), begin);
        _s.pendingCount -= applied;
        std::fill(begin + static_cast<std::ptrdiff_t>(_s.pendingCount), begin + static_cast<std::ptrdiff_t>(_s.pendingCount + applied), Event{});
    }
    _s.time = std::max(_s.time, t);
}

void MultiSoundDacs::Apply(const Event& event, uint64_t at)
{
    // The CPLD's DAC block (tdd-card-logic.md L14): the same three register writes as MultiSoundLogic
    MultiSoundDacState& channel = _s.channels[event.channel & 3u];
    switch (event.kind)
    {
        case MultiSoundDacStrobe::GsSample:
            channel.sample = MultiSoundLogic::ConvertSample(event.value);
            break;
        case MultiSoundDacStrobe::GsVolume:
            channel.volume = event.value & 0x3Fu;
            break;
        case MultiSoundDacStrobe::SoundriveWrite:
            channel.sample = MultiSoundLogic::ConvertSample(event.value);
            channel.volume = 0x3F;
            break;
    }
    _s.time = std::max(_s.time, at);
    Emit(at);
}

/// endregion </Events>

/// region <Output>

void MultiSoundDacs::Emit(uint64_t at)
{
    const int32_t left = OutputLeft();
    const int32_t right = OutputRight();
    const int32_t deltaLeft = left - _s.emittedLeft;
    const int32_t deltaRight = right - _s.emittedRight;
    if (!deltaLeft && !deltaRight)
        return;
    const uint64_t offset = at > _s.frameStart ? at - _s.frameStart : 0;
    const uint64_t pos = std::min<uint64_t>(offset, _frameCapacityTicks ? _frameCapacityTicks - 1 : 0);
    if (_blipLeft && deltaLeft)
        blip_add_delta(_blipLeft, static_cast<unsigned>(pos), deltaLeft);
    if (_blipRight && deltaRight)
        blip_add_delta(_blipRight, static_cast<unsigned>(pos), deltaRight);
    _s.emittedLeft = left;
    _s.emittedRight = right;
}

size_t MultiSoundDacs::EndFrame(uint64_t t, int16_t* stereo, size_t frames)
{
    Run(t);
    frames = std::min<size_t>(frames, MAX_SAMPLES_PER_FRAME);
    const uint64_t length = std::min<uint64_t>(_s.time - _s.frameStart, _frameCapacityTicks);
    _s.frameStart = _s.time;
    if (!_blipLeft || !_blipRight || !stereo)
        return 0;

    blip_end_frame(_blipLeft, static_cast<unsigned>(length));
    blip_end_frame(_blipRight, static_cast<unsigned>(length));
    const int want = static_cast<int>(frames);
    const int gotLeft = blip_read_samples(_blipLeft, stereo, want, 1);
    const int gotRight = blip_read_samples(_blipRight, stereo + 1, want, 1);
    if (gotLeft > 0)
        _lastSample[0] = stereo[(gotLeft - 1) * 2];
    if (gotRight > 0)
        _lastSample[1] = stereo[(gotRight - 1) * 2 + 1];
    for (int i = gotLeft; i < want; i++)
        stereo[i * 2] = _lastSample[0];
    for (int i = gotRight; i < want; i++)
        stereo[i * 2 + 1] = _lastSample[1];

    if (_cfg.renderMode == MultiSoundRenderMode::Authentic)
    {
        for (int i = 0; i < want; i++)
        {
            stereo[i * 2] = ClampSample(_filterLeft.Process(stereo[i * 2]));
            stereo[i * 2 + 1] = ClampSample(_filterRight.Process(stereo[i * 2 + 1]));
        }
    }

    // A caller that reads fewer frames than it renders would fill the buffer: drop the surplus beyond a few frames
    int16_t discard[256];
    while (blip_samples_avail(_blipLeft) > 256)
        blip_read_samples(_blipLeft, discard, 256, 0);
    while (blip_samples_avail(_blipRight) > 256)
        blip_read_samples(_blipRight, discard, 256, 0);
    return frames;
}

/// endregion </Output>

/// region <TTD>

size_t MultiSoundDacs::TTDStateSize() const
{
    return kBlobSize;
}

void MultiSoundDacs::TTDSaveState(uint8_t* dst) const
{
    BlobWriter w{ dst };
    w.U8(kStateVersion);
    for (const MultiSoundDacState& channel : _s.channels)
    {
        w.U8(channel.sample);
        w.U8(channel.volume);
    }
    w.U64(_s.time);
    w.U32(static_cast<uint32_t>(_s.pendingCount));
    for (size_t i = 0; i < kMaxPendingEvents; i++)
    {
        const Event& event = _s.pending[i];
        w.U64(event.strobeEnd);
        w.U8(static_cast<uint8_t>(event.kind));
        w.U8(event.channel);
        w.U8(event.value);
    }
    w.U64(_s.lateEvents);
}

void MultiSoundDacs::TTDLoadState(const uint8_t* src)
{
    BlobReader r{ src };
    if (r.U8() != kStateVersion)
        return;     // a foreign layout: keep the live state rather than load garbage
    for (MultiSoundDacState& channel : _s.channels)
    {
        channel.sample = r.U8();
        channel.volume = r.U8() & 0x3Fu;
    }
    _s.time = r.U64();
    _s.pendingCount = std::min<size_t>(r.U32(), kMaxPendingEvents);
    for (size_t i = 0; i < kMaxPendingEvents; i++)
    {
        Event& event = _s.pending[i];
        event.strobeEnd = r.U64();
        event.kind = static_cast<MultiSoundDacStrobe>(std::min<uint8_t>(r.U8(), static_cast<uint8_t>(MultiSoundDacStrobe::GsSample)));
        event.channel = r.U8() & 3u;
        event.value = r.U8();
    }
    _s.lateEvents = r.U64();

    // The output buffers and the filter history belong to the host: restart the frame here and step from silence to
    // the restored level at its start
    if (_blipLeft)
        blip_clear(_blipLeft);
    if (_blipRight)
        blip_clear(_blipRight);
    _s.frameStart = _s.time;
    _s.emittedLeft = 0;
    _s.emittedRight = 0;
    _lastSample[0] = _lastSample[1] = 0;
    _filterLeft.Reset();
    _filterRight.Reset();
    Emit(_s.time);
}

uint64_t MultiSoundDacs::TTDHashState() const
{
    uint8_t blob[kBlobSize];
    TTDSaveState(blob);
    uint64_t h = 14695981039346656037ull;
    for (uint8_t b : blob)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

/// endregion </TTD>
