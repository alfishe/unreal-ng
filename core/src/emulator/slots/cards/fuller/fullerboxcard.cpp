#include "fullerboxcard.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"

namespace
{
/// One generator tick (the AY clock / 8) on the card axis (CPU_CLOCK_RATE per second)
constexpr double kTickTime = 8.0 * static_cast<double>(CPU_CLOCK_RATE) / FullerBoxCard::kAyClockHz;

void PutU64(uint8_t*& p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint64_t GetU64(const uint8_t*& p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= static_cast<uint64_t>(*p++) << (8 * i);
    return v;
}
} // namespace

/// The card's blob: u1 version, u8 origin, u8 last time, f8 next tick (IEEE bits), u8 the mixer's frame-start sample
/// phase, then the AY (SoundChip_AY8910's 73 bytes)
class FullerBoxTtd final : public ttd::TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;
    static constexpr size_t kChipOffset = 1 + 8 + 8 + 8 + 8;

    explicit FullerBoxTtd(FullerBoxCard& card) : _card(card)
    {
    }

    size_t TTDStateSize() const override
    {
        return kChipOffset + _card.Chip().TTDStateSize();
    }
    void TTDSaveState(uint8_t* dst) const override
    {
        uint8_t* p = dst;
        *p++ = kVersion;
        PutU64(p, _card.Origin());
        PutU64(p, _card.LastTime());
        uint64_t tick = 0;
        const double tickPos = _card.TickPosition();
        std::memcpy(&tick, &tickPos, sizeof(tick));
        PutU64(p, tick);
        PutU64(p, _card.MixerSamplePhase());
        _card.Chip().TTDSaveState(p);
    }
    void TTDLoadState(const uint8_t* src) override
    {
        const uint8_t* p = src;
        if (*p++ != kVersion)
            return;
        const uint64_t origin = GetU64(p);
        const uint64_t last = GetU64(p);
        const uint64_t tick = GetU64(p);
        double tickPos = 0;
        std::memcpy(&tickPos, &tick, sizeof(tickPos));
        const uint64_t samplePhase = GetU64(p);
        _card.Chip().TTDLoadState(p);
        _card.RestoreTime(origin, last, tickPos);
        // The frames after the restore must get the sample counts they were recorded with (MultiSoundCardTtd)
        _card.AdoptMixerSamplePhase(samplePhase);
    }
    std::string TTDDeviceName() const override
    {
        return "FullerBox";
    }
    ttd::TTDDeviceType TTDType() const override
    {
        return ttd::TTDDeviceType::FullerBox;
    }
    uint64_t TTDHashState() const override
    {
        std::vector<uint8_t> blob(TTDStateSize());
        TTDSaveState(blob.data());
        uint64_t hash = 0xcbf29ce484222325ull;
        for (uint8_t b : blob)
        {
            hash ^= b;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

private:
    FullerBoxCard& _card;
};

/// region <Construction>

std::unique_ptr<ICard> FullerBoxCard::Create(const CardContext& context)
{
    return std::make_unique<FullerBoxCard>(context);
}

FullerBoxCard::FullerBoxCard(const CardContext& context)
    : ICard(context), _context(context.emulator), _chip(std::make_unique<SoundChip_AY8910>(context.emulator)),
      _ttd(std::make_unique<FullerBoxTtd>(*this))
{
    _chip->setChipModel(AYChipModel::AY8910);
    _chip->setStereoMode(AYStereoMode::Mono);
    _chip->setGeneratorRate(kAyClockHz / 8.0);
}

FullerBoxCard::~FullerBoxCard() = default;

uint8_t FullerBoxCard::StickByte(uint16_t controls)
{
    uint8_t pressed = 0;
    if (controls & controllers::kUp)
        pressed |= 0x01;
    if (controls & controllers::kDown)
        pressed |= 0x02;
    if (controls & controllers::kLeft)
        pressed |= 0x04;
    if (controls & controllers::kRight)
        pressed |= 0x08;
    if (controls & controllers::kFire)
        pressed |= 0x80;
    return static_cast<uint8_t>(~pressed);
}

/// endregion </Construction>

/// region <Time>

uint64_t FullerBoxCard::FrameDuration() const
{
    if (_context == nullptr)
        return 0;
    return static_cast<uint64_t>(_context->config.frame) * _context->emulatorState.HostSpeedMultiplier();
}

uint64_t FullerBoxCard::Now()
{
    uint64_t now = _origin;
    Z80* z80 = (_context != nullptr && _context->pCore != nullptr) ? _context->pCore->GetZ80() : nullptr;
    if (z80 != nullptr)
    {
        const uint32_t frameT = _context->emulatorState.AudioTstate(z80->t);
        now += static_cast<uint64_t>(frameT) * _context->emulatorState.HostSpeedMultiplier();
    }
    _last = std::max(_last, now);
    return _last;
}

void FullerBoxCard::RestoreTime(uint64_t origin, uint64_t last, double tickPos)
{
    _origin = origin;
    _last = last;
    _tickPos = tickPos;
    _ticks.clear();
}

uint64_t FullerBoxCard::MixerSamplePhase() const
{
    return (_context != nullptr && _context->pSoundManager != nullptr) ? _context->pSoundManager->samplePhase() : 0;
}

void FullerBoxCard::AdoptMixerSamplePhase(uint64_t phase)
{
    if (_context != nullptr && _context->pSoundManager != nullptr)
        _context->pSoundManager->adoptSamplePhase(phase);
}

void FullerBoxCard::AdvanceTo(uint64_t t)
{
    const double until = static_cast<double>(t);
    while (_tickPos < until)
    {
        _chip->updateState(true);
        _ticks.push_back(static_cast<float>(_chip->mixedLeft()));
        _tickPos += kTickTime;
    }
}

/// endregion </Time>

/// region <Bus>

uint8_t FullerBoxCard::portDeviceInMethod(uint16_t port)
{
    bool drives = false;
    return portDeviceReadCycle(port, drives);
}

void FullerBoxCard::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    // A7-A5 tell the functions apart (zxsp); the claims decide the rest of the decode
    switch (port & 0x00E0)
    {
        case kPortSelect & 0xE0:
            _chip->setRegister(value);
            break;
        case kPortData & 0xE0:
            AdvanceTo(Now());  // the generators run with the old value up to the write
            _chip->writeCurrentRegister(value);
            break;
        default:
            break;
    }
}

uint8_t FullerBoxCard::portDeviceReadCycle(uint16_t port, bool& drives)
{
    return Peek(port, drives);
}

uint8_t FullerBoxCard::Peek(uint16_t port, bool& drives) const
{
    drives = true;
    switch (port & 0x00E0)
    {
        case kPortSelect & 0xE0:
            return _chip->readRegisterOnBus(_chip->getCurrentRegisterIndex());
        case kPortStick & 0xE0:
            return StickByte(_context && _context->pControllerManager ? _context->pControllerManager->OwnedState(this)
                                                                       : 0);
        default:
            drives = false;
            return 0xFF;
    }
}

void FullerBoxCard::BusReset()
{
    AdvanceTo(Now());
    _chip->reset();
}

/// endregion </Bus>

/// region <Frames>

void FullerBoxCard::FrameStart()
{
    _last = std::max(_last, _origin);
    _ticks.clear();
}

void FullerBoxCard::FrameEnd(size_t samples)
{
    // The machine's T-states are already rebased when the frame ends: the frame's nominal end on the card axis
    const uint64_t end = _origin + FrameDuration();
    AdvanceTo(end);
    _origin = end;
    samples = std::min<size_t>(samples, MAX_SAMPLES_PER_FRAME);
    const size_t n = _ticks.size();
    for (size_t k = 0; k < samples; k++)
    {
        const size_t from = n * k / samples;
        const size_t to = std::max(from + 1, n * (k + 1) / samples);
        double level = 0;
        size_t count = 0;
        for (size_t i = from; i < to && i < n; i++, count++)
            level += _ticks[i];
        if (count)
            level /= static_cast<double>(count);
        const auto sample = static_cast<int16_t>(std::clamp(level * 32767.0, -32768.0, 32767.0));
        _buffer[k * 2] = sample;
        _buffer[k * 2 + 1] = sample;
    }
    std::fill(_buffer + samples * 2, _buffer + MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS, int16_t{0});
    _ticks.clear();
}

void FullerBoxCard::SetOutputRate(uint32_t rate)
{
    (void)rate;  // the frame end gets its sample count from the mixer
}

void FullerBoxCard::MixerRows(std::vector<CardMixerRow>& out) const
{
    // The third AY row of the mixer: no machine has a third AY in its socket
    out.push_back({AudioSourceType::AY3_All, "Fuller AY", true});
}

const int16_t* FullerBoxCard::MixerBuffer(AudioSourceType type) const
{
    return type == AudioSourceType::AY3_All ? _buffer : nullptr;
}

int16_t* FullerBoxCard::SsgMixerBuffer(AudioSourceType type)
{
    return type == AudioSourceType::AY3_All ? _buffer : nullptr;
}

void FullerBoxCard::MixerChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const
{
    if (type != AudioSourceType::AY3_All)
        return;
    out.push_back({"a", "A", true, false});
    out.push_back({"b", "B", true, false});
    out.push_back({"c", "C", true, false});
}

void FullerBoxCard::SetMixerChannel(AudioSourceType type, size_t index, bool muted, float volume)
{
    if (type != AudioSourceType::AY3_All || index >= 3)
        return;
    _chip->setChannelMuted(static_cast<uint8_t>(index), muted);
    _chip->setChannelVolume(static_cast<uint8_t>(index), volume);
}

/// endregion </Frames>

void FullerBoxCard::Connectors(std::vector<CardConnector>& out) const
{
    out.push_back({"fuller", controllers::ConnectorKind::Fuller, controllers::RetroDeviceKind::FullerJoystick});
}

void FullerBoxCard::CollectTtdDevices(std::vector<CardTtdDevice>& out)
{
    out.push_back({_ttd.get(), SlotId() + "." + kCardId, nullptr});
}
