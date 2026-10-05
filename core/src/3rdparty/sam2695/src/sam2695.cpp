// libsam2695 - Synth: time model, input queue, state blob, render layer.
#include "sam2695/sam2695.h"

#include "common/statearchive.h"
#include "common/wideint.h"
#include "midi/parser.h"
#include "midi/uart.h"
#include "render/resampler.h"
#include "synthcore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sam2695
{

namespace
{

constexpr uint32_t kStateMagic = 0x324D4153; // "SAM2"
constexpr uint32_t kStateVersion = 2; // 2: SAM-3 part parameters, SAM-4 effects
constexpr uint64_t kBusySamples = static_cast<uint64_t>(kInternalRate) * kResetBusyMs / 1000; // 1875
constexpr uint32_t kMinOutputRate = 8000;
constexpr uint32_t kMaxOutputRate = 192000;

enum class EventKind : uint8_t
{
    Byte,
    Reset,
    Panic   // every voice of every part stops (Synth::Panic)
};

struct Event
{
    uint64_t sample = 0;
    uint8_t byte = 0;
    EventKind kind = EventKind::Byte;

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(sample);
        ar(byte);
        ar(kind);
    }
};

} // namespace

struct Synth::Impl
{
    SynthConfig cfg;
    std::shared_ptr<const ISoundBank> bank;
    SynthCore core;
    Uart uart;
    MidiParser parser;

    // chip state beyond the core
    std::vector<Event> queue;   // ring, capacity cfg.eventCapacity
    uint32_t queueHead = 0;
    uint32_t queueCount = 0;
    uint64_t lastQueued = 0;    // sample of the newest queued event (keeps the queue ordered)
    uint64_t pos = 0;           // next internal sample to synthesize, a multiple of kControlBlock
    uint64_t busyUntil = 0;     // MIDI is ignored before this sample (reset in progress)
    uint64_t droppedBusy = 0;
    uint64_t droppedQueueFull = 0;

    // render layer (not chip state)
    std::vector<float> stream;  // interleaved stereo ring at the internal rate
    size_t streamHead = 0;
    size_t streamCount = 0;
    uint64_t overruns = 0;
    Resampler resampler;

    BankDigest Digest() const { return bank != nullptr ? bank->Digest() : BankDigest{}; }
    uint64_t SampleAt(uint64_t t) const { return MulDivCeil(t, kInternalRate, cfg.hostTickRate); }

    void Enqueue(uint64_t sample, uint8_t byte, EventKind kind)
    {
        sample = std::max({sample, pos, lastQueued});
        if (queueCount == queue.size())
        {
            droppedQueueFull++;
            return;
        }
        Event& e = queue[(queueHead + queueCount) % queue.size()];
        e.sample = sample;
        e.byte = byte;
        e.kind = kind;
        queueCount++;
        lastQueued = sample;
    }

    void AdvanceUart(uint64_t t)
    {
        uart.AdvanceTo(t, [this](uint64_t time, uint8_t byte) { Enqueue(SampleAt(time), byte, EventKind::Byte); });
    }

    void HardReset()
    {
        core.PowerOn();
        parser.Reset();
        parser.ClearCounters();
        uart.Reset();
        queueHead = queueCount = 0;
        lastQueued = pos;
        busyUntil = 0;
        droppedBusy = droppedQueueFull = 0;
    }

    void Apply(const Event& e, uint32_t offset)
    {
        if (e.kind == EventKind::Panic)
        {
            core.Panic();
            return;
        }
        if (e.kind == EventKind::Reset)
        {
            core.PowerOn();
            parser.Reset();
            busyUntil = cfg.resetDelay ? e.sample + kBusySamples : 0;
            return;
        }
        if (e.sample < busyUntil)
        {
            droppedBusy++;
            return;
        }
        parser.Feed(e.byte, [&](const MidiMessage& m) {
            if (core.Message(m, offset) && cfg.resetDelay)
                busyUntil = e.sample + kBusySamples;
        });
    }

