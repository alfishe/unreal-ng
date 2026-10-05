#include "multisoundslotcard.h"

#include <algorithm>
#include <cstdio>

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/config.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/soundmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "sam2695/sam2695.h"

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
    const Config* loader = emulator != nullptr && emulator->pEmulator != nullptr ? emulator->pEmulator->GetConfigLoader()
                                                                                : nullptr;
    if (loader != nullptr && StringHelper::ToLower(loader->GetMidiBank()) == "none")
    {
        config.midiBankPath.clear();   // [MIDI] Bank=NONE: no bank, the synthesizer stays silent
    }
    else if (loader != nullptr && !loader->GetMidiBank().empty())
    {
        config.midiBankPath = loader->GetMidiBank();
    }
    return config;
}

MultiSoundSlotCard::MultiSoundSlotCard(const CardContext& context, const MultiSoundCardConfig& config)
    : ICard(context), _context(context.emulator), _card(context.emulator, config),
      _cardTtd(std::make_unique<MultiSoundCardTtd>(*this)), _synthTtd(std::make_unique<Sam2695Ttd>(_card.Synth()))
{
}

MultiSoundSlotCard::~MultiSoundSlotCard() = default;

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

uint64_t MultiSoundSlotCard::Position() const
{
    uint64_t now = _origin;
    const Z80* z80 = (_context != nullptr && _context->pCore != nullptr) ? _context->pCore->GetZ80() : nullptr;
    if (z80 != nullptr)
    {
        const uint32_t frameT = _context->emulatorState.AudioTstate(z80->t);
        now += static_cast<uint64_t>(frameT) * _context->emulatorState.HostSpeedMultiplier();
    }
    return std::max(_last, now);
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

bool MultiSoundSlotCard::MidiPanic()
{
    _card.MidiPanic(Now());
    return true;
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

/// region <Time travel>

void MultiSoundSlotCard::CollectTtdDevices(std::vector<CardTtdDevice>& out)
{
    const std::string base = SlotId() + "." + MultiSoundCard::kCardId;
    out.push_back({ ttd::PeripheralId::MultiSound, _cardTtd.get(), base, nullptr });
    out.push_back({ ttd::PeripheralId::Saa1099, &_card.Saa(), base + ".saa1099", nullptr });
    out.push_back({ ttd::PeripheralId::Sam2695, _synthTtd.get(), base + ".sam2695", nullptr });
    SoundChip_GeneralSound& gs = _card.Gs();
    out.push_back({ gs.TTDPeripheralId(), &gs, base + ".gs", &gs });
}

void MultiSoundSlotCard::TtdFingerprint(std::vector<std::pair<std::string, uint64_t>>& out) const
{
    out.emplace_back("slots." + SlotId() + ".bank", Sam2695Ttd::BankFingerprint(_card.Synth()));
}

bool MultiSoundSlotCard::TtdSessionMatches(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
                                           std::string& why) const
{
    const auto id = static_cast<uint8_t>(ttd::PeripheralId::Sam2695);
    const auto found = blobs.find(id);
    if (found == blobs.end() || found->second.empty())
        return true;   // a session without the card: the slot-set guard names that
    const std::vector<uint8_t> state = ttd::TTDPeripheralRegistry::DecodeBlob(id, found->second);
    sam2695::BankDigest recorded{};
    if (!sam2695::Synth::StateBank(state.data(), state.size(), recorded))
    {
        why = SlotId() + ": the session's MIDI synthesizer state has another layout";
        return false;
    }
    const sam2695::ISoundBank* bank = _card.Synth().Bank();
    const sam2695::BankDigest live = bank != nullptr ? bank->Digest() : sam2695::BankDigest{};
    if (recorded == live)
        return true;
    auto shortHex = [](const sam2695::BankDigest& d) {
        char text[17] = {};
        for (int i = 0; i < 8; i++)
            std::snprintf(text + i * 2, 3, "%02x", d[static_cast<size_t>(i)]);
        return std::string(text);
    };
    why = SlotId() + ": MIDI bank differs from the recording (recorded SHA-256 " + shortHex(recorded) +
          "..., this machine " + (bank != nullptr ? shortHex(live) + "..." : std::string("no bank")) +
          ") - set [MIDI] Bank= to the recorded bank and restart";
    return false;
}

/// endregion </Time travel>

