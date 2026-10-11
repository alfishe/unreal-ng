#include "stdafx.h"

#include "nextaudio.h"

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
    for (unsigned i = 0; i < kChips; i++)
    {
        _chips[i]->reset();
        _pan[i] = 3;
    }
    _selected = 0;
    _dac = {0x80, 0x80, 0x80, 0x80};
    _timeKnown = false;
    _tickLeft.clear();
    _tickRight.clear();
    Configure(_ym, _turboSound, _dacEnabled, _acb, _monoMask, _speakerExcluded);
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

void NextAudio::Configure(bool ymMode, bool turboSound, bool dacEnabled, bool acb, uint8_t monoMask, bool speakerExcluded)
{
    // levels up to now were made with the old settings
    if (_now && _timeKnown)
        AdvanceTo(_now());
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
    if (!_turboSound)
        _selected = 0;
}

void NextAudio::WriteSelect(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
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
    _chips[_selected]->setRegister(value & 0x0F);
}

void NextAudio::WriteData(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _chips[_selected]->writeCurrentRegister(value);
}

uint8_t NextAudio::ReadData()
{
    return _chips[_selected]->readCurrentRegister();
}

void NextAudio::WriteDac(unsigned channel, uint8_t value)
{
    if (!_dacEnabled)
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _dac[channel & 3] = value;
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
        if (!_turboSound && i > 0)
            continue;
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
        left += ((_dac[0] - 128) + (_dac[1] - 128)) * 16.0;
        right += ((_dac[2] - 128) + (_dac[3] - 128)) * 16.0;
    }
    _tickLeft.push_back(static_cast<float>(left / 32768.0));
    _tickRight.push_back(static_cast<float>(right / 32768.0));
}

void NextAudio::AdvanceTo(double t)
{
    if (_suppressed)
    {
        _tickT = std::max(_tickT, t);
        return;
    }
    while (_tickT < t)
    {
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
