#include "stdafx.h"

#include "nextaudio.h"

#include <cmath>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

namespace
{
constexpr double kTickT = 16.0;  // one generator tick of the 1.75 MHz AY clock, in base T-states
constexpr double kDcAlpha = 2.0 * 3.14159265358979 * 10.0 / 218750.0;  // a 10 Hz high-pass at the tick rate
}

NextAudio::NextAudio(EmulatorContext* context) : _context(context)
{
    for (auto& chip : _chips)
        chip = std::make_unique<SoundChip_AY8910>(context);
    Reset();
}

void NextAudio::Reset()
{
    ResetChips();
    _dac = {0x80, 0x80, 0x80, 0x80};
    _timeKnown = false;
    _tickLeft.clear();
    _tickRight.clear();
    Configure(_ym, _turboSound, _dacEnabled, _acb, _monoMask, _speakerExcluded, _held);
}

/// reset_i of turbosound.vhd (a soft or hard reset, or NR #06 bits 1:0 = 11): chip 0 selected, both pan bits of every chip on, the
/// register files zero with R7 = #FF (ym2149.vhd 101-109)
void NextAudio::ResetChips()
{
    for (unsigned i = 0; i < kChips; i++)
    {
        _chips[i]->reset();
        _chips[i]->writeRegister(7, 0xFF);
        _chips[i]->setRegister(0);
        _pan[i] = 3;
        _addr[i] = 0;
    }
    _selected = 0;
}

void NextAudio::ApplyBeeperLevels()
{
    if (!_context->pSoundManager)
        return;
    static const int32_t kNormal[4] = {-1280, -768, 768, 1280};  // 00, MIC, EAR, EAR + MIC: 0 / 512 / 2048 / 2560 around the middle
    static const int32_t kExcluded[4] = {0, 0, 0, 0};
    _context->pSoundManager->getBeeper().setDacLevels(_speakerExcluded ? kExcluded : kNormal);
}

void NextAudio::ReleaseBeeperLevels()
{
    if (_context->pSoundManager)
        _context->pSoundManager->getBeeper().clearDacLevels();
}

void NextAudio::Configure(bool ymMode, bool turboSound, bool dacEnabled, bool acb, uint8_t monoMask, bool speakerExcluded, bool held)
{
    // levels up to now were made with the old settings
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _held = held;
    if (_held)
        ResetChips();  // every configuration write of the mode finds the chips in reset: nothing a program writes stays
    // soundrive.vhd: reset_i is `reset or not nr_08_dac_en`: the four levels sit at #80 whenever the DAC is off
    if (!dacEnabled)
        _dac = {0x80, 0x80, 0x80, 0x80};
    _ym = ymMode;
    _turboSound = turboSound;
    _dacEnabled = dacEnabled;
    _acb = acb;
    _monoMask = monoMask & 7;
    _speakerExcluded = speakerExcluded;
    ApplyBeeperLevels();
    for (unsigned i = 0; i < kChips; i++)
    {
        _chips[i]->setChipModel(_ym ? AYChipModel::YM2149 : AYChipModel::AY8910);
        _chips[i]->setStereoMode((_monoMask >> i) & 1 ? AYStereoMode::Mono : (_acb ? AYStereoMode::ACB : AYStereoMode::ABC));
    }
    // turbosound_en does not move ay_select: a chip chosen while it was on stays the one that is heard and written after it goes off
}

void NextAudio::WriteSelect(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    if (_held)
        return;  // audio_ay_reset has priority over every write
    // bit 7 and bits 4:2 all set: the chip select (bits 1:0: 11 = AY 0, 10 = AY 1, 01 = AY 2) and the pan (6 left, 5 right)
    if ((value & 0x9C) == 0x9C)
    {
        if (_turboSound)
        {
            const unsigned chip = (value & 3) == 3 ? 0 : (value & 3) == 2 ? 1 : (value & 3) == 1 ? 2 : 0;
            _selected = chip;
            _pan[chip] = static_cast<uint8_t>(((value >> 6) & 1) << 1 | ((value >> 5) & 1));
        }
        return;
    }
    // psg_addr (turbosound.vhd 190): only bytes whose bits 7:5 are 0 are a register address; the chip keeps five bits and a register
    // above #0F is not writable
    if (value & 0xE0)
        return;
    _addr[_selected] = value & 0x1F;
    _chips[_selected]->setRegister(value & 0x0F);
}

