#include "multisoundslotcard.h"

#include <algorithm>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/soundmanager.h"

using namespace slots;

namespace
{

/// The five rows in display order (architecture.md §5)
constexpr struct
{
    AudioSourceType type;
    MultiSoundRow row;
    const char* name;
} kRows[] = {
    { AudioSourceType::MultiSoundFm, MultiSoundRow::Fm, "MS FM" },
    { AudioSourceType::MultiSoundSsg, MultiSoundRow::Ssg, "MS SSG" },
    { AudioSourceType::MultiSoundSaa, MultiSoundRow::Saa, "MS SAA" },
    { AudioSourceType::MultiSoundDac, MultiSoundRow::Dac, "MS DAC" },
    { AudioSourceType::MultiSoundMidi, MultiSoundRow::Midi, "MS MIDI" },
};

} // namespace

/// region <Construction>

std::unique_ptr<ICard> MultiSoundSlotCard::Create(const CardContext& context)
{
    return std::make_unique<MultiSoundSlotCard>(context, ConfigFrom(context));
}

MultiSoundOptions MultiSoundSlotCard::OptionsFrom(const CardDef& def, const CardOptions& options)
{
    MultiSoundOptions out;
    const uint32_t dip = OptionBits(def, options, Opt::Dip);
    out.ym = (dip & Bit(0)) != 0;
    out.saa = (dip & Bit(1)) != 0;
    out.gs = (dip & Bit(2)) != 0;
    out.sd = (dip & Bit(3)) != 0;
    out.gsRam = (OptionBits(def, options, Opt::GsRam) & Bit(1)) ? MultiSoundGsRam::TwoMb : MultiSoundGsRam::OneMb;
    out.ctrlMask =
        (OptionBits(def, options, Opt::CtrlMask) & Bit(1)) ? MultiSoundCtrlMask::Classic : MultiSoundCtrlMask::Pro;
    return out;
}

MultiSoundCardConfig MultiSoundSlotCard::ConfigFrom(const CardContext& context)
{
    MultiSoundCardConfig config;
    config.options = OptionsFrom(*context.def, context.options);
    // The emulator's audio axis: every sound device and the mixer's per-frame sample count run at CPU_CLOCK_RATE
    // T-states per second (turbo descaled by AudioTstate)
    config.hostTickRate = static_cast<uint32_t>(CPU_CLOCK_RATE);
    EmulatorContext* emulator = context.emulator;
    if (emulator != nullptr && emulator->pSoundManager != nullptr)
    {
        config.outputRate = static_cast<uint32_t>(emulator->pSoundManager->getCoreRate());
    }
    if (emulator != nullptr && !emulator->config.midiBank.empty())
    {
        config.midiBankPath = emulator->config.midiBank;
    }
    return config;
}

MultiSoundSlotCard::MultiSoundSlotCard(const CardContext& context, const MultiSoundCardConfig& config)
    : ICard(context), _context(context.emulator), _card(context.emulator, config)
{
}

/// endregion </Construction>

/// region <Time>

uint64_t MultiSoundSlotCard::FrameDuration() const
{
    if (_context == nullptr)
    {
        return 0;
    }
    return static_cast<uint64_t>(_context->config.frame) * _context->emulatorState.HostSpeedMultiplier();
}

uint64_t MultiSoundSlotCard::Now()
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

void MultiSoundSlotCard::TrackM1()
{
    Z80* z80 = (_context != nullptr && _context->pCore != nullptr) ? _context->pCore->GetZ80() : nullptr;
    if (z80 != nullptr)
    {
        _card.M1(z80->m1_pc);
    }
}

/// endregion </Time>

/// region <Bus>

uint8_t MultiSoundSlotCard::portDeviceInMethod(uint16_t port)
{
    bool drives = false;
    return portDeviceReadCycle(port, drives);
}

void MultiSoundSlotCard::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    TrackM1();
    _card.Out(port, value, Now());
}

uint8_t MultiSoundSlotCard::portDeviceReadCycle(uint16_t port, bool& drives)
{
    TrackM1();
    return _card.In(port, Now(), drives);
}

uint8_t MultiSoundSlotCard::Peek(uint16_t port, bool& drives) const
{
    return _card.Peek(port, drives);
}

void MultiSoundSlotCard::BusReset()
{
    _card.BusReset(Now());
}

/// endregion </Bus>

/// region <Frames>

void MultiSoundSlotCard::FrameStart()
{
    _last = std::max(_last, _origin);
    _card.FrameStart(_origin, FrameDuration());
}

void MultiSoundSlotCard::FrameEnd(size_t samples)
{
    // The machine's T-states are already rebased when the frame ends: the frame's nominal end on the card axis,
    // with the multiplier this frame ran with
    const uint64_t duration = FrameDuration();
    _card.FrameEnd(_origin + duration, samples);
    _origin += duration;
}

void MultiSoundSlotCard::SetOutputRate(uint32_t rate)
{
    _card.SetOutputRate(rate);
}

void MultiSoundSlotCard::MixerRows(std::vector<CardMixerRow>& out) const
{
    for (const auto& row : kRows)
    {
        out.push_back({ row.type, row.name });
    }
}

const int16_t* MultiSoundSlotCard::MixerBuffer(AudioSourceType type) const
{
    for (const auto& row : kRows)
    {
        if (row.type == type)
        {
            return _card.Row(row.row);
        }
    }
    return nullptr;
}

/// endregion </Frames>