    void PushStream(const float* left, const float* right, uint32_t n)
    {
        const size_t cap = stream.size() / 2;
        for (uint32_t i = 0; i < n; i++)
        {
            if (streamCount == cap)
            {
                streamHead = (streamHead + 1) % cap; // Render() lags: drop the oldest frame
                streamCount--;
                overruns++;
            }
            const size_t w = (streamHead + streamCount) % cap;
            // the output flushes anything below -600 dB to zero: no denormal reaches the host
            const float l = left[i], r = right[i];
            stream[w * 2] = std::fabs(l) < 1e-30f ? 0.0f : l;
            stream[w * 2 + 1] = std::fabs(r) < 1e-30f ? 0.0f : r;
            streamCount++;
        }
    }

    void RenderBlock()
    {
        FxBuses buses{};
        core.BeginBlock();
        uint32_t cur = 0;
        while (queueCount > 0 && queue[queueHead].sample < pos + kControlBlock)
        {
            const Event e = queue[queueHead];
            queueHead = static_cast<uint32_t>((queueHead + 1) % queue.size());
            queueCount--;
            const uint32_t offset = e.sample > pos ? static_cast<uint32_t>(e.sample - pos) : 0;
            if (offset > cur)
            {
                core.RenderSegment(cur, offset, buses);
                cur = offset;
            }
            Apply(e, offset);
        }
        core.RenderSegment(cur, kControlBlock, buses);
        float left[kControlBlock], right[kControlBlock];
        core.FinishBlock(buses, left, right);
        PushStream(left, right, kControlBlock);
        pos += kControlBlock;
    }

    template <class Ar>
    void SerializeState(Ar& ar)
    {
        uint32_t magic = kStateMagic, version = kStateVersion;
        BankDigest digest = Digest();
        uint32_t capacity = static_cast<uint32_t>(queue.size());
        ar(magic);
        ar(version);
        ar(digest);
        ar(capacity);
        ar(pos);
        ar(busyUntil);
        ar(lastQueued);
        ar(droppedBusy);
        ar(droppedQueueFull);
        ar(uart);
        ar(parser);
        ar(core);
        ar(queueHead);
        ar(queueCount);
        for (Event& e : queue)
            ar(e);
    }
};

Synth::Synth() : _impl(std::make_unique<Impl>())
{
    Configure(SynthConfig{});
}

Synth::~Synth() = default;

bool Synth::Configure(const SynthConfig& cfg)
{
    if (cfg.hostTickRate == 0 || cfg.outputRate < kMinOutputRate || cfg.outputRate > kMaxOutputRate ||
        cfg.eventCapacity < 16 || cfg.streamFrames < kControlBlock || cfg.polyphony > kMaxPolyphony)
        return false;
    Impl& d = *_impl;
    d.cfg = cfg;
    d.core.Configure(cfg);
    d.uart.Configure(cfg.hostTickRate);
    d.queue.assign(cfg.eventCapacity, Event{});
    d.stream.assign(static_cast<size_t>(cfg.streamFrames) * 2, 0.0f);
    d.streamHead = d.streamCount = 0;
    d.resampler.Reserve(kInternalRate, kMinOutputRate);
    d.resampler.Configure(kInternalRate, cfg.outputRate);
    d.pos = 0;
    d.HardReset();
    return true;
}

const SynthConfig& Synth::Config() const
{
    return _impl->cfg;
}

bool Synth::LoadBank(std::shared_ptr<const ISoundBank> bank)
{
    Impl& d = *_impl;
    d.bank = std::move(bank);
    d.core.SetBank(d.bank.get());
    d.HardReset();
    return d.bank != nullptr;
}

const ISoundBank* Synth::Bank() const
{
    return _impl->bank.get();
}

void Synth::Reset(uint64_t t)
{
    Impl& d = *_impl;
    d.AdvanceUart(t);
    d.uart.Reset();
    d.parser.ClearCounters();
    d.droppedBusy = d.droppedQueueFull = 0;
    d.Enqueue(d.SampleAt(t), 0, EventKind::Reset);
}

void Synth::Panic(uint64_t t)
{
    Impl& d = *_impl;
    d.AdvanceUart(t);
    d.Enqueue(d.SampleAt(t), 0, EventKind::Panic);
}

void Synth::WriteLine(uint64_t t, bool level)
{
    Impl& d = *_impl;
    d.uart.SetLevel(t, level, [&d](uint64_t time, uint8_t byte) { d.Enqueue(d.SampleAt(time), byte, EventKind::Byte); });
}

void Synth::WriteByte(uint64_t t, uint8_t byte)
{
    Impl& d = *_impl;
    d.AdvanceUart(t);
    d.Enqueue(d.SampleAt(t), byte, EventKind::Byte);
}