void NextAudio::WriteData(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    if (_held || (_addr[_selected] & 0x10))
        return;  // ym2149.vhd 123: `addr(4) = '0'`
    _chips[_selected]->writeCurrentRegister(value);
}

uint8_t NextAudio::ReadData()
{
    // ym2149.vhd 159-160: a YM answers #FF for a register above #0F; an AY mirrors the low four bits
    if ((_addr[_selected] & 0x10) && _ym)
        return 0xFF;
    return _chips[_selected]->readCurrentRegister();
}

void NextAudio::SetPiAudio(unsigned left, unsigned right)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _piLeft = left & 0x3FF;
    _piRight = right & 0x3FF;
}

uint8_t NextAudio::ReadIdentity() const
{
    const unsigned id = 3 - _selected;  // AY_ID of psg0 / psg1 / psg2 in turbosound.vhd: "11", "10", "01"
    return static_cast<uint8_t>((id << 6) | (_addr[_selected] & 0x1F));
}

void NextAudio::WriteDac(unsigned channel, uint8_t value)
{
    if (!_dacEnabled)
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _dac[channel & 3] = value;
    _dacMeter = std::max({_dacMeter, std::fabs(((_dac[0] - 128) + (_dac[1] - 128)) * 16.0),
                              std::fabs(((_dac[2] - 128) + (_dac[3] - 128)) * 16.0)});
}

/// One generator tick: the per-chip stereo law of turbosound.vhd (ABC: left = A + B, right = B + C; ACB: left = A + C,
/// right = B + C; mono: all three on both sides), the pan bits, the DAC, and the 13-bit PCM of audio_mixer.vhd scaled to
/// 16 bits (x4); a slow high-pass takes the DC of the unsigned sum out
void NextAudio::Tick()
{
    double left = 0, right = 0;
    for (unsigned i = 0; i < kChips; i++)
    {
        SoundChip_AY8910& chip = *_chips[i];
        chip.updateState(true);
        if (!_turboSound && i != _selected)
            continue;  // turbosound.vhd: `ay_select = i or turbosound_en`
        const double a = chip.left()[0] * 255.0, b = chip.left()[1] * 255.0, c = chip.left()[2] * 255.0;
        double l, r;
        if ((_monoMask >> i) & 1)
            l = r = a + b + c;
        else if (_acb)
        {
            l = a + c;
            r = b + c;
        }
        else
        {
            l = a + b;
            r = b + c;
        }
        {
            // The part's meter (before its gain)
            double* m = &_meter[i * 4];
            m[0] = std::min(m[0], l);
            m[1] = std::max(m[1], l);
            m[2] = std::min(m[2], r);
            m[3] = std::max(m[3], r);
        }
        // The mixer's part gain (host-side; skipped while every chip's is 1, so the hardware's sum is exact)
        if (_chipGainActive)
        {
            l *= _partGain[i];
            r *= _partGain[i];
        }
        const uint8_t pan = _pan[i];
        if (pan & 2)
            left += l;
        if (pan & 1)
            right += r;
    }
    left *= 4.0;
    right *= 4.0;
    // the unsigned sum has a DC level; the DAC is centred on #80
    _dcLeft += (left - _dcLeft) * kDcAlpha;
    _dcRight += (right - _dcRight) * kDcAlpha;
    left -= _dcLeft;
    right -= _dcRight;
    if (_dacEnabled)
    {
        left += DacSide(false);
        right += DacSide(true);
    }
    // the Pi's I2S: 10 bits unsigned in the 13-bit PCM (audio_mixer.vhd 89-100), x4 here; #200 is silence
    left += (static_cast<double>(_piLeft) - 512.0) * 4.0;
    right += (static_cast<double>(_piRight) - 512.0) * 4.0;
    _tickLeft.push_back(static_cast<float>(left / 32768.0));
    _tickRight.push_back(static_cast<float>(right / 32768.0));
    if (_dacTap)
        _tickDac.push_back(_dacEnabled ? DacMono() : 0);
}

