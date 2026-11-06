#include "wildsoundxmplayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wildsound
{

namespace
{

/// FT2's vibrato / tremolo sine, half a period in 32 steps (0..255)
constexpr uint8_t kSine[32] = {0,   24,  49,  74,  97,  120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                               255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97,  74,  49,  24};

constexpr int32_t kMinPeriod = 1;
constexpr int32_t kMaxPeriod = 31999;
constexpr int kNoteOff = 97;

/// Rxy's volume change by x (FT2)
int RetrigVolume(int x, int v)
{
    switch (x)
    {
        case 1: return v - 1;
        case 2: return v - 2;
        case 3: return v - 4;
        case 4: return v - 8;
        case 5: return v - 16;
        case 6: return v * 2 / 3;
        case 7: return v / 2;
        case 9: return v + 1;
        case 10: return v + 2;
        case 11: return v + 4;
        case 12: return v + 8;
        case 13: return v + 16;
        case 14: return v * 3 / 2;
        case 15: return v * 2;
        default: return v;
    }
}

int Clamp64(int v)
{
    return std::clamp(v, 0, 64);
}

}  // namespace

/// region <Pitch>

int32_t WildSoundXmPlayer::NotePeriod(bool linear, int note, int fineTune)
{
    note = std::clamp(note, 1, 120);
    if (linear)
        return 7680 - (note - 1) * 64 - fineTune / 2;
    // Amiga: 4 x ProTracker periods, C-4 (note 49) = 1712, finetune in 1/128 semitones
    const double semis = (note - 49) + fineTune / 128.0;
    return static_cast<int32_t>(std::lround(1712.0 * std::pow(2.0, -semis / 12.0)));
}

double WildSoundXmPlayer::PeriodToHz(bool linear, int32_t period)
{
    if (period <= 0)
        return 0;
    if (linear)
        return 8363.0 * std::pow(2.0, (4608.0 - period) / 768.0);
    return 8363.0 * 1712.0 / period;
}

/// endregion </Pitch>

/// region <Control>

void WildSoundXmPlayer::SetOutputRate(uint32_t rate)
{
    _s.outputRate = rate ? rate : 44100;
}

void WildSoundXmPlayer::Stop()
{
    const uint32_t rate = _s.outputRate;
    _s = State{};
    _s.outputRate = rate;
}

void WildSoundXmPlayer::Start(const WildSoundStorage& storage)
{
    Stop();
    const WildSoundTrack& track = storage.Track();
    if (!track.valid)
        return;
    _s.channelCount = static_cast<uint8_t>(std::min<int>(track.channels, kMaxChannels));
    _s.linear = track.linearFrequencies;
    // The header's speed as it is, up to 255 (an S3M's initial speed may exceed FT2's 31; the firmware takes the word
    // as it is, reference-firmware.md §D.1); Fxx below #20 sets 1..31 later
    _s.speed = static_cast<uint8_t>(std::clamp<int>(track.speed ? track.speed : 6, 1, 255));
    _s.bpm = static_cast<uint8_t>(std::clamp<int>(track.bpm ? track.bpm : 125, 32, 255));
    _s.globalVolume = 64;
    for (Channel& ch : _s.channels)
        ch.panning = 128;
    _s.playing = true;
    SeekOrder(storage, 0, 0);
    _s.tick = 0;
    _s.tickRemainder = 0;
}

/// endregion </Control>

/// region <Patterns>

uint32_t WildSoundXmPlayer::SkipRows(const uint8_t* data, uint32_t size, int channels, unsigned rows)
{
    uint32_t p = 0;
    for (unsigned r = 0; r < rows; ++r)
    {
        for (int c = 0; c < channels && p < size; ++c)
        {
            const uint8_t b = data[p++];
            if (b & 0x80)
                p += ((b & 1) != 0) + ((b & 2) != 0) + ((b & 4) != 0) + ((b & 8) != 0) + ((b & 16) != 0);
            else
                p += 4;
        }
    }
    return std::min(p, size);
}

void WildSoundXmPlayer::SeekOrder(const WildSoundStorage& storage, unsigned order, unsigned row)
{
    const WildSoundTrack& track = storage.Track();
    if (order >= track.songLength)
        order = track.restart < track.songLength ? track.restart : 0;
    _s.order = static_cast<uint8_t>(order);
    const WildSoundPattern& pat = track.patterns[track.order[order]];
    const unsigned rows = pat.size ? pat.rows : 64;
    if (row >= rows)
        row = 0;
    _s.row = static_cast<uint8_t>(row);
    const uint8_t* data = storage.TrackData() + pat.offset;
    _s.patternOffset = pat.size ? SkipRows(data, pat.size, _s.channelCount, row) : 0;
}

void WildSoundXmPlayer::ReadRow(const WildSoundStorage& storage)
{
    const WildSoundTrack& track = storage.Track();
    const WildSoundPattern& pat = track.patterns[track.order[_s.order]];
    const uint8_t* data = pat.size ? storage.TrackData() + pat.offset : nullptr;
    uint32_t p = _s.patternOffset;
    auto next = [&]() -> uint8_t { return data && p < pat.size ? data[p++] : 0; };
    for (int c = 0; c < _s.channelCount; ++c)
    {
        Channel& ch = _s.channels[c];
        ch.note = ch.instrument = ch.volColumn = ch.effect = ch.param = 0;
        if (!data || p >= pat.size)
            continue;
        const uint8_t b = next();
        if (b & 0x80)
        {
            if (b & 1)
                ch.note = next();
            if (b & 2)
                ch.instrument = next();
            if (b & 4)
                ch.volColumn = next();
            if (b & 8)
                ch.effect = next();
            if (b & 16)
                ch.param = next();
        }
        else
        {
            ch.note = b;
            ch.instrument = next();
            ch.volColumn = next();
            ch.effect = next();
            ch.param = next();
        }
    }
    _s.patternOffset = p;
    ++_s.rows;
}

void WildSoundXmPlayer::AdvanceRow(const WildSoundStorage& storage)
{
    const WildSoundTrack& track = storage.Track();
    if (_s.loopJump)
    {
        _s.loopJump = false;
        _s.jump = false;
        SeekOrder(storage, _s.order, _s.loopJumpRow);
        return;
    }
    if (_s.jump)
    {
        _s.jump = false;
        SeekOrder(storage, _s.jumpOrder, _s.jumpRow);
        return;
    }
    const WildSoundPattern& pat = track.patterns[track.order[_s.order]];
    const unsigned rows = pat.size ? pat.rows : 64;
    if (_s.row + 1u >= rows)
        SeekOrder(storage, _s.order + 1u, 0);
    else
        ++_s.row;
}

/// endregion </Patterns>

/// region <Notes and instruments>

const WildSoundInstrument* WildSoundXmPlayer::Instrument(const WildSoundStorage& storage, uint8_t ins) const
{
    const auto& list = storage.Instruments();
    if (ins == 0 || ins > list.size())
        return nullptr;
    const WildSoundInstrument& i = list[ins - 1];
    return i.length && i.received ? &i : nullptr;
}

void WildSoundXmPlayer::SetInstrument(const WildSoundStorage& storage, Channel& ch)
{
    ch.ins = ch.instrument;
    const WildSoundInstrument* ins = Instrument(storage, ch.ins);
    if (!ins)
        return;
    // An instrument number resets volume, panning, finetune (FT2)
    ch.volume = ins->volume;
    ch.panning = ins->panning;
    ch.fineTune = ins->fineTune;
    ch.relativeNote = ins->relativeNote;
}

void WildSoundXmPlayer::TriggerNote(const WildSoundStorage& storage, Channel& ch)
{
    const WildSoundInstrument* ins = Instrument(storage, ch.ins);
    if (!ins)
    {
        ch.active = false;
        return;
    }
    ch.period = NotePeriod(_s.linear, ch.baseNote + ch.relativeNote, ch.fineTune);
    ch.outPeriod = ch.period;
    ch.targetPeriod = ch.period;
    ch.pos = 0;
    if (ch.effect == 0x09)
    {
        if (ch.param)
            ch.offsetMemory = ch.param;
        const uint64_t start = static_cast<uint64_t>(ch.offsetMemory) * 256;
        if (start >= std::min(ins->length, ins->received))
        {
            ch.active = false;  // FT2: an offset past the sample's end plays nothing
            return;
        }
        ch.pos = start << 32;
    }
    ch.backward = false;
    ch.active = true;
    ch.keyOff = false;
    if (ch.vibratoWave < 4)
        ch.vibratoPos = 0;
    if (ch.tremoloWave < 4)
        ch.tremoloPos = 0;
    ch.retrigCount = 0;
    ch.tremorPos = 0;
    ch.tremorOn = true;
    // Ramp from silence: no click at the note's start
    ch.rampLeft = ch.rampRight = 0;
    ch.rampRemaining = 0;
}

void WildSoundXmPlayer::ProcessNote(const WildSoundStorage& storage, Channel& ch)
{
    const bool porta = ch.effect == 0x03 || ch.effect == 0x05 || ch.volColumn >= 0xF0;
    if (ch.effect == 0x0E && (ch.param >> 4) == 0x5)
        ch.fineTune = static_cast<int8_t>((ch.param & 0x0F) * 16 - 128);
    if (ch.instrument)
    {
        const int8_t e5 = ch.fineTune;
        SetInstrument(storage, ch);
        if (ch.effect == 0x0E && (ch.param >> 4) == 0x5)
            ch.fineTune = e5;
    }
    if (ch.note == kNoteOff)
    {
        // Key-off: no volume envelope in the upload, so the note is cut (FT2)
        ch.keyOff = true;
        ch.volume = 0;
        return;
    }
    if (ch.note >= 1 && ch.note <= 96)
    {
        if (porta && ch.active)
        {
            ch.baseNote = ch.note;
            ch.targetPeriod = NotePeriod(_s.linear, ch.note + ch.relativeNote, ch.fineTune);
        }
        else
        {
            ch.baseNote = ch.note;
            TriggerNote(storage, ch);
        }
    }
}

/// endregion </Notes and instruments>

/// region <Effects>

int WildSoundXmPlayer::Wave(uint8_t wave, uint8_t pos)
{
    pos &= 63;
    switch (wave & 3)
    {
        case 1:  // ramp down
        {
            const int v = (pos & 31) * 8;
            return pos < 32 ? 255 - v : -v;
        }
        case 2:  // square
            return pos < 32 ? 255 : -255;
        default:
            return pos < 32 ? kSine[pos] : -kSine[pos & 31];
    }
}

void WildSoundXmPlayer::DoVolumeSlide(Channel& ch, uint8_t param)
{
    const int up = param >> 4, down = param & 0x0F;
    ch.volume = Clamp64(up ? ch.volume + up : ch.volume - down);
}

void WildSoundXmPlayer::DoPortaToNote(Channel& ch)
{
    if (!ch.targetPeriod || ch.period == ch.targetPeriod)
        return;
    const int32_t delta = ch.portaSpeed * 4;
    if (ch.period < ch.targetPeriod)
        ch.period = std::min(ch.period + delta, ch.targetPeriod);
    else
        ch.period = std::max(ch.period - delta, ch.targetPeriod);
    ch.outPeriod = ch.period;
    if (ch.glissando)
    {
        // Round to the nearest semitone of the instrument's scale
        int best = 1;
        int32_t bestDiff = INT32_MAX;
        for (int n = 1; n <= 96; ++n)
        {
            const int32_t d = std::abs(NotePeriod(_s.linear, n + ch.relativeNote, ch.fineTune) - ch.period);
            if (d < bestDiff)
            {
                bestDiff = d;
                best = n;
            }
        }
        ch.outPeriod = NotePeriod(_s.linear, best + ch.relativeNote, ch.fineTune);
    }
}

void WildSoundXmPlayer::DoVibrato(Channel& ch)
{
    const int delta = (Wave(ch.vibratoWave, ch.vibratoPos) * ch.vibratoDepth) >> 5;
    ch.outPeriod = std::clamp(ch.period + delta, kMinPeriod, kMaxPeriod);
    ch.vibratoPos = static_cast<uint8_t>((ch.vibratoPos + ch.vibratoSpeed) & 63);
}

void WildSoundXmPlayer::DoTremolo(Channel& ch)
{
    const int delta = (Wave(ch.tremoloWave, ch.tremoloPos) * ch.tremoloDepth) >> 6;
    ch.outVolume = Clamp64(ch.volume + delta);
    ch.tremoloPos = static_cast<uint8_t>((ch.tremoloPos + ch.tremoloSpeed) & 63);
}

void WildSoundXmPlayer::DoTremor(Channel& ch)
{
    const int on = (ch.tremorParam >> 4) + 1, off = (ch.tremorParam & 0x0F) + 1;
    ch.tremorOn = ch.tremorPos < on;
    ch.tremorPos = static_cast<uint8_t>((ch.tremorPos + 1) % (on + off));
}

void WildSoundXmPlayer::DoRetrig(const WildSoundStorage& storage, Channel& ch)
{
    const int interval = ch.retrigParam & 0x0F;
    if (!interval)
        return;
    if (++ch.retrigCount >= interval)
    {
        ch.retrigCount = 0;
        ch.volume = Clamp64(RetrigVolume(ch.retrigParam >> 4, ch.volume));
        const int32_t vol = ch.volume;
        TriggerNote(storage, ch);
        ch.volume = vol;
        ch.rampLeft = ch.targetLeft;  // a retrigger keeps its loudness: no fade-in
        ch.rampRight = ch.targetRight;
    }
}

void WildSoundXmPlayer::ApplyVolumeColumnRow(Channel& ch)
{
    const uint8_t v = ch.volColumn;
    const int x = v & 0x0F;
    if (v >= 0x10 && v <= 0x50)
        ch.volume = v - 0x10;
    else if (v >= 0x80 && v < 0x90)
        ch.volume = Clamp64(ch.volume - x);
    else if (v >= 0x90 && v < 0xA0)
        ch.volume = Clamp64(ch.volume + x);
    else if (v >= 0xA0 && v < 0xB0)
        ch.vibratoSpeed = static_cast<uint8_t>(x << 2);
    else if (v >= 0xB0 && v < 0xC0)
    {
        if (x)
            ch.vibratoDepth = static_cast<uint8_t>(x);
    }
    else if (v >= 0xC0 && v < 0xD0)
        ch.panning = x * 16;
    else if (v >= 0xF0 && x)
        ch.portaSpeed = static_cast<uint8_t>(x << 4);
}

void WildSoundXmPlayer::ApplyVolumeColumnTick(Channel& ch)
{
    const uint8_t v = ch.volColumn;
    const int x = v & 0x0F;
    if (v >= 0x60 && v < 0x70)
        ch.volume = Clamp64(ch.volume - x);
    else if (v >= 0x70 && v < 0x80)
        ch.volume = Clamp64(ch.volume + x);
    else if (v >= 0xB0 && v < 0xC0)
        DoVibrato(ch);
    else if (v >= 0xD0 && v < 0xE0)
        ch.panning = std::max(0, ch.panning - x);
    else if (v >= 0xE0 && v < 0xF0)
        ch.panning = std::min(255, ch.panning + x);
    else if (v >= 0xF0)
        DoPortaToNote(ch);
}

void WildSoundXmPlayer::RowEffects(const WildSoundStorage& storage, Channel& ch, bool firstPass)
{
    (void)storage;
    const uint8_t p = ch.param;
    const int x = p >> 4, y = p & 0x0F;
    switch (ch.effect)
    {
        case 0x01:
            if (p)
                ch.portaUp = p;
            break;
        case 0x02:
            if (p)
                ch.portaDown = p;
            break;
        case 0x03:
            if (p)
                ch.portaSpeed = p;
            break;
        case 0x04:
            if (x)
                ch.vibratoSpeed = static_cast<uint8_t>(x << 2);
            if (y)
                ch.vibratoDepth = static_cast<uint8_t>(y);
            break;
        case 0x05:
        case 0x06:
        case 0x0A:
            if (p)
                ch.volSlide = p;
            break;
        case 0x07:
            if (x)
                ch.tremoloSpeed = static_cast<uint8_t>(x << 2);
            if (y)
                ch.tremoloDepth = static_cast<uint8_t>(y);
            break;
        case 0x08:
            ch.panning = p;
            break;
        case 0x0B:
            if (firstPass)
            {
                const bool breakSet = _s.jump && _s.jumpOrder == static_cast<uint8_t>(_s.order + 1);
                _s.jump = true;
                _s.jumpOrder = p;
                if (!breakSet)
                    _s.jumpRow = 0;
            }
            break;
        case 0x0C:
            ch.volume = Clamp64(p);
            break;
        case 0x0D:
            if (firstPass)
            {
                if (!_s.jump)
                    _s.jumpOrder = static_cast<uint8_t>(_s.order + 1);
                _s.jump = true;
                _s.jumpRow = static_cast<uint8_t>(x * 10 + y);
            }
            break;
        case 0x0E:
            switch (x)
            {
                case 0x1:
                    if (y)
                        ch.finePortaUp = static_cast<uint8_t>(y);
                    ch.period = std::max(kMinPeriod, ch.period - ch.finePortaUp * 4);
                    ch.outPeriod = ch.period;
                    break;
                case 0x2:
                    if (y)
                        ch.finePortaDown = static_cast<uint8_t>(y);
                    ch.period = std::min(kMaxPeriod, ch.period + ch.finePortaDown * 4);
                    ch.outPeriod = ch.period;
                    break;
                case 0x3:
                    ch.glissando = y != 0;
                    break;
                case 0x4:
                    ch.vibratoWave = static_cast<uint8_t>(y);
                    break;
                case 0x6:
                    if (!firstPass)
                        break;
                    if (y == 0)
                        ch.loopRow = _s.row;
                    else if (ch.loopCount == 0)
                    {
                        ch.loopCount = static_cast<uint8_t>(y);
                        _s.loopJump = true;
                        _s.loopJumpRow = ch.loopRow;
                    }
                    else if (--ch.loopCount)
                    {
                        _s.loopJump = true;
                        _s.loopJumpRow = ch.loopRow;
                    }
                    break;
                case 0x7:
                    ch.tremoloWave = static_cast<uint8_t>(y);
                    break;
                case 0xA:
                    if (y)
                        ch.fineVolUp = static_cast<uint8_t>(y);
                    ch.volume = Clamp64(ch.volume + ch.fineVolUp);
                    break;
                case 0xB:
                    if (y)
                        ch.fineVolDown = static_cast<uint8_t>(y);
                    ch.volume = Clamp64(ch.volume - ch.fineVolDown);
                    break;
                case 0xC:
                    ch.cutTick = static_cast<uint8_t>(y);
                    if (y == 0)
                        ch.volume = 0;
                    break;
                case 0xE:
                    if (firstPass && !_s.inDelay)
                        _s.patternDelaySet = static_cast<uint8_t>(y);
                    break;
                default:
                    break;
            }
            break;
        case 0x0F:
            if (p == 0)
                break;  // F00: FT2 halts; the card keeps playing
            if (p < 0x20)
                _s.speed = p;
            else
                _s.bpm = p;
            break;
        case 0x10:  // Gxx
            _s.globalVolume = static_cast<uint8_t>(Clamp64(p));
            break;
        case 0x11:  // Hxy
            if (p)
                ch.globalSlide = p;
            break;
        case 0x14:  // Kxx
            ch.keyOffTick = p;
            if (p == 0)
            {
                ch.keyOff = true;
                ch.volume = 0;
            }
            break;
        case 0x19:  // Pxy
            if (p)
                ch.panSlide = p;
            break;
        case 0x1B:  // Rxy
            if (x || y)
                ch.retrigParam = static_cast<uint8_t>(((x ? x : ch.retrigParam >> 4) << 4) | (y ? y : (ch.retrigParam & 0x0F)));
            break;
        case 0x1D:  // Txy
            if (p)
                ch.tremorParam = p;
            break;
        case 0x21:  // X1y / X2y
            if (x == 1)
            {
                if (y)
                    ch.xfinePortaUp = static_cast<uint8_t>(y);
                ch.period = std::max(kMinPeriod, ch.period - ch.xfinePortaUp);
            }
            else if (x == 2)
            {
                if (y)
                    ch.xfinePortaDown = static_cast<uint8_t>(y);
                ch.period = std::min(kMaxPeriod, ch.period + ch.xfinePortaDown);
            }
            ch.outPeriod = ch.period;
            break;
        default:
            break;
    }
}

void WildSoundXmPlayer::RowTick(const WildSoundStorage& storage, Channel& ch)
{
    ch.cutTick = ch.keyOffTick = ch.delayTick = 0;
    ch.outPeriod = ch.period;
    ch.outVolume = ch.volume;
    const bool delay = ch.effect == 0x0E && (ch.param >> 4) == 0xD && (ch.param & 0x0F);
    if (!_s.inDelay)
    {
        if (delay)
            ch.delayTick = static_cast<uint8_t>(ch.param & 0x0F);
        else
        {
            ProcessNote(storage, ch);
            ApplyVolumeColumnRow(ch);
        }
    }
    RowEffects(storage, ch, !_s.inDelay);
    ch.outVolume = ch.volume;
    ch.outPeriod = ch.period;
}

void WildSoundXmPlayer::OtherTick(const WildSoundStorage& storage, Channel& ch)
{
    ch.outPeriod = ch.period;
    if (ch.delayTick && _s.tick == ch.delayTick)
    {
        ch.delayTick = 0;
        ProcessNote(storage, ch);
        ApplyVolumeColumnRow(ch);
    }
    if (ch.cutTick && _s.tick == ch.cutTick)
        ch.volume = 0;
    if (ch.keyOffTick && _s.tick == ch.keyOffTick)
    {
        ch.keyOff = true;
        ch.volume = 0;
    }
    ApplyVolumeColumnTick(ch);
    ch.outVolume = ch.volume;

    const uint8_t p = ch.param;
    const int x = p >> 4, y = p & 0x0F;
    switch (ch.effect)
    {
        case 0x00:
            if (p)
            {
                const int add = _s.tick % 3 == 1 ? x : _s.tick % 3 == 2 ? y : 0;
                ch.outPeriod = NotePeriod(_s.linear, ch.baseNote + ch.relativeNote + add, ch.fineTune);
                if (!_s.linear && add)
                    ch.outPeriod = std::max(kMinPeriod, ch.outPeriod + (ch.period - NotePeriod(_s.linear, ch.baseNote + ch.relativeNote, ch.fineTune)));
            }
            break;
        case 0x01:
            ch.period = std::max(kMinPeriod, ch.period - ch.portaUp * 4);
            ch.outPeriod = ch.period;
            break;
        case 0x02:
            ch.period = std::min(kMaxPeriod, ch.period + ch.portaDown * 4);
            ch.outPeriod = ch.period;
            break;
        case 0x03:
            DoPortaToNote(ch);
            break;
        case 0x04:
            DoVibrato(ch);
            break;
        case 0x05:
            DoPortaToNote(ch);
            DoVolumeSlide(ch, ch.volSlide);
            ch.outVolume = ch.volume;
            break;
        case 0x06:
            DoVibrato(ch);
            DoVolumeSlide(ch, ch.volSlide);
            ch.outVolume = ch.volume;
            break;
        case 0x07:
            DoTremolo(ch);
            break;
        case 0x0A:
            DoVolumeSlide(ch, ch.volSlide);
            ch.outVolume = ch.volume;
            break;
        case 0x0E:
            if (x == 0x9 && y && _s.tick % y == 0)
            {
                const int32_t vol = ch.volume;
                TriggerNote(storage, ch);
                ch.volume = vol;
            }
            break;
        case 0x11:
        {
            const int up = ch.globalSlide >> 4, down = ch.globalSlide & 0x0F;
            _s.globalVolume = static_cast<uint8_t>(Clamp64(up ? _s.globalVolume + up : _s.globalVolume - down));
            break;
        }
        case 0x19:
        {
            const int right = ch.panSlide >> 4, left = ch.panSlide & 0x0F;
            ch.panning = std::clamp(right ? ch.panning + right : ch.panning - left, 0, 255);
            break;
        }
        case 0x1B:
            DoRetrig(storage, ch);
            break;
        case 0x1D:
            DoTremor(ch);
            break;
        default:
            break;
    }
    if (ch.effect == 0x1D && !ch.tremorOn)
        ch.outVolume = 0;
}

/// endregion </Effects>

/// region <Ticks and mixing>

void WildSoundXmPlayer::UpdateOutput(const WildSoundStorage& storage, Channel& ch)
{
    (void)storage;
    const double hz = PeriodToHz(_s.linear, std::clamp(ch.outPeriod ? ch.outPeriod : ch.period, kMinPeriod, kMaxPeriod));
    ch.step = static_cast<uint64_t>(hz / _s.outputRate * 4294967296.0);

    const int vol = ch.active ? ch.outVolume * _s.globalVolume / 64 : 0;  // 0..64
    const double gain = vol / 64.0 * 65536.0;
    const double pan = ch.panning / 256.0;
    ch.targetLeft = static_cast<int32_t>(gain * std::sqrt(1.0 - pan));
    ch.targetRight = static_cast<int32_t>(gain * std::sqrt(pan));
    ch.rampRemaining = kRampSamples;
    ch.rampLeftStep = (ch.targetLeft - ch.rampLeft) / kRampSamples;
    ch.rampRightStep = (ch.targetRight - ch.rampRight) / kRampSamples;
}

void WildSoundXmPlayer::NextTick(const WildSoundStorage& storage)
{
    if (_s.tick == 0)
    {
        if (!_s.inDelay)
            ReadRow(storage);
        for (int c = 0; c < _s.channelCount; ++c)
            RowTick(storage, _s.channels[c]);
    }
    else
    {
        for (int c = 0; c < _s.channelCount; ++c)
            OtherTick(storage, _s.channels[c]);
    }
    for (int c = 0; c < _s.channelCount; ++c)
        UpdateOutput(storage, _s.channels[c]);

    if (++_s.tick >= _s.speed)
    {
        _s.tick = 0;
        if (_s.patternDelaySet)
        {
            _s.patternDelay = _s.patternDelaySet;
            _s.patternDelaySet = 0;
        }
        if (_s.patternDelay)
        {
            --_s.patternDelay;
            _s.inDelay = true;
        }
        else
        {
            _s.inDelay = false;
            AdvanceRow(storage);
        }
    }
}

int WildSoundXmPlayer::MixChannel(const WildSoundStorage& storage, Channel& ch, int32_t* acc, size_t frames)
{
    const WildSoundInstrument* ins = Instrument(storage, ch.ins);
    if (!ins || (!ch.active && ch.rampLeft == 0 && ch.rampRight == 0))
    {
        ch.active = ch.active && ins;
        return 0;
    }
    const uint8_t* data = storage.SampleData(*ins);
    const int64_t length = std::min(ins->length, ins->received);
    const int loopType = ins->LoopType();
    const int64_t loopStart = std::min<int64_t>(ins->loopStart, length);
    const int64_t loopEnd = std::min<int64_t>(ins->loopEnd, length);
    const bool loops = (loopType == 1 || loopType == 2) && loopEnd > loopStart;
    const int64_t end = loops ? loopEnd : length;
    auto sampleAt = [&](int64_t i) -> int {
        if (i < 0 || i >= length)
            return 0;
        return static_cast<int>(data[i]) - 128;
    };

    for (size_t f = 0; f < frames; ++f)
    {
        if (ch.rampRemaining)
        {
            ch.rampLeft += ch.rampLeftStep;
            ch.rampRight += ch.rampRightStep;
            if (--ch.rampRemaining == 0)
            {
                ch.rampLeft = ch.targetLeft;
                ch.rampRight = ch.targetRight;
            }
        }
        if (!ch.active)
            break;
        const int64_t i = static_cast<int64_t>(ch.pos >> 32);
        const int64_t frac = static_cast<int64_t>(ch.pos & 0xFFFFFFFFu);
        int64_t next = ch.backward ? i - 1 : i + 1;
        if (!ch.backward && loops && next >= loopEnd)
            next = loopType == 1 ? loopStart : loopEnd - 1;
        const int s0 = sampleAt(i), s1 = sampleAt(next);
        const int s = static_cast<int>(s0 + (((s1 - s0) * frac) >> 32));  // -128..127
        const int64_t v = static_cast<int64_t>(s) << 8;
        acc[2 * f] += static_cast<int32_t>((v * ch.rampLeft) >> 16);
        acc[2 * f + 1] += static_cast<int32_t>((v * ch.rampRight) >> 16);

        // Advance and loop
        if (!ch.backward)
        {
            ch.pos += ch.step;
            const int64_t at = static_cast<int64_t>(ch.pos >> 32);
            if (at >= end)
            {
                if (!loops)
                {
                    ch.active = false;
                }
                else if (loopType == 1)
                {
                    const uint64_t len = static_cast<uint64_t>(loopEnd - loopStart) << 32;
                    while ((ch.pos >> 32) >= static_cast<uint64_t>(loopEnd))
                        ch.pos -= len;
                }
                else
                {
                    const uint64_t over = ch.pos - (static_cast<uint64_t>(loopEnd) << 32);
                    const uint64_t top = (static_cast<uint64_t>(loopEnd) << 32) - 1;
                    ch.pos = over < top - (static_cast<uint64_t>(loopStart) << 32) ? top - over : static_cast<uint64_t>(loopStart) << 32;
                    ch.backward = true;
                }
            }
        }
        else
        {
            const uint64_t floor = static_cast<uint64_t>(loopStart) << 32;
            if (ch.pos < floor + ch.step)
            {
                const uint64_t under = floor + ch.step - ch.pos;
                ch.pos = floor + std::min(under, (static_cast<uint64_t>(loopEnd - loopStart) << 32) - 1);
                ch.backward = false;
            }
            else
            {
                ch.pos -= ch.step;
            }
        }
    }
    return 1;
}

void WildSoundXmPlayer::Render(const WildSoundStorage& storage, int16_t* out, size_t frames)
{
    std::memset(out, 0, frames * 2 * sizeof(int16_t));
    if (!_s.playing || !storage.Playable() || _s.channelCount == 0)
        return;
    std::vector<int32_t> acc;
    const double samplesPerTick = _s.outputRate * 2.5;
    size_t done = 0;
    while (done < frames)
    {
        if ((_s.tickRemainder >> 32) == 0)
        {
            NextTick(storage);
            _s.tickRemainder += static_cast<uint64_t>(samplesPerTick / _s.bpm * 4294967296.0);
            continue;
        }
        const size_t n = std::min<size_t>(frames - done, static_cast<size_t>(_s.tickRemainder >> 32));
        acc.assign(n * 2, 0);
        for (int c = 0; c < _s.channelCount; ++c)
            MixChannel(storage, _s.channels[c], acc.data(), n);
        // Headroom: four channels at full volume and centre pan peak at 0.71 of full scale; more channels add up
        // like uncorrelated sources (sqrt)
        const double scale = 0.5 / std::sqrt(std::max(4, static_cast<int>(_s.channelCount)));
        const int32_t mul = static_cast<int32_t>(scale * 65536.0);
        for (size_t i = 0; i < n * 2; ++i)
        {
            const int64_t v = (static_cast<int64_t>(acc[i]) * mul) >> 16;
            out[done * 2 + i] = static_cast<int16_t>(std::clamp<int64_t>(v, -32768, 32767));
        }
        done += n;
        _s.tickRemainder -= static_cast<uint64_t>(n) << 32;
    }
}

/// endregion </Ticks and mixing>

void WildSoundXmPlayer::SaveState(uint8_t* dst) const
{
    std::memcpy(dst, &_s, sizeof(State));
}

void WildSoundXmPlayer::LoadState(const uint8_t* src)
{
    std::memcpy(&_s, src, sizeof(State));
}

}  // namespace wildsound
