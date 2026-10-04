// libsam2695 - the chip core.
#include "synthcore.h"

#include <algorithm>
#include <cmath>
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

void SynthCore::Configure(const SynthConfig& cfg)
{
    _cfg = cfg;
    _fx.Allocate();
}

void SynthCore::PowerOn()
{
    for (Voice& v : _voices)
        v = Voice{};
    ResetAll(false);
    _voicesStolen = 0;
    _notesDropped = 0;
}

void SynthCore::ResetAll(bool fadeVoices)
{
    // the power-up condition: parts, chip-level MIDI state, effects word and effect settings
    if (fadeVoices)
        for (Voice& v : _voices)
            v.Kill();
    for (int i = 0; i < 16; i++)
    {
        _channels[i].PowerOn(i);
        ResolvePreset(_channels[i]);
    }
    _rxNrpn.fill(kNullParameter);
    _rxNrpnSelected.fill(false);
    _masterTune = {0x00, 0x04, 0x00, 0x00};
    _keyShift = 0x40;
    _deviceId = 0x20;
    for (DrumTable& t : _drums)
        t.fill(DrumNote{});
    _effectsWord = kPowerUpEffectsWord;
    _fx.PowerOn();
}

void SynthCore::GsReset()
{
    // GM System On / GS reset: every part and the GS system parameters back to their defaults, sound
    // stops. The chip's own settings (NRPN 37xxh: effects word, equalizer, routing, spatial, clipping,
    // master and GM volume) are not GM / GS parameters and stay (the DreamBlaster X16 specification:
    // "master volume not reset by GS reset").
    for (Voice& v : _voices)
        v.Kill();
    for (int i = 0; i < 16; i++)
    {
        _channels[i].PowerOn(i);
        ResolvePreset(_channels[i]);
    }
    _rxNrpn.fill(kNullParameter);
    _rxNrpnSelected.fill(false);
    _masterTune = {0x00, 0x04, 0x00, 0x00};
    _keyShift = 0x40;
    _fx.params.gmPan = 0x40;
    for (DrumTable& t : _drums)
        t.fill(DrumNote{});
    _fx.ResetReverbChorus();
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

float SynthCore::MasterTuneCents() const
{
    // GS master tune, four nibbles: 0018h .. 0400h .. 07E8h = -100.0 .. 0 .. +100.0 cents (p.27)
    const int v = (_masterTune[0] & 15) << 12 | (_masterTune[1] & 15) << 8 | (_masterTune[2] & 15) << 4 |
                  (_masterTune[3] & 15);
    return std::clamp(static_cast<float>(v - 0x400) / 10.0f, -100.0f, 100.0f);
}

VoiceContext SynthCore::ContextFor(const Channel& ch) const
{
    VoiceContext c;
    c.tuneCents = (static_cast<float>(ch.fineTune) - 8192.0f) * (100.0f / 8192.0f) +
                  100.0f * static_cast<float>(ch.coarseTuneSemitones) + MasterTuneCents();
    c.reverbScale = _fx.params.reverbSend / 64.0f;
    c.chorusScale = _fx.params.chorusSend / 64.0f;
    if (ch.rhythm)
        c.drums = &_drums[ch.rxChannel == 9 ? 0 : 1];
    return c;
}

void SynthCore::TouchAllParts()
{
    for (Channel& ch : _channels)
        ch.modulationVersion++;
}

void SynthCore::Retarget(Voice& v, uint32_t offset, bool first)
{
    const Channel& ch = _channels[v.channel];
    v.Control(*Model(), ch, ContextFor(ch), kControlBlock - std::min(offset, kControlBlock), first);
}

void SynthCore::BeginBlock()
{
    if (Model() == nullptr)
        return;
    for (Voice& v : _voices)
        if (v.state != VoiceState::Off && v.state != VoiceState::Dying)
            Retarget(v, 0, false);
}

void SynthCore::RenderSegment(uint32_t from, uint32_t to, FxBuses& b)
{
    if (Model() == nullptr || to <= from)
        return;
    for (Voice& v : _voices)
        if (v.state != VoiceState::Off)
            v.Render(*Model(), _cfg.interpolation, b.left + from, b.right + from, b.reverb + from, b.chorus + from,
                     to - from, _mute[v.channel]);
}

void SynthCore::FinishBlock(FxBuses& buses, float* outL, float* outR)
{
    _fx.Process(buses, kControlBlock, _effectsWord, _cfg.effects, _cfg.outputGain, outL, outR);
}

bool SynthCore::Message(const MidiMessage& m, uint32_t offset)
{
    if (m.status == 0xFF)
    {
        ResetAll(true); // MIDI Reset (datasheet p.26): back to the power-up condition
        return false;
    }
    if (m.status == 0xF0)
        return SysEx(m.sysEx, m.sysExLength);
    if (m.status >= 0xF0)
        return false; // the remaining system messages are not in the chip's chart
    const uint8_t channel = m.status & 0x0F;
    if ((m.status & 0xF0) == 0xB0)
    {
        // chip-level controllers, applied once per message whatever the part assignment
        switch (m.data1)
        {
            case 98:
                _rxNrpn[channel] = static_cast<uint16_t>((_rxNrpn[channel] & 0xFF00) | m.data2);
                _rxNrpnSelected[channel] = true;
                break;
            case 99:
                _rxNrpn[channel] = static_cast<uint16_t>((m.data2 << 8) | (_rxNrpn[channel] & 0x00FF));
                _rxNrpnSelected[channel] = true;
                break;
            case 100:
            case 101:
                _rxNrpnSelected[channel] = false;
                break;
            case 6:
                if (_rxNrpnSelected[channel] && (_rxNrpn[channel] >> 8) == 0x37)
                {
                    // the chip's special NRPNs (§2-1): accepted on any MIDI channel
                    if (ChipNrpn(static_cast<uint8_t>(_rxNrpn[channel] & 0xFF), m.data2))
                        return true;
                }
                break;
            case 80: // reverb program (datasheet p.26, "DREAM")
                if (m.data2 <= 7)
                {
                    _fx.params.SelectReverb(m.data2);
                    _fx.Changed();
                }
                break;
            case 81: // chorus program
                if (m.data2 <= 7)
                {
                    _fx.params.SelectChorus(m.data2);
                    _fx.Changed();
                }
                break;
            default:
                break;
        }
    }
    for (uint8_t p = 0; p < 16; p++)
        if (_channels[p].rxChannel == channel)
            PartMessage(p, m, offset);
    return false;
}

void SynthCore::PartMessage(uint8_t part, const MidiMessage& m, uint32_t offset)
{
    Channel& ch = _channels[part];
    switch (m.status & 0xF0)
    {
        case 0x80:
            NoteOff(part, m.data1, offset);
            break;
        case 0x90:
            if (m.data2 == 0)
                NoteOff(part, m.data1, offset);
            else
                NoteOn(part, m.data1, m.data2, offset);
            break;
        case 0xA0:
            ch.polyPressure[m.data1] = m.data2;
            ch.modulationVersion++;
            break;
        case 0xB0:
            ControlChange(part, m.data1, m.data2, offset);
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
}

void SynthCore::ReleaseHeld(uint8_t part, uint32_t offset)
{
    // pedal-held voices whose pedals are both up now go into their release
    const Channel& ch = _channels[part];
    for (Voice& v : _voices)
        if (v.channel == part && v.state == VoiceState::Sustained && !ch.Sustain() && !(v.sostenuto && ch.Sostenuto()))
        {
            v.Release();
            Retarget(v, offset, false);
        }
}

void SynthCore::ControlChange(uint8_t part, uint8_t cc, uint8_t value, uint32_t offset)
{
    Channel& ch = _channels[part];
    const uint8_t before = ch.cc[cc];
    ch.cc[cc] = value;
    switch (cc)
    {
        case 0:
        case 32:
            return; // bank select takes effect at the next Program Change; LSB (CC 32) is not used
        case 6:
            DataEntry(part, true, value);
            return;
        case 38:
            DataEntry(part, false, value);
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
        case 64: // sustain (damper)
            if (value < 64)
                ReleaseHeld(part, offset);
            return;
        case 66: // sostenuto: holds the notes sounding when it goes down, and only those
            if (value >= 64 && before < 64)
            {
                for (Voice& v : _voices)
                    if (v.channel == part && v.state == VoiceState::On)
                        v.sostenuto = true;
            }
            else if (value < 64 && before >= 64)
            {
                ReleaseHeld(part, offset);
                for (Voice& v : _voices)
                    if (v.channel == part)
                        v.sostenuto = false;
            }
            return;
        case 120: // All Sound Off: abrupt stop
            for (Voice& v : _voices)
                if (v.channel == part)
                    v.Kill();
            return;
        case 121: // Reset All Controllers (lifts the pedals)
            ch.ResetControllers();
            ReleaseHeld(part, offset);
            for (Voice& v : _voices)
                if (v.channel == part)
                    v.sostenuto = false;
            return;
        case 123: // All Notes Off
        case 124: // Omni Off, Omni On, Mono On, Poly On: imply All Notes Off (MIDI 1.0)
        case 125:
        case 126:
        case 127:
            if (cc == 126)
                ch.mono = true;
            else if (cc == 127)
                ch.mono = false;
            for (Voice& v : _voices)
                if (v.channel == part && v.state == VoiceState::On)
                {
                    v.NoteOff(ch.Sustain() || (v.sostenuto && ch.Sostenuto()));
                    if (v.state == VoiceState::Released)
                        Retarget(v, offset, false);
                }
            return;
        default:
            ch.modulationVersion++; // any other controller may feed a modulator or the controller matrix
            return;
    }
}

void SynthCore::DataEntry(uint8_t part, bool msb, uint8_t value)
{
    Channel& ch = _channels[part];
    if (ch.nrpnSelected)
    {
        if (msb && ch.nrpn != kNullParameter)
            PartNrpnWrite(part, ch.nrpn, value);
        return;
    }
    switch (ch.rpn)
    {
        case 0x0000: // pitch bend sensitivity (datasheet p.27, default 2 semitones)
            if (msb)
                ch.bendRangeSemitones = std::min<uint8_t>(value, 24);
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

void SynthCore::PartNrpnWrite(uint8_t part, uint16_t number, uint8_t value)
{
    Channel& ch = _channels[part];
    const uint8_t msb = static_cast<uint8_t>(number >> 8), lsb = static_cast<uint8_t>(number & 0x7F);
    if (msb == 0x01)
    {
        // GS part NRPNs (p.27): vibrato, TVF, envelope, relative to the sound (40h = no change)
        int index = -1;
        switch (lsb)
        {
            case 0x08: index = static_cast<int>(PartNrpn::VibratoRate); break;
            case 0x09: index = static_cast<int>(PartNrpn::VibratoDepth); break;
            case 0x0A: index = static_cast<int>(PartNrpn::VibratoDelay); break;
            case 0x20: index = static_cast<int>(PartNrpn::Cutoff); break;
            case 0x21: index = static_cast<int>(PartNrpn::Resonance); break;
            case 0x63: index = static_cast<int>(PartNrpn::Attack); break;
            case 0x64: index = static_cast<int>(PartNrpn::Decay); break;
            case 0x66: index = static_cast<int>(PartNrpn::Release); break;
            default: break;
        }
        if (index >= 0)
        {
            ch.partNrpn[index] = value;
            ch.modulationVersion++;
        }
        return;
    }
    if (msb >= 0x18 && msb <= 0x1E)
    {
        // drum instrument edits (p.27) of key lsb, in the table of the channel the message came on: one
        // for channel 10, one shared by the other channels (DreamBlaster X16 specification, note 6)
        DrumNote& d = _drums[ch.rxChannel == 9 ? 0 : 1][lsb];
        switch (msb)
        {
            case 0x18: d.pitch = value; break;
            case 0x1A: d.level = value; break;
            case 0x1C: d.pan = value; break;
            case 0x1D: d.reverb = value; break;
            case 0x1E: d.chorus = value; break;
            default: return; // 19h, 1Bh: not in the chip's chart
        }
        TouchAllParts();
    }
    // 37xxh is chip level (Message); anything else is not in the chart
}

bool SynthCore::ChipNrpn(uint8_t index, uint8_t value)
{
    FxParams& p = _fx.params;
    switch (index)
    {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            p.eqLevel[index] = value;
            break;
        case 0x07:
            p.masterVolume = value;
            break;
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
            p.eqFreq[index - 0x08] = value;
            break;
        case 0x13:
            p.clipMode = value;
            break;
        case 0x15:
            p.reverbSend = value;
            TouchAllParts();
            break;
        case 0x16:
            p.chorusSend = value;
            TouchAllParts();
            break;
        case 0x18:
            p.postGm = value;
            break;
        case 0x19:
            p.postMike = value; // stored; the mike input is not modeled
            break;
        case 0x1A:
            p.postFx = value;
            break;
        case 0x20:
            p.spatialVolume = value;
            break;
        case 0x22:
            p.gmVolume = value;
            break;
        case 0x23:
            p.gmPan = value;
            break;
        case 0x2C:
            p.spatialDelay = value;
            break;
        case 0x2D:
            p.spatialInput = value;
            break;
        case 0x57:
            _deviceId = std::min<uint8_t>(value, 0x20);
            return false;
        case 0x5F:
            if (value == kResetAllEffectsWord)
            {
                PowerOn(); // datasheet p.35: restores the power-up status and every MIDI parameter
                return true;
            }
            SetEffectsWord(value);
            return false;
        default:
            return false; // 3724h-3735h mike / echo, 3751h auto-test: not modeled (README)
    }
    _fx.Changed();
    return false;
}

bool SynthCore::DeviceAccepted(uint8_t device, bool universal) const
{
    // NRPN 3757h: 20h accepts every device ID; else only that ID (and the universal all-call 7Fh)
    return _deviceId >= 0x20 || device == _deviceId || (universal && device == 0x7F);
}

bool SynthCore::SysEx(const uint8_t* d, uint16_t n)
{
    if (d == nullptr)
        return false;
    // GM System On: F0 7E <dev> 09 01 F7
    if (n >= 4 && d[0] == 0x7E && DeviceAccepted(d[1], true) && d[2] == 0x09 && d[3] == 0x01)
    {
        GsReset();
        return false;
    }
    // GM master volume: F0 7F <dev> 04 01 <lsb> <msb> F7 (the datasheet sends 00 ll)
    if (n >= 6 && d[0] == 0x7F && DeviceAccepted(d[1], true) && d[2] == 0x04 && d[3] == 0x01)
    {
        _fx.params.gmVolume = d[5] & 0x7F;
        return false;
    }
    // Roland GS data set: F0 41 <dev> 42 12 a1 a2 a3 data... checksum F7. The checksum is "don't care"
    // on the chip (datasheet p.29: "x or xx means don't care"), so it is not verified.
    if (n >= 9 && d[0] == 0x41 && DeviceAccepted(d[1], false) && d[2] == 0x42 && d[3] == 0x12)
    {
        const uint32_t address = static_cast<uint32_t>(d[4] & 0x7F) << 14 | static_cast<uint32_t>(d[5] & 0x7F) << 7 |
                                 static_cast<uint32_t>(d[6] & 0x7F);
        for (uint16_t i = 7; i + 1 < n; i++)
            GsWrite(address + (i - 7), d[i] & 0x7F);
        _fx.Changed();
        return false;
    }
    // Dream port write (§6): F0 00 20 00 00 00 12 33 77 pp v3 v2 v1 v0 xx F7; only the two codec ports
    // are honored ("not recommended to write in SAM2695 ports except" 12h and 14h)
    static constexpr uint8_t kDream[8] = {0x00, 0x20, 0x00, 0x00, 0x00, 0x12, 0x33, 0x77};
    if (n >= 14 && std::equal(kDream, kDream + 8, d))
    {
        const uint16_t value = static_cast<uint16_t>((d[9] & 15) << 12 | (d[10] & 15) << 8 | (d[11] & 15) << 4 | (d[12] & 15));
        if (d[8] == 0x12)
            _fx.params.codec0 = value;
        else if (d[8] == 0x14)
            _fx.params.codec1 = value;
    }
    return false;
}

void SynthCore::GsWrite(uint32_t address, uint8_t value)
{
    // GS block number to part: block 0 is part 10 (index 9), blocks 1-9 parts 1-9, A-F parts 11-16
    auto partOfBlock = [](uint32_t block) { return block == 0 ? 9u : block <= 9 ? block - 1 : block; };
    const uint32_t a1 = address >> 14, a2 = (address >> 7) & 0x7F, a3 = address & 0x7F;
    if (a1 != 0x40)
        return;
    FxParams& p = _fx.params;
    if (a2 == 0x00)
    {
        if (a3 <= 0x03)
            _masterTune[a3] = value & 15;
        else if (a3 == 0x04)
            p.gmVolume = value; // GS master volume = the GM bus volume (NRPN 3722h)
        else if (a3 == 0x05)
            _keyShift = std::clamp<uint8_t>(value, 0x28, 0x58);
        else if (a3 == 0x06)
            p.gmPan = value; // "same as" NRPN 3723h (datasheet p.16)
        else if (a3 == 0x7F && value == 0x00)
            GsReset();
        return;
    }
    if (a2 == 0x01)
    {
        if (a3 >= 0x10 && a3 <= 0x1F)
        {
            _channels[partOfBlock(a3 - 0x10)].voiceReserve = std::min<uint8_t>(value, kMaxPolyphony);
            return;
        }
        switch (a3)
        {
            case 0x30: if (value <= 7) p.SelectReverb(value); break;
            case 0x31: if (value <= 7) p.reverbCharacter = value; break;
            case 0x33: p.reverbLevel = value; break;
            case 0x34: p.reverbTime = value; break;
            case 0x35: p.reverbFeedback = value; break;
            case 0x38: if (value <= 7) p.SelectChorus(value); break;
            case 0x3A: p.chorusLevel = value; break;
            case 0x3B: p.chorusFeedback = value; break;
            case 0x3C: p.chorusDelay = value; break;
            case 0x3D: p.chorusRate = value; break;
            case 0x3E: p.chorusDepth = value; break;
            default: break; // 32h pre-LPF, 37h pre-delay, 39h, 3Fh: not in the chip's chart
        }
        return;
    }
    if (a2 >= 0x10 && a2 <= 0x1F)
    {
        Channel& ch = _channels[partOfBlock(a2 - 0x10)];
        if (a3 == 0x02)
            ch.rxChannel = value < 16 ? value : kPartOff;
        else if (a3 == 0x15)
        {
            ch.rhythm = value != 0;
            ResolvePreset(ch);
        }
        else if (a3 == 0x1A)
            ch.velocitySlope = value;
        else if (a3 == 0x1B)
            ch.velocityOffset = value;
        else if (a3 == 0x1F)
            ch.cc1Number = std::min<uint8_t>(value, 0x5F);
        else if (a3 == 0x20)
            ch.cc2Number = std::min<uint8_t>(value, 0x5F);
        else if (a3 >= 0x40 && a3 <= 0x4B)
            ch.scaleTuning[a3 - 0x40] = value;
        else
            return;
        ch.modulationVersion++;
        return;
    }
    if (a2 >= 0x20 && a2 <= 0x2F)
    {
        // controller destinations: a3 = source (0 mod, 1 bend, 2 CAF, 4 CC1, 5 CC2) << 4 | destination
        Channel& ch = _channels[partOfBlock(a2 - 0x20)];
        const uint32_t src = a3 >> 4, dest = a3 & 15;
        static constexpr int kSource[6] = {0, 1, 2, -1, 3, 4};
        if (src > 5 || kSource[src] < 0 || dest >= static_cast<uint32_t>(kCtrlDests))
            return;
        const CtrlSource s = static_cast<CtrlSource>(kSource[src]);
        // the chart lists the LFO1 rate only for the modulation wheel
        if (dest == static_cast<uint32_t>(CtrlDest::LfoRate) && s != CtrlSource::Mod)
            return;
        if (s == CtrlSource::Bend && dest == static_cast<uint32_t>(CtrlDest::Pitch))
            ch.bendRangeSemitones = static_cast<uint8_t>(std::clamp(static_cast<int>(value) - 0x40, 0, 24));
        else
            ch.Ctrl(s, static_cast<CtrlDest>(dest)) = value;
        ch.modulationVersion++;
    }
}

void SynthCore::SetEffectsWord(uint8_t word)
{
    const uint8_t before = _effectsWord;
    _effectsWord = word & 0x7F;
    _fx.EffectsWordChanged(before, _effectsWord);
    EnforceLimit();
}

int SynthCore::ChooseVictim(uint64_t currentNote, int part) const
{
    // Stealing order (README "Voice allocation"): voices of parts within their GS voice reserve are
    // protected (a part may always take its own); then voices in their release first, then voices held
    // only by a pedal, then held keys - melodic before rhythm; inside a tier the quietest, then the
    // oldest. The voices of the note being started are never taken.
    std::array<uint32_t, 16> count{};
    for (const Voice& v : _voices)
        if (v.Counts())
            count[v.channel & 15]++;
    int best = -1;
    std::tuple<int, int, int, float, uint64_t> bestKey{};
    for (size_t i = 0; i < _voices.size(); i++)
    {
        const Voice& v = _voices[i];
        if (!v.Counts() || v.noteId == currentNote)
            continue;
        const int reserved = (v.channel != part && count[v.channel & 15] <= _channels[v.channel & 15].voiceReserve) ? 1 : 0;
        const int tier = v.state == VoiceState::Released ? 0 : v.state == VoiceState::Sustained ? 1 : 2;
        const int protectedRhythm = (v.state == VoiceState::On && v.rhythm) ? 1 : 0;
        const auto key = std::make_tuple(reserved, tier, protectedRhythm, v.Loudness(), v.noteId);
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
        const int victim = ChooseVictim(UINT64_MAX, -1);
        if (victim < 0)
            return;
        _voices[victim].Kill();
        _voicesStolen++;
    }
}

Voice* SynthCore::Allocate(uint64_t currentNote, int part)
{
    uint32_t counted = 0;
    for (const Voice& v : _voices)
        counted += v.Counts() ? 1u : 0u;
    if (counted >= PolyphonyLimit())
    {
        const int victim = ChooseVictim(currentNote, part);
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

void SynthCore::NoteOn(uint8_t chIndex, uint8_t midiKey, uint8_t midiVel, uint32_t offset)
{
    Channel& ch = _channels[chIndex];
    const BankModel* model = Model();
    if (model == nullptr || ch.preset < 0)
        return;
    // GS velocity sense depth / offset (40 1p 1A / 1B): 40h / 40h leave the velocity as played
    const int sensed = static_cast<int>(std::lround(midiVel * (ch.velocitySlope / 64.0) + (ch.velocityOffset - 0x40)));
    const uint8_t vel = static_cast<uint8_t>(std::clamp(sensed, 1, 127));
    // GS master key shift (40 00 05) transposes melodic parts before the zones are chosen; drums stay
    const uint8_t key =
        ch.rhythm ? midiKey : static_cast<uint8_t>(std::clamp(static_cast<int>(midiKey) + _keyShift - 0x40, 0, 127));
    // a key struck again releases its previous note; mono mode releases every note of the part
    for (Voice& v : _voices)
        if (v.channel == chIndex && (v.midiKey == midiKey || ch.mono) &&
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

    // portamento (CC 65 on): the new note glides from the last key of the part at the CC 5 rate
    const bool glide = ch.Portamento() && !ch.rhythm && ch.lastKey <= 127 && ch.lastKey != key;
    const VoiceContext ctx = ContextFor(ch);
    for (size_t i = 0; i < pairCount; i++)
    {
        Voice* v = Allocate(noteId, chIndex);
        if (v == nullptr)
        {
            _notesDropped++;
            continue;
        }
        if (!v->Start(*model, pairs[i].zones, pairs[i].sample, ch, ctx, chIndex, key, midiKey, vel, noteId))
        {
            v->state = VoiceState::Off;
            continue;
        }
        if (glide)
        {
            v->portaCents = 100.0f * (static_cast<float>(ch.lastKey) - static_cast<float>(key));
            v->portaRate = PortamentoCentsPerSample(ch.cc[5]);
        }
        Retarget(*v, offset, true);
    }
    ch.lastKey = key;
}

void SynthCore::NoteOff(uint8_t chIndex, uint8_t key, uint32_t offset)
{
    const Channel& ch = _channels[chIndex];
    for (Voice& v : _voices)
        if (v.channel == chIndex && v.midiKey == key && v.state == VoiceState::On)
        {
            v.NoteOff(ch.Sustain() || (v.sostenuto && ch.Sostenuto()));
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
        cv.rxChannel = ch.rxChannel;
        cv.voiceReserve = ch.voiceReserve;
    }
    const FxParams& p = _fx.params;
    out.reverbProgram = p.reverbType;
    out.reverbCharacter = p.reverbCharacter;
    out.chorusProgram = p.chorusType;
    out.reverbDecaySeconds = _fx.ReverbDecaySeconds();
    out.masterVolume = p.masterVolume;
    out.gmVolume = p.gmVolume;
    out.gmPan = p.gmPan;
    out.masterTuneCents = MasterTuneCents();
    out.keyShift = static_cast<int8_t>(static_cast<int>(_keyShift) - 0x40);
    out.deviceId = _deviceId;
    out.softClip = p.clipMode < 0x40;
    out.codecGainDb = Effects::CodecGainDb(p.codec0);
    out.codecMuted = Effects::CodecMuted(p.codec0);
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
    {
        if (model == nullptr || ch.preset >= static_cast<int32_t>(model->presets.size()) || ch.preset < -1)
            ch.preset = -1;
        if (ch.rxChannel > kPartOff)
            ch.rxChannel = kPartOff;
        ch.cc1Number &= 0x7F;
        ch.cc2Number &= 0x7F;
        ch.bendRangeSemitones = std::min<uint8_t>(ch.bendRangeSemitones, 24);
    }
    for (Voice& v : _voices)
    {
        if (model == nullptr)
            v.state = VoiceState::Off;
        else
            v.Sanitize(*model);
        v.channel &= 15;
        v.midiKey &= 0x7F;
    }
    _effectsWord &= 0x7F;
    _keyShift = std::clamp<uint8_t>(_keyShift, 0x28, 0x58);
    _deviceId = std::min<uint8_t>(_deviceId, 0x20);
    _fx.Sanitize();
}

} // namespace sam2695