bool NextAudio::QuietTicks(size_t ticks)
{
    if (!_quietTicksEnabled || ticks == 0)
        return false;
    for (const auto& chip : _chips)
    {
        if (!chip->silentAtRest())
            return false;
    }
    // Tick() with every chip level 0 (silentAtRest: amplitude 0 now, and the levels the last tick made were 0 - the
    // chips' own output filters rest on 0): the same arithmetic, which must leave the DC filter where it is
    double left = 0, right = 0;
    left *= 4.0;
    right *= 4.0;
    const double dcLeft = _dcLeft + (left - _dcLeft) * kDcAlpha;
    const double dcRight = _dcRight + (right - _dcRight) * kDcAlpha;
    if (dcLeft != _dcLeft || dcRight != _dcRight)
        return false;
    left -= dcLeft;
    right -= dcRight;
    if (_dacEnabled)
    {
        left += DacSide(false);
        right += DacSide(true);
    }
    left += (static_cast<double>(_piLeft) - 512.0) * 4.0;
    right += (static_cast<double>(_piRight) - 512.0) * 4.0;
    _tickLeft.insert(_tickLeft.end(), ticks, static_cast<float>(left / 32768.0));
    _tickRight.insert(_tickRight.end(), ticks, static_cast<float>(right / 32768.0));
    if (_dacTap)
        _tickDac.insert(_tickDac.end(), ticks, _dacEnabled ? DacMono() : 0);
    for (const auto& chip : _chips)
        chip->advanceSilent(static_cast<uint32_t>(ticks));
    _quietTicks += ticks;
    return true;
}

void NextAudio::AdvanceTo(double t)
{
    if (_suppressed)
    {
        _tickT = std::max(_tickT, t);
        return;
    }
    // A quiet run to t when the chips are silent at rest (QuietTicks), asked at the start and every 256 ticks. The tick
    // times are whole multiples of a binary fraction of a T (the CPU clock's), so k additions of 16 are 16 x k exactly
    uint32_t untilAsk = 0;
    while (_tickT < t)
    {
        if (untilAsk-- == 0)
        {
            untilAsk = 255;
            const size_t ticks = static_cast<size_t>(std::ceil((t - _tickT) / kTickT));
            const double last = _tickT + kTickT * static_cast<double>(ticks - 1);  // the run's last tick: before t
            if (ticks > 0 && last < t && last + kTickT >= t && QuietTicks(ticks))
            {
                _tickT += kTickT * static_cast<double>(ticks);
                return;
            }
        }
        Tick();
        _tickT += kTickT;
    }
}

void NextAudio::AudioFrameStart(bool synthesisSuppressed)
{
    _suppressed = synthesisSuppressed;
    if (_now)
    {
        _frameStartT = _now();
        if (!_timeKnown)
        {
            _tickT = _frameStartT;
            _timeKnown = true;
        }
    }
    _tickLeft.clear();
    _tickRight.clear();
    _tickDac.clear();
    ResetMeters();
}