void Synth::Run(uint64_t t)
{
    Impl& d = *_impl;
    d.AdvanceUart(t);
    const uint64_t end = d.SampleAt(t) / kControlBlock * kControlBlock;
    while (d.pos + kControlBlock <= end)
        d.RenderBlock();
}

size_t Synth::Render(float* stereo, size_t maxFrames)
{
    Impl& d = *_impl;
    const size_t cap = d.stream.size() / 2;
    size_t produced = 0;
    while (produced < maxFrames)
    {
        const size_t span = std::min(d.streamCount, cap - d.streamHead);
        size_t consumed = 0;
        const size_t n = d.resampler.Process(&d.stream[d.streamHead * 2], span, consumed, stereo + produced * 2,
                                             maxFrames - produced);
        d.streamHead = (d.streamHead + consumed) % cap;
        d.streamCount -= consumed;
        produced += n;
        if (n == 0 && (consumed == 0 || d.streamCount == 0))
            break;
    }
    return produced;
}

void Synth::DiscardPendingAudio()
{
    _impl->streamHead = _impl->streamCount = 0;
    _impl->resampler.Reset();
}

void Synth::SetOutputRate(uint32_t rate)
{
    if (rate < kMinOutputRate || rate > kMaxOutputRate)
        return;
    _impl->cfg.outputRate = rate;
    _impl->resampler.Configure(kInternalRate, rate);
}

void Synth::SetInterpolation(Interpolation mode)
{
    _impl->cfg.interpolation = mode;
    _impl->core.SetInterpolation(mode);
}

size_t Synth::StateSize() const
{
    StateSizer sizer;
    const_cast<Impl&>(*_impl).SerializeState(sizer); // the sizer only reads
    return sizer.Size();
}

void Synth::SaveState(uint8_t* out) const
{
    StateWriter writer(out);
    const_cast<Impl&>(*_impl).SerializeState(writer); // the writer only reads
}

bool Synth::LoadState(const uint8_t* in, size_t size)
{
    Impl& d = *_impl;
    if (in == nullptr || size != StateSize())
        return false;
    // header first: magic, version, bank, queue capacity must match before anything is touched
    StateReader header(in, size);
    uint32_t magic = 0, version = 0, capacity = 0;
    BankDigest digest{};
    header(magic);
    header(version);
    header(digest);
    header(capacity);
    if (!header.Ok() || magic != kStateMagic || version != kStateVersion || digest != d.Digest() ||
        capacity != d.queue.size())
        return false;
    StateReader reader(in, size);
    d.SerializeState(reader);
    if (!reader.Ok())
        return false;
    // a blob is not trusted: clamp what indexes memory
    d.core.Sanitize();
    if (d.queueHead >= d.queue.size() || d.queueCount > d.queue.size())
        d.queueHead = d.queueCount = 0;
    d.pos -= d.pos % kControlBlock;
    d.streamHead = d.streamCount = 0;
    d.resampler.Reset();
    return true;
}

bool Synth::StateBank(const uint8_t* in, size_t size, BankDigest& digest)
{
    if (in == nullptr)
        return false;
    StateReader header(in, size);
    uint32_t magic = 0, version = 0;
    header(magic);
    header(version);
    header(digest);
    return header.Ok() && magic == kStateMagic && version == kStateVersion;
}

void Synth::SetChannelMute(int channel, bool mute)
{
    if (channel >= 0 && channel < 16)
        _impl->core.SetChannelMute(channel, mute);
}

void Synth::Describe(SynthReport& out) const
{
    const Impl& d = *_impl;
    out = SynthReport{};
    d.core.Describe(out);
    out.bytesReceived = d.uart.Bytes();
    out.framingErrors = d.uart.FramingErrors();
    out.bytesDroppedBusy = d.droppedBusy;
    out.bytesDroppedQueueFull = d.droppedQueueFull;
    out.sysExOverflows = d.parser.SysExOverflows();
    out.sysExReceived = d.parser.SysExReceived();
    out.streamOverruns = d.overruns;
}

uint64_t Synth::InternalPosition() const
{
    return _impl->pos;
}

uint64_t Synth::HostTimeOfSample(uint64_t n) const
{
    return MulDivCeil(n, _impl->cfg.hostTickRate, kInternalRate);
}

} // namespace sam2695
