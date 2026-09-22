#include "gsmodplayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

/// region <ProTracker tables>

namespace
{
// Classic Amiga period table, one octave in finetune step 0 (C..B, low to
// high). Lower octaves double the period, higher halve it. The firmware's
// AMFRQTB is this same table (materials/gs/gs-firmware/_AMFRQTB.a80).
const uint16_t kPeriodTable[12] = {
    1712, 1616, 1525, 1440, 1357, 1281, 1209, 1141, 1077, 1017, 961, 907
};

// ProTracker vibrato/tremolo sine (0-255, two mirrored quarters)
const uint8_t kSineTable[32] = {
    0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24
};

// Base note of a new row's period, including the finetune trim (each of the
// 16 finetune steps is 1/8 semitone; value 8..F read as -8..-1)
int16_t finetuneShift(uint8_t finetune)
{
    int16_t value = static_cast<int16_t>(finetune & 0x0F);
    if (value >= 8)
        value -= 16;
    return value; // -8..+7
}

uint16_t readBe16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void writeLe32(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t readLe32(const uint8_t* src)
{
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8)
        | (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

// 16-bit TTD helpers are not needed yet - the runtime blob is byte/32-bit
} // namespace

/// endregion </ProTracker tables>

/// region <Parsing>

bool GSModPlayer::parse(const uint8_t* data, size_t size, const char** reason)
{
    _parsed = false;
    if (reason) *reason = "";

    if (size < 1084)
    {
        if (reason) *reason = "module shorter than the ProTracker header";
        return false;
    }

    // 31-sample M.K. signature at offset 1080 (M!K! = >64 patterns, v1.05+)
    const char* tag = reinterpret_cast<const char*>(data + 1080);
    const bool mk = std::memcmp(tag, "M.K.", 4) == 0 || std::memcmp(tag, "M!K!", 4) == 0
        || std::memcmp(tag, "4CHN", 4) == 0 || std::memcmp(tag, "FLT4", 4) == 0;
    if (!mk)
    {
        if (reason) *reason = "missing M.K./M!K! signature at offset 1080";
        return false;
    }

    _sampleCount = 31;
    size_t sampleBytes = 0;
    for (size_t i = 0; i < _sampleCount; i++)
    {
        const uint8_t* header = data + 20 + i * 30;
        SampleInfo& info = _samples[i];
        std::memcpy(info.name, header, 22);
        info.name[22] = '\0';
        info.lengthWords = readBe16(header + 22);
        info.finetune = header[24] & 0x0F;
        // ProTracker volume is 0..64 (0x40 = full); masking with 0x3F turned
        // the common 0x40 into 0 and silenced most instruments of real
        // modules (cc_wizard.mod: 13 of 17 samples at 64) - reported as
        // sample dropouts. The card's 6-bit latch maps 64 -> 63 downstream.
        info.volume = std::min<uint8_t>(header[25], 64);
        info.loopStartWords = readBe16(header + 26);
        info.loopLengthWords = readBe16(header + 28);
        if (info.loopLengthWords < 2)
            info.loopLengthWords = 0; // one-shot per ProTracker convention

        info.dataLength = static_cast<uint32_t>(info.lengthWords) * 2;
        info.loopStart = static_cast<uint32_t>(info.loopStartWords) * 2;
        info.loopLength = static_cast<uint32_t>(info.loopLengthWords) * 2;
        if (info.loopStart + info.loopLength > info.dataLength)
        {
            // Clamp loops that overrun their sample (damaged-but-playable)
            info.loopStart = std::min(info.loopStart, info.dataLength);
            info.loopLength = std::min(info.loopLength, info.dataLength - info.loopStart);
            if (info.loopLength < 4)
                info.loopLength = 0;
        }
        sampleBytes += info.dataLength;
    }

    _songLength = data[950];
    _restartPosition = data[951];
    if (_songLength == 0 || _songLength > 128)
    {
        if (reason) *reason = "song length byte out of range";
        return false;
    }

    uint8_t highestPattern = 0;
    for (size_t i = 0; i < 128; i++)
    {
        _patternTable[i] = data[952 + i];
        if (i < _songLength)
            highestPattern = std::max(highestPattern, _patternTable[i]);
    }
    if (highestPattern > 127)
    {
        if (reason) *reason = "pattern table entry above 127";
        return false;
    }
    _patternCount = static_cast<size_t>(highestPattern) + 1;

    const size_t expectedSize = 1084 + _patternCount * 1024 + sampleBytes;
    if (size < expectedSize)
    {
        if (reason) *reason = "module truncated (pattern/sample data missing)";
        return false;
    }

    // Sample data offsets follow the pattern block, in header order
    uint32_t offset = 1084 + static_cast<uint32_t>(_patternCount) * 1024;
    for (size_t i = 0; i < _sampleCount; i++)
    {
        _samples[i].dataOffset = offset;
        offset += _samples[i].dataLength;
    }

    _module.assign(data, data + expectedSize);
    _parsed = true;
    return true;
}

/// endregion </Parsing>

/// region <Note/effect math>

uint16_t GSModPlayer::periodForNote(int note, uint8_t finetune)
{
    // note 0..59 = C-1..B-6 in the PT table range (907..1712 base octave)
    if (note < 0)
        note = 0;
    if (note > 59)
        note = 59;

    const int octave = note / 12;
    const int index = note % 12;

    // Period with the 1/8-semitone finetune trim (approximation of the
    // firmware's 16-step table - tolerance domain, cross-validation target)
    const int16_t shift = finetuneShift(finetune);
    const double semitone = std::ldexp(1.0, octave); // 2^octave period scale
    const double trimmed = semitone * (kPeriodTable[index] / 32.0)
        * std::pow(2.0, -shift / 96.0) * 32.0;

    return static_cast<uint16_t>(std::clamp<double>(trimmed, kMinPeriod, kMaxPeriod));
}

uint32_t GSModPlayer::incrementForPeriod(uint16_t period)
{
    // Amiga PAL rate: freq = 3546895 / period. Per-quantum 16.16 step =
    // freq * 65536 / 37500 - the firmware's GSFRQTB+CHFADV equivalent
    // resampled onto the card's 37.5 kHz sample clock
    const uint64_t num = 3546895ull * 65536ull;
    const uint64_t den = static_cast<uint64_t>(period) * kQuantumRate;
    return static_cast<uint32_t>(num / den);
}

/// endregion </Note/effect math>

/// region <Playback control>

void GSModPlayer::reset()
{
    _playing = false;
    _songPosition = 0;
    _row = 0;
    _tick = 0;
    _speed = kDefaultSpeed;
    _bpm = kDefaultBpm;
    _quantaIntoTick = 0;
    _tickQuanta = kDefaultTickQuanta;
    _rowDelay = 0;
    _jumpFlag = false;
    _breakFlag = false;
    for (auto& ch : _channels)
        ch = ChannelState{};
}

void GSModPlayer::start(uint8_t songPosition)
{
    if (!_parsed)
        return;

    reset();
    if (songPosition < _songLength)
        _songPosition = songPosition;
    _tick = 0;
    _quantaIntoTick = _tickQuanta; // process the first row on the next quantum
    _playing = true;
}

void GSModPlayer::stop()
{
    _playing = false;
}

void GSModPlayer::continuePlay()
{
    if (_parsed)
        _playing = true;
}

uint8_t GSModPlayer::currentPattern() const
{
    if (!_parsed || _songPosition >= _songLength)
        return 0;
    return _patternTable[_songPosition];
}

void GSModPlayer::setBpm(uint8_t bpm)
{
    _bpm = bpm;
    // FXF: TICKLEN = 37500/(0.4*BPM) quanta, floored like the firmware's
    // integer math (the Fxx effect path uses the same expression)
    _tickQuanta = kQuantumRate * 5 / (2 * bpm);
    if (_tickQuanta == 0)
        _tickQuanta = 1;
}

/// endregion </Playback control>

/// region <Sequencer>

void GSModPlayer::advanceQuantum(uint8_t out[kChannels], uint8_t vols[kChannels])
{
    if (!_playing)
    {
        // Stopped card: DAC latches hold (the card keeps its own copy); the
        // caller ignores the fill on !isPlaying
        for (int i = 0; i < kChannels; i++)
        {
            out[i] = 0x80;
            vols[i] = 0;
        }
        return;
    }

    // Tick boundary (TICKLEN quanta per tempo tick)
    if (++_quantaIntoTick >= _tickQuanta)
    {
        _quantaIntoTick = 0;
        processTick();
    }

    for (int i = 0; i < kChannels; i++)
        fetchChannelOutput(i, out[i], vols[i]);
}

void GSModPlayer::processTick()
{
    if (_tick == 0)
        processRow(); // new row: notes latch, row-start effects apply

    // Per-tick effects on every tick including 0 (PT applies slides on 0 too)
    const uint8_t* pattern = _module.data() + 1084 + static_cast<size_t>(currentPattern()) * 1024;
    const uint8_t* rowData = pattern + static_cast<size_t>(_row) * 4 * 4;
    for (int i = 0; i < kChannels; i++)
    {
        const uint8_t* note = rowData + i * 4;
        const uint8_t effect = note[2] & 0x0F;
        const uint8_t param = note[3];
        ChannelState& ch = _channels[i];
        applyEffect(ch, effect, param, _tick == 0);

        // Vibrato/tremolo render ONCE PER TICK (PT semantics, firmware
        // CHFADV cadence): the sine advances at the tick rate and the
        // deltas compose with the current period/volume for the whole tick
        // without permanently altering them. Rendering per QUANTUM instead
        // advanced the sine at 37.5 kHz - 4xx/7xx became kHz-rate FM/ring
        // modulation and the whole mix turned audibly dirty (live NEARTHGS
        // report); the depth is the canonical sine*depth/128 as well.
        //
        // Gated on THIS row's effect column, not on the remembered param:
        // PT only bends pitch/volume while the row's effect is actually
        // 4xx/6xx (vibrato) or 7xx (tremolo) - vibratoParam/tremoloParam
        // are memory for the NEXT time that effect reappears (0-param
        // reuse), not a standing modulation that outlives the row. Applying
        // it unconditionally bled a stale vibrato's pitch wobble into every
        // later unrelated note once any 4xx/6xx had ever been seen -
        // reported as constant pitch-warble distortion on real content
        // (2026-09-21).
        if (effect == 0x04 || effect == 0x06)
        {
            const uint8_t rate = ch.vibratoParam >> 4;
            const int depth = ch.vibratoParam & 0x0F;
            const int sine = kSineTable[(ch.vibratoPos / 64) % 32];
            const int delta = sine * depth / 128; // period units
            ch.increment = incrementForPeriod(static_cast<uint16_t>(
                std::clamp<int>(ch.period - delta, kMinPeriod, kMaxPeriod)));
            ch.vibratoPos = static_cast<uint16_t>((ch.vibratoPos + rate * 64) % (32 * 64));
        }
        else
        {
            // Not vibrating this row: play the clean period (undoes any
            // offset left over from a previous 4xx/6xx row)
            ch.increment = incrementForPeriod(ch.period);
        }

        if (effect == 0x07)
        {
            const uint8_t rate = ch.tremoloParam >> 4;
            const int depth = ch.tremoloParam & 0x0F;
            const int sine = kSineTable[(ch.tremoloPos / 64) % 32];
            ch.tremoloDelta = static_cast<int8_t>(sine * depth / 128);
            ch.tremoloPos = static_cast<uint16_t>((ch.tremoloPos + rate * 64) % (32 * 64));
        }
        else
        {
            ch.tremoloDelta = 0;
        }
    }

    if (++_tick >= _speed)
    {
        _tick = 0;

        // EEx row delay holds the row without reprocessing it
        if (_rowDelay > 0)
        {
            _rowDelay--;
            return;
        }

        // Deferred Bxx/Dxx navigation acts at the row end
        if (_jumpFlag)
        {
            _jumpFlag = false;
            _songPosition = (_jumpPosition < _songLength) ? _jumpPosition : 0;
            _row = 0;
            return;
        }
        if (_breakFlag)
        {
            _breakFlag = false;
            _row = (_breakRow < kRowsPerPattern) ? _breakRow : 0;
            if (_songPosition + 1 >= _songLength)
                wrapSong();
            else
                _songPosition = static_cast<uint8_t>(_songPosition + 1);
            return;
        }

        _row++;
        if (_row >= kRowsPerPattern)
        {
            _row = 0;
            if (_songPosition + 1 >= _songLength)
                wrapSong();
            else
                _songPosition = static_cast<uint8_t>(_songPosition + 1);
        }
    }
}

// Song end (firmware QUANTUM.a80 EFXSKP7): restart at byte 951's position
// when it is inside the song (else 0), and reset speed to 6 and the tick
// length to 750 quanta (125 BPM). The firmware does NOT rewrite MTBPM (COM68
// still reports the old value), only TICKLEN - mirrored here. Without this
// a module that ends on a slow Fxx (cc_wizard.mod: F20 in its last pattern)
// looped at 32 BPM forever on the LW card while the LLE played the loop at
// full tempo (live report 2026-09-22: "restarted, 4x slower").
void GSModPlayer::wrapSong()
{
    _songPosition = (_restartPosition < _songLength) ? _restartPosition : 0;
    _speed = kDefaultSpeed;
    _tickQuanta = kDefaultTickQuanta;
    _rowDelay = 0;
    _jumpFlag = false;
    _breakFlag = false;
}

void GSModPlayer::processRow()
{
    const uint8_t* pattern = _module.data() + 1084 + static_cast<size_t>(currentPattern()) * 1024;
    const uint8_t* rowData = pattern + static_cast<size_t>(_row) * 4 * 4;

    for (int i = 0; i < kChannels; i++)
    {
        const uint8_t* note = rowData + i * 4;
        ChannelState& ch = _channels[i];

        const uint16_t period = ((static_cast<uint16_t>(note[0] & 0x0F) << 8) | note[1]) & 0x0FFF;
        const uint8_t sample = (note[0] & 0xF0) | (note[2] >> 4); // instrument nibbles
        const uint8_t effect = note[2] & 0x0F;
        const uint8_t param = note[3];

        const bool hasNote = period >= kMinPeriod && period <= kMaxPeriod;
        const bool hasSample = sample >= 1 && sample <= 31;

        if (hasSample && sample <= _sampleCount)
        {
            ch.sample = sample;
            ch.volume = _samples[sample - 1].volume;
        }

        if (hasNote)
        {
            // Plain note restarts the voice; 3xx glides instead
            if (effect == 0x03 && param != 0)
            {
                ch.portamentoTarget = period;
            }
            else
            {
                ch.period = period;
                ch.position = 0;
                ch.vibratoPos = 0;
                ch.tremoloPos = 0;
                ch.increment = incrementForPeriod(ch.period);
                // 9xx sample offset: param * 256 BYTES (PT semantics), param 0
                // reuses the last non-zero offset. Only a row that carries 9xx
                // starts mid-sample - a plain note always starts at 0 (the
                // memory is for 900, not for every later note). Previously the
                // offset was param bytes (256x too small) and the memory was
                // applied to every subsequent note: cc_wizard.mod uses 9xx 177
                // times, every hit landed at the wrong sample position
                if (effect == 0x09)
                {
                    if (param != 0)
                        ch.offsetParam = param;
                    ch.position = static_cast<uint64_t>(ch.offsetParam) << 24; // (param*256) << 16
                }
            }
        }
        else if (effect == 0x03 && param != 0)
        {
            ch.portamentoTarget = period; // target without a new note
        }
    }
}

void GSModPlayer::applyEffect(ChannelState& ch, uint8_t effect, uint8_t param, bool rowStart)
{
    auto slidePeriod = [&ch](int delta)
    {
        ch.period = static_cast<uint16_t>(std::clamp<int>(ch.period + delta, kMinPeriod, kMaxPeriod));
        ch.increment = incrementForPeriod(ch.period);
    };

    auto slideVolume = [&ch](int delta)
    {
        ch.volume = static_cast<uint8_t>(std::clamp<int>(ch.volume + delta, 0, 64));
    };

    // Tone-portamento step toward the latched 3xx target, shared by 0x03
    // and 0x05 (5xx reuses whatever speed 3xx last set - its own param is
    // the volume-slide amount, never a portamento speed)
    auto tonePortamento = [&ch, &slidePeriod]()
    {
        if (ch.portamentoTarget == 0 || ch.portamentoParam == 0)
            return;
        const int step = ch.portamentoParam;
        if (ch.period < ch.portamentoTarget)
            slidePeriod(std::min<int>(step, ch.portamentoTarget - ch.period));
        else if (ch.period > ch.portamentoTarget)
            slidePeriod(-std::min<int>(step, ch.period - ch.portamentoTarget));
    };

    switch (effect)
    {
        case 0x00: // arpeggio: table of base, +n semitones, +m
            if (param != 0)
            {
                // Recompute from the current period's base note
                int base = 0;
                while (base < 59 && periodForNote(base + 1, 0) < ch.period)
                    base++;
                const int step = (_tick % 3 == 0) ? 0
                    : (_tick % 3 == 1) ? (param >> 4) : (param & 0x0F);
                ch.period = periodForNote(base + step, 0);
                ch.increment = incrementForPeriod(ch.period);
            }
            break;

        case 0x01: // portamento up
            if (rowStart && param != 0) ch.portamentoParam = param;
            if (!rowStart || param != 0) slidePeriod(-ch.portamentoParam);
            break;

        case 0x02: // portamento down
            if (rowStart && param != 0) ch.portamentoParam = param;
            if (!rowStart || param != 0) slidePeriod(ch.portamentoParam);
            break;

        case 0x03: // tone portamento toward the latched target
            if (rowStart && param != 0) ch.portamentoParam = param;
            tonePortamento();
            break;

        case 0x04: // vibrato (rendered per tick after the effect loop)
            if (rowStart && param != 0) ch.vibratoParam = param;
            break;

        case 0x05: // tone portamento (3xx memory) + volume slide
            tonePortamento();
            if (param != 0) ch.volumeSlideParam = param;
            if (ch.volumeSlideParam != 0)
            {
                const uint8_t slide = ch.volumeSlideParam;
                slideVolume((slide >> 4) != 0 ? (slide >> 4) : -(slide & 0x0F));
            }
            break;

        case 0x06: // vibrato (own memory, rendered after the loop) + volume slide
            if (param != 0) ch.volumeSlideParam = param;
            if (ch.volumeSlideParam != 0)
            {
                const uint8_t slide = ch.volumeSlideParam;
                slideVolume((slide >> 4) != 0 ? (slide >> 4) : -(slide & 0x0F));
            }
            break;

        case 0x07: // tremolo (rendered per tick after the effect loop)
            if (rowStart && param != 0) ch.tremoloParam = param;
            break;

        case 0x09: // sample offset (consumed at note latch)
            if (param != 0) ch.offsetParam = param;
            break;

        case 0x0A: // volume slide
        {
            if (param != 0) ch.volumeSlideParam = param;
            const uint8_t slide = ch.volumeSlideParam;
            if (slide != 0)
                slideVolume((slide >> 4) != 0 ? (slide >> 4) : -(slide & 0x0F));
            break;
        }

        case 0x0B: // position jump - deferred to the row end
            if (rowStart)
            {
                _jumpFlag = true;
                _jumpPosition = param;
            }
            break;

        case 0x0C: // set volume
            if (rowStart)
                ch.volume = std::min<uint8_t>(param, 64); // Cxx: 0..64
            break;

        case 0x0D: // pattern break - deferred to the row end
            if (rowStart)
            {
                _breakFlag = true;
                _breakRow = static_cast<uint8_t>(((param >> 4) * 10) + (param & 0x0F));
            }
            break;

        case 0x0E:
        {
            const uint8_t kind = param >> 4;
            const uint8_t arg = param & 0x0F;
            switch (kind)
            {
                case 0x0: break;                          // E0x filter: parse-ignore
                case 0x1: if (rowStart && arg) slidePeriod(-arg); break;   // fine up
                case 0x2: if (rowStart && arg) slidePeriod(arg); break;    // fine down
                case 0x3: break;                          // glissando control: ignored
                case 0x4: break;                          // vibrato waveform: ignored
                case 0x5: break;                          // finetune set: ignored
                case 0x6: break;                          // loop pattern: v1 ignore
                case 0x7: break;                          // tremolo waveform: ignored
                case 0x8: break;                          // E8x: PT-unused palette entry
                case 0x9: if (arg != 0 && _tick % arg == 0) { ch.position = 0; ch.increment = incrementForPeriod(ch.period); } break; // retrigger
                case 0xA: if (rowStart && arg) slideVolume(arg); break;    // fine volume up
                case 0xB: if (rowStart && arg) slideVolume(-arg); break;   // fine volume down
                case 0xC: if (_tick >= arg) ch.volume = 0; break;          // cut after arg ticks
                case 0xD: if (_tick == arg) { ch.position = 0; ch.increment = incrementForPeriod(ch.period); } break; // delayed note start
                case 0xE: if (rowStart && arg) _rowDelay = arg; break;     // pattern delay
                case 0xF: break;                          // EFx invert loop: parse-ignore
            }
            break;
        }

        case 0x0F: // speed (param < 32) / tempo (>= 32)
            if (rowStart && param != 0)
            {
                if (param < 32)
                {
                    _speed = param;
                }
                else
                {
                    _bpm = param;
                    _tickQuanta = kQuantumRate * 5 / (2 * param); // 37500/(0.4*bpm)
                    if (_tickQuanta == 0) _tickQuanta = 1;
                }
            }
            break;

        default:
            break;
    }
}

void GSModPlayer::fetchChannelOutput(int channel, uint8_t& out, uint8_t& vol)
{
    ChannelState& ch = _channels[channel];

    if (ch.sample == 0 || ch.muted)
    {
        out = 0x80;
        vol = 0;
        return;
    }

    const SampleInfo& info = _samples[ch.sample - 1];
    if (info.dataLength == 0)
    {
        out = 0x80;
        vol = 0;
        return;
    }

    // One-shot already finished (increment zeroed below): keep easing the
    // DAC toward 0x80, firmware GENZERO style, until the next note
    if (ch.increment == 0)
    {
        const int eased = (static_cast<int>(ch.lastOut) - 0x80) / 2;
        ch.lastOut = static_cast<uint8_t>(0x80 + eased);
        out = ch.lastOut;
        vol = static_cast<uint8_t>(std::clamp<int>(ch.volume + ch.tremoloDelta, 0, 64));
        return;
    }

    // Advance the 16.16 position with loop handling (64-bit throughout - a
    // 131070-byte sample's loopEnd<<16 is ~8.6G, already past uint32_t range)
    uint64_t position = ch.position;
    position = static_cast<uint64_t>(static_cast<int64_t>(position) + ch.increment);
    if (info.loopLength >= 4)
    {
        const uint64_t loopEnd = static_cast<uint64_t>(info.loopStart + info.loopLength) << 16;
        if (position >= loopEnd)
            position = (position - loopEnd) + (static_cast<uint64_t>(info.loopStart) << 16);
    }
    else if ((position >> 16) >= info.dataLength)
    {
        // One-shot end (firmware GEN_L.a80 GENZERO): the DAC is eased from
        // the last value to 0x80 by successive halving instead of being
        // held on the last byte forever - holding leaves a DC step that
        // clicks at the next note (cc_wizard.mod is mostly one-shots)
        position = (static_cast<uint64_t>(info.dataLength) - 1) << 16;
        ch.increment = 0;
        ch.position = position;
        const int last = static_cast<int>(ch.lastOut) - 0x80;
        const int eased = last / 2; // toward 0x80, converges in <= 8 quanta
        ch.lastOut = static_cast<uint8_t>(0x80 + eased);
        out = ch.lastOut;
        vol = static_cast<uint8_t>(std::clamp<int>(ch.volume + ch.tremoloDelta, 0, 64));
        return;
    }
    ch.position = position;

    // ProTracker sample data is SIGNED 8-bit; the card's DAC latch is
    // 0x80-centered unsigned (the firmware XORs 0x80 on upload, Unreal's
    // gshle did the same). Feeding the raw signed byte turned every
    // near-zero sample into a full-swing 0x00/0xFF - the "fuzz/distortion"
    // on real content (cc_wizard.mod, live-verified 2026-09-22)
    const uint8_t* data = _module.data() + info.dataOffset;
    const uint32_t index = static_cast<uint32_t>(position >> 16);
    auto at = [&](uint32_t i) -> int {
        // Next-sample lookups wrap inside the loop like the firmware's
        // GENCHK reload; past a one-shot end they clamp to the last byte
        if (info.loopLength >= 4 && i >= info.loopStart + info.loopLength)
            i = info.loopStart + (i - (info.loopStart + info.loopLength));
        return static_cast<int>(data[std::min<uint32_t>(i, info.dataLength - 1)] ^ 0x80);
    };

    // Firmware SGEN1 parity (materials/gs/gs-firmware/firmware/src/sgen/):
    // sample-and-hold with a (prev+next)/2 midpoint at phase crossings -
    // approximated here by linear interpolation on the 16.16 fraction.
    // (SGEN2's box-averaging is for steps above 1 byte/quantum, which the
    // ProTracker period range 113..856 never reaches - max ~0.84.) Pure
    // nearest-neighbour left ~30x more energy above 6 kHz than the LLE on
    // the same content (live A/B 2026-09-22)
    const int frac = static_cast<int>(position & 0xFFFF);
    const int a = at(index);
    const int bnext = at(index + 1);
    const int value = a + (((bnext - a) * frac) >> 16);
    out = static_cast<uint8_t>(std::clamp(value, 0, 255));
    ch.lastOut = out;
    // Tremolo delta was rendered once per tick in processTick (PT
    // semantics): held here, not re-derived per quantum
    vol = static_cast<uint8_t>(std::clamp<int>(ch.volume + ch.tremoloDelta, 0, 64));
}

/// endregion </Sequencer>

/// region <TTD runtime state>

size_t GSModPlayer::serializeStateSize() const
{
    return 4 /* playing/speed/bpm/flags */ + 8 /* songLength-padded counters */
        + 4 * 4 /* position/row/tick/quanta */ + 4 /* tickQuanta */
        + 2 /* jump/break */ + 1 /* rowDelay */
        + kChannels * sizeof(ChannelState) + 4; /* channel-state guard */
}

void GSModPlayer::serializeState(uint8_t* dst) const
{
    uint8_t* p = dst;
    *p++ = _playing ? 1 : 0;
    *p++ = _speed;
    *p++ = _bpm;
    *p++ = static_cast<uint8_t>((_jumpFlag ? 1 : 0) | (_breakFlag ? 2 : 0));
    *p++ = _songPosition;
    *p++ = _row;
    *p++ = _tick;
    *p++ = _rowDelay;
    writeLe32(p, _quantaIntoTick); p += 4;
    writeLe32(p, _tickQuanta); p += 4;
    *p++ = _jumpPosition;
    *p++ = _breakRow;
    // ChannelState is a POD aggregate of fixed-width members: byte layout is
    // deterministic on the platforms this core targets (little-endian TTD blob)
    std::memcpy(p, _channels, kChannels * sizeof(ChannelState));
    p += kChannels * sizeof(ChannelState);
    writeLe32(p, 0x47534D50); // "GSMP" guard
}

void GSModPlayer::loadState(const uint8_t* src, size_t size)
{
    if (size < serializeStateSize())
        return;

    const uint8_t* p = src;
    _playing = (*p++ != 0);
    _speed = *p++;
    _bpm = *p++;
    const uint8_t flags = *p++;
    _jumpFlag = (flags & 1) != 0;
    _breakFlag = (flags & 2) != 0;
    _songPosition = *p++;
    _row = *p++;
    _tick = *p++;
    _rowDelay = *p++;
    _quantaIntoTick = readLe32(p); p += 4;
    _tickQuanta = readLe32(p); p += 4;
    _jumpPosition = *p++;
    _breakRow = *p++;
    std::memcpy(_channels, p, kChannels * sizeof(ChannelState));
    p += kChannels * sizeof(ChannelState);
    if (readLe32(p) != 0x47534D50)
    {
        // Guard mismatch: refuse half-loaded state
        reset();
        return;
    }
}

/// endregion </TTD runtime state>