void NextAudio::AudioFrameEnd(size_t samples)
{
    if (!_now || !_timeKnown)
        return;
    const double frameLength = static_cast<double>(_context->config.frame);
    AdvanceTo(_frameStartT + frameLength);
    _hadSound = false;
    if (samples == 0)
        return;
    samples = std::min<size_t>(samples, MAX_SAMPLES_PER_FRAME);
    const size_t n = _tickLeft.size();
    float peak = 0;
    for (size_t k = 0; k < samples; k++)
    {
        const size_t from = n * k / samples, to = std::max(from + 1, n * (k + 1) / samples);
        double l = 0, r = 0;
        size_t count = 0;
        for (size_t i = from; i < to && i < n; i++, count++)
        {
            l += _tickLeft[i];
            r += _tickRight[i];
        }
        if (count)
        {
            l /= count;
            r /= count;
        }
        peak = std::max({peak, std::fabs(static_cast<float>(l)), std::fabs(static_cast<float>(r))});
        _buffer[k * 2] = static_cast<int16_t>(std::clamp(l * 32767.0, -32768.0, 32767.0));
        _buffer[k * 2 + 1] = static_cast<int16_t>(std::clamp(r * 32767.0, -32768.0, 32767.0));
    }
    _hadSound = peak > 0.002f;
    if (_dacTap)
    {
        // The same tick-to-sample mapping as the mix above, in integers
        const size_t ticks = _tickDac.size();
        for (size_t k = 0; k < samples; k++)
        {
            const size_t from = ticks * k / samples, to = std::max(from + 1, ticks * (k + 1) / samples);
            int64_t sum = 0;
            size_t count = 0;
            for (size_t i = from; i < to && i < ticks; i++, count++)
                sum += _tickDac[i];
            _dacTapBuffer[k] = static_cast<int16_t>(count ? sum / static_cast<int64_t>(count) : 0);
        }
    }

    // The parts' meters: each chip's swing (half the largest side's peak-to-peak), the DAC's largest side, in the
    // 13-bit PCM x 4 of Tick, as a share of full scale
    for (unsigned i = 0; i < kChips; i++)
    {
        const double* m = &_meter[i * 4];
        const double swing = std::max(m[1] - m[0], m[3] - m[2]) * 0.5 * 4.0;
        _partPeak[i] = m[1] >= m[0] ? static_cast<float>(std::min(1.0, swing / 32768.0)) : 0.0f;
    }
    _partPeak[kChips] = static_cast<float>(std::min(1.0, _dacMeter / 32768.0));
}

void NextAudio::ResetMeters()
{
    for (unsigned i = 0; i < kChips; i++)
    {
        double* m = &_meter[i * 4];
        m[0] = m[2] = 1e30;
        m[1] = m[3] = -1e30;
    }
    // The DAC holds its level between writes: the frame starts at the level it has
    _dacMeter = _dacEnabled ? std::max(std::fabs(((_dac[0] - 128) + (_dac[1] - 128)) * 16.0),
                                       std::fabs(((_dac[2] - 128) + (_dac[3] - 128)) * 16.0))
                            : 0.0;
}

double NextAudio::DacSide(bool right) const
{
    const unsigned a = right ? 2 : 0;
    if (!_dacGainActive)
        return ((_dac[a] - 128) + (_dac[a + 1] - 128)) * 16.0;
    return ((_dac[a] - 128) * _dacGain[a] + (_dac[a + 1] - 128) * _dacGain[a + 1]) * 16.0;
}

void NextAudio::SetAudioPartTap(size_t part, bool on)
{
    if (part != kChips)
        return;
    _dacTap = on;
    _tickDac.clear();
}

const int16_t* NextAudio::AudioPartTap(size_t part) const
{
    return part == kChips && _dacTap ? _dacTapBuffer.data() : nullptr;
}

void NextAudio::UpdateDacGains()
{
    _dacGainActive = false;
    for (unsigned c = 0; c < 4; c++)
    {
        _dacGain[c] = _dacChannelGain[c] * _partGain[kChips];
        _dacGainActive = _dacGainActive || _dacGain[c] != 1.0;
    }
}

AudioSourceType NextAudio::AudioPartType(size_t part) const
{
    static const AudioSourceType kTypes[kParts] = {AudioSourceType::NextAy1, AudioSourceType::NextAy2,
                                                   AudioSourceType::NextAy3, AudioSourceType::NextDac};
    return part < kParts ? kTypes[part] : AudioSourceType::Custom;
}

