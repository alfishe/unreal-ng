// libsam2695 - the chip core.
#include "synthcore.h"

#include <algorithm>
#include <tuple>

namespace sam2695
{

uint32_t PolyphonyForEffectsWord(uint8_t word)
{
    // Datasheet §5 (p.34-35). Bit costs: REV (bit 5) 13, CHR (bit 4) 3, OM spatial (bit 3) 1, MIC
    // (bit 2) 1, ECH (bit 6) 3, EQ (bits 1:0) 10b = 2-band 4, 11b = 4-band 8. "In some configurations,
    // polyphony is decreased by 1 for reason of internal mixing": the rule that reproduces all 23 rows
    // of the p.35 table is one extra voice whenever an effect is on, except with mike echo on and
    // except reverb + chorus alone (30h). 45h is "reset all" and restores the power-up word.
    if (word == kResetAllEffectsWord)
        word = kPowerUpEffectsWord;
    word &= 0x7F;
    uint32_t cost = 0;
    if (word & 0x20)
        cost += 13;
    if (word & 0x10)
        cost += 3;
    if (word & 0x08)
        cost += 1;
    if (word & 0x04)
        cost += 1;
    if (word & 0x40)
        cost += 3;
    if ((word & 0x03) == 0x02)
        cost += 4;
    else if ((word & 0x03) == 0x03)
        cost += 8;
    const bool anyEffect = cost > 0;
    const bool mixingVoice = anyEffect && !(word & 0x40) && word != 0x30;
    return kMaxPolyphony - cost - (mixingVoice ? 1u : 0u);
}

void SynthCore::PowerOn()
{
    for (int i = 0; i < 16; i++)
    {
        _channels[i].PowerOn(i);
        ResolvePreset(_channels[i]);
    }
    for (Voice& v : _voices)
        v = Voice{};
    _effectsWord = kPowerUpEffectsWord;
    _voicesStolen = 0;
    _notesDropped = 0;
}

uint32_t SynthCore::PolyphonyLimit() const
{
    if (_cfg.polyphony != 0)
        return std::min<uint32_t>(_cfg.polyphony, kMaxPolyphony);
    return PolyphonyForEffectsWord(_effectsWord);
}

void SynthCore::ResolvePreset(Channel& ch)
{
    ch.preset = -1;
    if (_bank == nullptr)
        return;
    // Rhythm parts play SF2 bank 128 (drum sets by program, bank select ignored: datasheet p.26).
    // Melodic parts: bank select MSB is the variation (127 = MT-32 set, p.31); a missing variation
    // falls back to the capital tone in bank 0 as GS does.
    const Preset* p = nullptr;
    if (ch.rhythm)
    {
        p = _bank->FindPreset(128, ch.program);
        if (p == nullptr)
            p = _bank->FindPreset(128, 0);
    }
    else
    {
        p = _bank->FindPreset(ch.bankMsb, ch.program);
        if (p == nullptr)
            p = _bank->FindPreset(0, ch.program);
    }
    if (p != nullptr)
        ch.preset = static_cast<int32_t>(p - _bank->Model().presets.data());
}

PitchContext SynthCore::PitchFor(const Channel& ch) const
{
    PitchContext pc;
    pc.channelCents = (static_cast<float>(ch.fineTune) - 8192.0f) * (100.0f / 8192.0f) +
                      100.0f * static_cast<float>(ch.coarseTuneSemitones);
    return pc;
}

void SynthCore::Retarget(Voice& v, uint32_t offset, bool first)
{
    const Channel& ch = _channels[v.channel];
    v.Control(*Model(), ch, PitchFor(ch), kControlBlock - std::min(offset, kControlBlock), first);
}

void SynthCore::BeginBlock()
{
    if (Model() == nullptr)
        return;
    for (Voice& v : _voices)
        if (v.state != VoiceState::Off && v.state != VoiceState::Dying)
            Retarget(v, 0, false);
}

void SynthCore::RenderSegment(uint32_t from, uint32_t to, float* left, float* right)
{
    if (Model() == nullptr || to <= from)
        return;
    for (Voice& v : _voices)
        if (v.state != VoiceState::Off)
            v.Render(*Model(), _cfg.interpolation, left + from, right + from, to - from, _mute[v.channel]);
}

bool SynthCore::Message(const MidiMessage& m, uint32_t offset)
{
    if (m.status == 0xFF)
    {
        // MIDI Reset (datasheet p.26): back to the power-up condition
        for (Voice& v : _voices)
            v.Kill();
        for (int i = 0; i < 16; i++)
        {
            _channels[i].PowerOn(i);
            ResolvePreset(_channels[i]);
        }
        _effectsWord = kPowerUpEffectsWord;
        return false;
    }
    if (m.status >= 0xF0)
        return false; // SysEx and the remaining system messages: SAM-3
    const uint8_t chIndex = m.status & 0x0F;
    Channel& ch = _channels[chIndex];
    switch (m.status & 0xF0)
    {
        case 0x80:
            NoteOff(chIndex, m.data1, offset);
            break;
        case 0x90:
            if (m.data2 == 0)
                NoteOff(chIndex, m.data1, offset);
            else
                NoteOn(chIndex, m.data1, m.data2, offset);
            break;
        case 0xA0:
            ch.polyPressure[m.data1] = m.data2;
            ch.modulationVersion++;
            break;
        case 0xB0:
            if (m.data1 == 6 && ch.nrpnSelected && ch.nrpn == 0x375F)
            {
                ch.cc[6] = m.data2;
                return Nrpn(chIndex, ch.nrpn, m.data2);
            }
            ControlChange(chIndex, m.data1, m.data2, offset);
            break;
        case 0xC0:
            ch.program = m.data1;
            ch.bankMsb = ch.cc[0];
            ResolvePreset(ch);
            break;
        case 0xD0:
            ch.channelPressure = m.data1;
            ch.modulationVersion++;
            break;
        case 0xE0:
            ch.pitchBend = static_cast<uint16_t>(m.data1 | (m.data2 << 7));
            ch.modulationVersion++;
            break;
        default:
            break;
    }
    return false;
}

void SynthCore::ControlChange(uint8_t chIndex, uint8_t cc, uint8_t value, uint32_t offset)
{
    Channel& ch = _channels[chIndex];
    ch.cc[cc] = value;
    switch (cc)
    {
        case 0:
        case 32:
            return; // bank select takes effect at the next Program Change
        case 6:
            DataEntry(chIndex, true, value);
            return;
        case 38:
            DataEntry(chIndex, false, value);
            return;
        case 98:
            ch.nrpn = static_cast<uint16_t>((ch.nrpn & 0xFF00) | value);
            ch.nrpnSelected = true;
            return;
        case 99:
            ch.nrpn = static_cast<uint16_t>((value << 8) | (ch.nrpn & 0x00FF));
            ch.nrpnSelected = true;
            return;
        case 100:
            ch.rpn = static_cast<uint16_t>((ch.rpn & 0xFF00) | value);
            ch.nrpnSelected = false;
            return;
        case 101:
            ch.rpn = static_cast<uint16_t>((value << 8) | (ch.rpn & 0x00FF));
            ch.nrpnSelected = false;
            return;
        case 64:
            if (value < 64)
                for (Voice& v : _voices)
                    if (v.channel == chIndex && v.state == VoiceState::Sustained)
                    {
                        v.Release();
                        Retarget(v, offset, false);
                    }
            return;
        case 120: // All Sound Off: abrupt stop
            for (Voice& v : _voices)
                if (v.channel == chIndex)
                    v.Kill();
            return;
        case 121: // Reset All Controllers
        {
            ch.ResetControllers();
            for (Voice& v : _voices)
                if (v.channel == chIndex && v.state == VoiceState::Sustained)
                {
                    v.Release();
                    Retarget(v, offset, false);
                }
            return;
        }
        case 123: // All Notes Off
        case 124: // Omni Off, Omni On, Mono On, Poly On: imply All Notes Off
        case 125:
        case 126:
        case 127:
            if (cc == 126)
                ch.mono = true;
            else if (cc == 127)
                ch.mono = false;
            for (Voice& v : _voices)
                if (v.channel == chIndex && v.state == VoiceState::On)
                {
                    v.NoteOff(ch.Sustain());
                    if (v.state == VoiceState::Released)
                        Retarget(v, offset, false);
                }
            return;
        default:
            ch.modulationVersion++; // any other controller may feed a modulator
            return;
    }
}

void SynthCore::DataEntry(uint8_t chIndex, bool msb, uint8_t value)
{
    Channel& ch = _channels[chIndex];
    if (ch.nrpnSelected)
    {
        if (msb && ch.nrpn != kNullParameter)
            Nrpn(chIndex, ch.nrpn, value);
        return;
    }
    switch (ch.rpn)
    {
        case 0x0000: // pitch bend sensitivity (datasheet p.27, default 2 semitones)
            if (msb)
                ch.bendRangeSemitones = value;
            else
                ch.bendRangeCents = std::min<uint8_t>(value, 99);
            break;
        case 0x0001: // fine tuning, 14-bit: MSB 00 = -100, 40h = 0, 7Fh = +100 cents (p.27)
            ch.fineTune = msb ? static_cast<uint16_t>((value << 7) | (ch.fineTune & 0x7F))
                              : static_cast<uint16_t>((ch.fineTune & 0x3F80) | value);
            break;
        case 0x0002: // coarse tuning in semitones around 40h
            if (msb)
                ch.coarseTuneSemitones = static_cast<int16_t>(static_cast<int>(value) - 64);
            break;
        default:
            return;
    }
    ch.modulationVersion++;
}

bool SynthCore::Nrpn(uint8_t chIndex, uint16_t number, uint8_t value)
{
    (void)chIndex;
    if (number == 0x375F)
    {
        if (value == kResetAllEffectsWord)
        {
            PowerOn(); // datasheet p.35: restores the power-up status and every MIDI parameter
            return true;
        }
        SetEffectsWord(value);
    }
    // the remaining chip NRPNs (37xxh effects, 01xxh / 18-1Exxh GS part and drum parameters): SAM-3
    return false;
}

void SynthCore::SetEffectsWord(uint8_t word)
{
    _effectsWord = word;
    EnforceLimit();
}

int SynthCore::ChooseVictim(uint64_t currentNote) const
{
    // Stealing order (README "Voice allocation"): voices in their release first, then voices held only
    // by the sustain pedal, then held keys - melodic before rhythm; inside a tier the quietest, then
    // the oldest. The voices of the note being started are never taken.
    int best = -1;
    std::tuple<int, int, float, uint64_t> bestKey{};
    for (size_t i = 0; i < _voices.size(); i++)
    {
        const Voice& v = _voices[i];
        if (!v.Counts() || v.noteId == currentNote)
            continue;
        const int tier = v.state == VoiceState::Released ? 0 : v.state == VoiceState::Sustained ? 1 : 2;
        const int protectedRhythm = (v.state == VoiceState::On && v.rhythm) ? 1 : 0;
        const auto key = std::make_tuple(tier, protectedRhythm, v.Loudness(), v.noteId);
        if (best < 0 || key < bestKey)
        {
            best = static_cast<int>(i);
            bestKey = key;
        }
    }
    return best;
}

void SynthCore::EnforceLimit()
{
    const uint32_t limit = PolyphonyLimit();
    for (;;)
    {
        uint32_t counted = 0;
        for (const Voice& v : _voices)
            counted += v.Counts() ? 1u : 0u;
        if (counted <= limit)
            return;
        const int victim = ChooseVictim(UINT64_MAX);
        if (victim < 0)
            return;
        _voices[victim].Kill();
        _voicesStolen++;
    }
}

Voice* SynthCore::Allocate(uint64_t currentNote)
{
    uint32_t counted = 0;
    for (const Voice& v : _voices)
        counted += v.Counts() ? 1u : 0u;
    if (counted >= PolyphonyLimit())
    {
        const int victim = ChooseVictim(currentNote);
        if (victim < 0)
            return nullptr;
        _voices[victim].Kill();
        _voicesStolen++;
    }
    for (Voice& v : _voices)
        if (v.state == VoiceState::Off)
            return &v;
    // every spare slot is fading: cut the one closest to silence
    Voice* cut = nullptr;
    for (Voice& v : _voices)
        if (v.state == VoiceState::Dying && (cut == nullptr || v.fadeLeft < cut->fadeLeft))
            cut = &v;
    if (cut != nullptr)
        cut->state = VoiceState::Off;
    return cut;
}

namespace
{

bool InRange(const Zone& z, const Zone* global, uint8_t key, uint8_t vel)
{
    const Zone* kz = z.IsSet(Gen::KeyRange) ? &z : (global != nullptr && global->IsSet(Gen::KeyRange) ? global : nullptr);
    const Zone* vz = z.IsSet(Gen::VelRange) ? &z : (global != nullptr && global->IsSet(Gen::VelRange) ? global : nullptr);
    if (kz != nullptr && (key < kz->keyLo || key > kz->keyHi))
        return false;
    if (vz != nullptr && (vel < vz->velLo || vel > vz->velHi))
        return false;
    return true;
}

} // namespace

void SynthCore::NoteOn(uint8_t chIndex, uint8_t key, uint8_t vel, uint32_t offset)
{
    Channel& ch = _channels[chIndex];
    const BankModel* model = Model();
    if (model == nullptr || ch.preset < 0)
        return;
    // a key struck again releases its previous note; mono mode releases every note of the channel
    for (Voice& v : _voices)
        if (v.channel == chIndex && (v.key == key || ch.mono) &&
            (v.state == VoiceState::On || v.state == VoiceState::Sustained))
        {
            v.Release();
            Retarget(v, offset, false);
        }

    const uint64_t noteId = ++_noteCounter;
    const Preset& preset = model->presets[ch.preset];
    const Zone* pg = preset.globalZone >= 0 ? &model->zones[preset.globalZone] : nullptr;

    struct Pair
    {
        VoiceZones zones;
        int32_t sample;
        uint8_t exclusiveClass;
    };
    std::array<Pair, kMaxPolyphony> pairs{};
    size_t pairCount = 0;
    for (uint32_t pi = 0; pi < preset.zoneCount && pairCount < pairs.size(); pi++)
    {
        const Zone& pz = model->zones[preset.zoneFirst + pi];
        if (!InRange(pz, pg, key, vel) || pz.link < 0)
            continue;
        const Instrument& inst = model->instruments[pz.link];
        const Zone* ig = inst.globalZone >= 0 ? &model->zones[inst.globalZone] : nullptr;
        for (uint32_t ii = 0; ii < inst.zoneCount && pairCount < pairs.size(); ii++)
        {
            const Zone& iz = model->zones[inst.zoneFirst + ii];
            if (!InRange(iz, ig, key, vel) || iz.link < 0)
                continue;
            Pair& p = pairs[pairCount++];
            p.zones = VoiceZones{pg, &pz, ig, &iz};
            p.sample = iz.link;
            const int16_t cls = iz.IsSet(Gen::ExclusiveClass)   ? iz.gens[static_cast<int>(Gen::ExclusiveClass)]
                                : (ig != nullptr && ig->IsSet(Gen::ExclusiveClass)) ? ig->gens[static_cast<int>(Gen::ExclusiveClass)]
                                                                                    : 0;
            p.exclusiveClass = static_cast<uint8_t>(std::clamp<int>(cls, 0, 127));
        }
    }

    // Exclusive classes (SF2 generator 57): a new note cuts every sounding voice of the same class on
    // the channel (the hi-hat pedal closes the open hi-hat)
    for (size_t i = 0; i < pairCount; i++)
    {
        if (pairs[i].exclusiveClass == 0)
            continue;
        for (Voice& v : _voices)
            if (v.channel == chIndex && v.exclusiveClass == pairs[i].exclusiveClass && v.noteId != noteId &&
                v.state != VoiceState::Off)
                v.Kill();
    }

    for (size_t i = 0; i < pairCount; i++)
    {
        Voice* v = Allocate(noteId);
        if (v == nullptr)
        {
            _notesDropped++;
            continue;
        }
        if (!v->Start(*model, pairs[i].zones, pairs[i].sample, ch, chIndex, key, vel, noteId))
        {
            v->state = VoiceState::Off;
            continue;
        }
        Retarget(*v, offset, true);
    }
}

void SynthCore::NoteOff(uint8_t chIndex, uint8_t key, uint32_t offset)
{
    const bool pedal = _channels[chIndex].Sustain();
    for (Voice& v : _voices)
        if (v.channel == chIndex && v.key == key && v.state == VoiceState::On)
        {
            v.NoteOff(pedal);
            if (v.state == VoiceState::Released)
                Retarget(v, offset, false);
        }
}

void SynthCore::Describe(SynthReport& out) const
{
    out.polyphonyLimit = PolyphonyLimit();
    out.effectsWord = _effectsWord;
    out.voicesStolen = _voicesStolen;
    out.notesDropped = _notesDropped;
    out.activeVoices = 0;
    out.fadingVoices = 0;
    for (int i = 0; i < 16; i++)
    {
        const Channel& ch = _channels[i];
        SynthReport::ChannelView& cv = out.channels[i];
        cv.program = ch.program;
        cv.bankMsb = ch.bankMsb;
        cv.rhythm = ch.rhythm;
        cv.preset = ch.preset;
        cv.volume = ch.cc[7];
        cv.pan = ch.cc[10];
        cv.expression = ch.cc[11];
        cv.pitchBend = ch.pitchBend;
        cv.activeVoices = 0;
        cv.muted = _mute[i];
    }
    for (const Voice& v : _voices)
    {
        if (v.Counts())
        {
            out.activeVoices++;
            out.channels[v.channel].activeVoices++;
        }
        else if (v.state == VoiceState::Dying)
            out.fadingVoices++;
    }
}

void SynthCore::Sanitize()
{
    const BankModel* model = Model();
    for (Channel& ch : _channels)
        if (model == nullptr || ch.preset >= static_cast<int32_t>(model->presets.size()) || ch.preset < -1)
            ch.preset = -1;
    for (Voice& v : _voices)
    {
        if (model == nullptr)
            v.state = VoiceState::Off;
        else
            v.Sanitize(*model);
        v.channel &= 15;
    }
}

} // namespace sam2695