std::string NextAudio::AudioPartName(size_t part) const
{
    static const char* const kNames[kParts] = {"Next AY 1", "Next AY 2", "Next AY 3", "Next DAC"};
    return part < kParts ? kNames[part] : std::string();
}

void NextAudio::SetAudioPartGain(size_t part, float gain)
{
    if (part >= kParts)
        return;
    const double g = std::clamp(static_cast<double>(gain), 0.0, 1.0);
    if (_partGain[part] == g)
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());  // the ticks up to now were made with the old gain
    _partGain[part] = g;
    _chipGainActive = _partGain[0] != 1.0 || _partGain[1] != 1.0 || _partGain[2] != 1.0;
    UpdateDacGains();
}

void NextAudio::AudioChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const
{
    if (type == AudioSourceType::NextDac)
    {
        static const char* const kKeys[4] = {"a", "b", "c", "d"};
        static const char* const kNames[4] = {"A (left)", "B (left)", "C (right)", "D (right)"};
        for (unsigned c = 0; c < 4; c++)
            out.push_back({kKeys[c], kNames[c], true, false});
        return;
    }
    if (type == AudioSourceType::NextAy1 || type == AudioSourceType::NextAy2 || type == AudioSourceType::NextAy3)
    {
        out.push_back({"a", "A", true, false});
        out.push_back({"b", "B", true, false});
        out.push_back({"c", "C", true, false});
    }
}

void NextAudio::SetAudioChannel(AudioSourceType type, size_t index, bool muted, float volume)
{
    if (type == AudioSourceType::NextDac && index < 4)
    {
        const double g = muted ? 0.0 : std::clamp(static_cast<double>(volume), 0.0, 1.0);
        if (_dacChannelGain[index] == g)
            return;
        if (_now && _timeKnown)
            AdvanceTo(_now());
        _dacChannelGain[index] = g;
        UpdateDacGains();
        return;
    }
    const int chip = type == AudioSourceType::NextAy1   ? 0
                     : type == AudioSourceType::NextAy2 ? 1
                     : type == AudioSourceType::NextAy3 ? 2
                                                        : -1;
    if (chip < 0 || index >= 3)
        return;
    SoundChip_AY8910& ay = *_chips[static_cast<size_t>(chip)];
    const auto ch = static_cast<uint8_t>(index);
    if (ay.isChannelMuted(ch) == muted && ay.getChannelVolume(ch) == static_cast<double>(volume))
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());
    ay.setChannelMuted(ch, muted);
    ay.setChannelVolume(ch, volume);
}

std::vector<std::pair<std::string, std::string>> NextAudio::AudioStateFields() const
{
    auto hex = [](unsigned v, unsigned digits) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%0*X", static_cast<int>(digits), v);
        return std::string(buf);
    };
    std::vector<std::pair<std::string, std::string>> fields;
    fields.push_back({"chips", _turboSound ? "3 (turbosound)" : "1 (turbosound off)"});
    fields.push_back({"selected AY", std::to_string(_selected)});
    fields.push_back({"chip type", _ym ? "YM2149" : "AY-3-8912"});
    fields.push_back({"stereo", _acb ? "ACB" : "ABC"});
    for (unsigned i = 0; i < kChips; i++)
    {
        std::string regs;
        for (unsigned r = 0; r < 14; r++)
            regs += (r ? " " : "") + hex(_chips[i]->readRegister(static_cast<uint8_t>(r)), 2);
        fields.push_back({"AY " + std::to_string(i) + " regs 0-13", regs});
        fields.push_back({"AY " + std::to_string(i) + " pan / mono", std::string(_pan[i] & 2 ? "L" : "-") + (_pan[i] & 1 ? "R" : "-") + ((_monoMask >> i) & 1 ? " mono" : " stereo")});
    }
    fields.push_back({"DAC", _dacEnabled ? "on" : "off"});
    fields.push_back({"DAC A B C D", hex(_dac[0], 2) + " " + hex(_dac[1], 2) + " " + hex(_dac[2], 2) + " " + hex(_dac[3], 2)});
    return fields;
}
