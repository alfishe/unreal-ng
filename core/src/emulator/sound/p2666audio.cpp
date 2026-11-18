#include "stdafx.h"

#include "p2666audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "debugger/breakpoints/chipregister.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/ayhistory.h"
#include "emulator/sound/soundmanager.h"

namespace
{
/// A 10 Hz high-pass at the tick rate (3.5625 MHz / 15.5): the DAC word is unsigned, the output capacitor takes the DC
constexpr double kDcAlpha = 2.0 * 3.14159265358979 * 10.0 / (3562500.0 / 15.5);
}  // namespace

P2666Audio::P2666Audio(EmulatorContext* context) : _context(context)
{
    for (auto& chip : _chips)
    {
        chip = std::make_unique<SoundChip_AY8910>(context);
        chip->setChipModel(AYChipModel::YM2149);  // the 32-step envelope of YM2149_linmix
    }
    Reset();
}

P2666Audio::~P2666Audio() = default;

void P2666Audio::Reset()
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    for (unsigned i = 0; i < kChips; i++)
    {
        _chips[i]->reset();
        for (uint8_t r = 0; r < 14; r++)
            _chips[i]->writeRegister(r, 0);  // reg <= (others => '0'): R7 = 0, every tone and noise gate open
        _chips[i]->setRegister(0);
        _regs[i].fill(0);
        _addr[i] = 0;
        _oda[i] = 0;
    }
    _selected = 0;
    _covox = 0;
    _fe = 0;
}

void P2666Audio::ApplyBeeperLevels()
{
    static const int32_t kSilent[4] = {0, 0, 0, 0};
    if (_context->pSoundManager)
        _context->pSoundManager->getBeeper().setDacLevels(kSilent);
}

void P2666Audio::ReleaseBeeperLevels()
{
    if (_context->pSoundManager)
        _context->pSoundManager->getBeeper().clearDacLevels();
}

/// region <Ports>

void P2666Audio::WriteAddress(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    // PaE.vhd:1311-1319: the address goes to the chip selected before this write (ym_number is a signal: the case
    // reads its old value), then #FF / #FE select chip 0 / 1
    const unsigned chip = _selected;
    _addr[chip] = value;
    _chips[chip]->setRegister(value & 0x0F);
    if (value == 0xFF)
        _selected = 0;
    else if (value == 0xFE)
        _selected = 1;
    RecordWrite(0xFFFD, value, value);
}

void P2666Audio::WriteData(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    const unsigned chip = _selected;
    const uint8_t reg = _addr[chip] & 0x0F;  // case addr(3 downto 0): #10..#1D are R0..R13 again
    if (reg > 13)
        return;
    _regs[chip][reg] = value;
    _chips[chip]->writeRegister(reg, value);  // R13 restarts the envelope (env_reset)
    if (_context && _context->chipRegisterWatch) [[unlikely]]
        ReportChipRegisterWrite(_context, SoundChipKind::Ay, static_cast<uint8_t>(chip), reg, value);
    RecordWrite(0xBFFD, value, reg);
}

uint8_t P2666Audio::ReadData()
{
    // YM2149_linmix.vhd O_DA: the register masked to its width; R14 / R15 leave O_DA as the last read left it
    const unsigned chip = _selected;
    const std::array<uint8_t, 16>& r = _regs[chip];
    uint8_t& oda = _oda[chip];
    switch (_addr[chip] & 0x0F)
    {
        case 0x1: case 0x3: case 0x5: case 0xD: oda = r[_addr[chip] & 0x0F] & 0x0F; break;
        case 0x6: case 0x8: case 0x9: case 0xA: oda = r[_addr[chip] & 0x0F] & 0x1F; break;
        case 0xE: case 0xF: break;
        default: oda = r[_addr[chip] & 0x0F]; break;
    }
    return oda;
}

void P2666Audio::WriteCovox(uint8_t value)
{
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _covox = value;
}

void P2666Audio::WriteFe(uint8_t value)
{
    const uint8_t bits = value & 0x18;
    if (bits == _fe)
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _fe = bits;
}

void P2666Audio::RecordWrite(uint16_t port, uint8_t value, uint8_t reg)
{
    if (_history && _history->On()) [[unlikely]]
        RecordAYWrite(_context, *_history, port, value, static_cast<uint8_t>(_selected), reg);
}

/// endregion </Ports>

/// region <Mix>

int P2666Audio::BeeperTerm() const
{
    // addiction = beeper & tapeout: + 50 for one, + 100 for both (PaE.vhd:1407-1415)
    return ((_fe & 0x10) ? 50 : 0) + ((_fe & 0x08) ? 50 : 0);
}

void P2666Audio::MixNow(uint8_t& right, uint8_t& left)
{
    unsigned r = _covox + static_cast<unsigned>(BeeperTerm()), l = r;
    for (unsigned i = 0; i < kChips; i++)
    {
        const unsigned a = kLinmix[_chips[i]->channelAmplitude(0)];
        const unsigned b = kLinmix[_chips[i]->channelAmplitude(1)];
        const unsigned c = kLinmix[_chips[i]->channelAmplitude(2)];
        r += a + b;
        l += b + c;
    }
    right = static_cast<uint8_t>(r);
    left = static_cast<uint8_t>(l);
}

void P2666Audio::Mix(bool step)
{
    // The parts' contributions to each side, at their gains (all 1: the board's integers)
    std::array<double, kParts> right{}, left{};
    for (unsigned i = 0; i < kChips; i++)
    {
        SoundChip_AY8910& chip = *_chips[i];
        if (step)
            chip.updateState(true);
        double a = kLinmix[chip.channelAmplitude(0)];
        double b = kLinmix[chip.channelAmplitude(1)];
        double c = kLinmix[chip.channelAmplitude(2)];
        if (_gainActive)
        {
            a *= _channelGain[i][0] * _partGain[i];
            b *= _channelGain[i][1] * _partGain[i];
            c *= _channelGain[i][2] * _partGain[i];
        }
        right[i] = a + b;
        left[i] = b + c;
    }
    right[2] = left[2] = _covox * (_gainActive ? _partGain[2] : 1.0);
    right[3] = left[3] = BeeperTerm() * (_gainActive ? _partGain[3] : 1.0);

    double sum[2] = {0, 0};
    for (size_t p = 0; p < kParts; p++)
    {
        sum[0] += right[p];
        sum[1] += left[p];
        const double mono = (right[p] + left[p]) * 0.5;
        _meterMin[p] = std::min(_meterMin[p], mono);
        _meterMax[p] = std::max(_meterMax[p], mono);
    }
    // std_logic_vector(7 downto 0): the sum wraps (D-11, the board as built)
    //
    // P2666-FIX D-11 (disabled, hardware-reference.md §11): a 10-bit sum scaled to the DAC would not wrap:
    //   level = std::min(sum, 607.0) * 255.0 / 607.0;
    const double level[2] = {sum[0] - 256.0 * std::floor(sum[0] / 256.0), sum[1] - 256.0 * std::floor(sum[1] / 256.0)};
    _lastRight = static_cast<uint8_t>(level[0]);
    _lastLeft = static_cast<uint8_t>(level[1]);

    for (int side = 0; side < 2; side++)
        _dc[side] += (level[side] - _dc[side]) * kDcAlpha;
    _tickRight.push_back(static_cast<float>((level[0] - _dc[0]) / 256.0));
    _tickLeft.push_back(static_cast<float>((level[1] - _dc[1]) / 256.0));

    if (_taps)
    {
        // Each part alone, mono, at the scale of the other machines' taps (its full swing is INT16_MAX), DC out
        static constexpr double kScale[kParts] = {32767.0 / 126.0, 32767.0 / 126.0, 32767.0 / 255.0, 32767.0 / 100.0};
        for (size_t p = 0; p < kParts; p++)
        {
            if (!_partTap[p])
                continue;
            const auto x = static_cast<int64_t>((right[p] + left[p]) * 0.5 * kScale[p]) * 64;
            int64_t& dc = _tapDc[p];
            _tickTap[p].push_back(static_cast<int32_t>((x - dc) / 64));
            dc += (x - dc) / 1024;
        }
    }
}

void P2666Audio::Tick()
{
    Mix(true);
}

void P2666Audio::AdvanceTo(double t)
{
    if (_suppressed)
    {
        // The generators' phase is machine state only while TTD records or replays (AudioFrameStart); otherwise the
        // output stage is off and nothing is heard
        _tickT = std::max(_tickT, t);
        return;
    }
    while (_tickT < t)
    {
        // Both chips silent at rest: every tick's chip levels are 0, only Covox and the beeper sound; the generators
        // still advance
        if (_chips[0]->silentAtRest() && _chips[1]->silentAtRest() && !_taps)
        {
            const auto ticks = static_cast<uint32_t>(std::ceil((t - _tickT) / kTickT));
            for (uint32_t k = 0; k < ticks; k++)
                Mix(false);
            for (auto& chip : _chips)
                chip->advanceSilent(ticks);
            _tickT += kTickT * ticks;
            return;
        }
        Tick();
        _tickT += kTickT;
    }
}

void P2666Audio::AudioFrameStart(bool synthesisSuppressed)
{
    const ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    const bool ttdActive = ttd != nullptr && (ttd->IsRecording() || ttd->IsReplayActive());
    _suppressed = synthesisSuppressed && !ttdActive;
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
    for (auto& ticks : _tickTap)
        ticks.clear();
    _meterMin.fill(1e30);
    _meterMax.fill(-1e30);
}

void P2666Audio::AudioFrameEnd(size_t samples)
{
    if (!_now || !_timeKnown)
        return;
    AdvanceTo(_frameStartT + static_cast<double>(_context->config.frame));
    _hadSound = false;
    static constexpr double kSwing[kParts] = {126.0, 126.0, 255.0, 100.0};
    for (size_t p = 0; p < kParts; p++)
        _partPeak[p] = _meterMax[p] >= _meterMin[p]
                           ? static_cast<float>(std::min(1.0, (_meterMax[p] - _meterMin[p]) / kSwing[p]))
                           : 0.0f;
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
            l /= static_cast<double>(count);
            r /= static_cast<double>(count);
        }
        peak = std::max({peak, std::fabs(static_cast<float>(l)), std::fabs(static_cast<float>(r))});
        _buffer[k * 2] = static_cast<int16_t>(std::clamp(l * 32767.0, -32768.0, 32767.0));
        _buffer[k * 2 + 1] = static_cast<int16_t>(std::clamp(r * 32767.0, -32768.0, 32767.0));
    }
    _hadSound = peak > 0.002f;
    for (size_t p = 0; p < kParts; p++)
    {
        if (!_partTap[p])
            continue;
        const std::vector<int32_t>& perTick = _tickTap[p];
        const size_t ticks = perTick.size();
        for (size_t k = 0; k < samples; k++)
        {
            const size_t from = ticks * k / samples, to = std::max(from + 1, ticks * (k + 1) / samples);
            int64_t sum = 0;
            size_t count = 0;
            for (size_t i = from; i < to && i < ticks; i++, count++)
                sum += perTick[i];
            _tapBuffer[p][k] =
                static_cast<int16_t>(std::clamp<int64_t>(count ? sum / static_cast<int64_t>(count) : 0, -32768, 32767));
        }
    }
}

/// endregion </Mix>

/// region <Parts and channels>

AudioSourceType P2666Audio::AudioPartType(size_t part) const
{
    static const AudioSourceType kTypes[kParts] = {AudioSourceType::AY1_All, AudioSourceType::AY2_All,
                                                   AudioSourceType::COVOX, AudioSourceType::Beeper};
    return part < kParts ? kTypes[part] : AudioSourceType::Custom;
}

std::string P2666Audio::AudioPartName(size_t part) const
{
    static const char* const kNames[kParts] = {"TS AY 1", "TS AY 2", "COVOX", "Beeper"};
    return part < kParts ? kNames[part] : std::string();
}

void P2666Audio::UpdateGainActive()
{
    _gainActive = false;
    for (double g : _partGain)
        _gainActive = _gainActive || g != 1.0;
    for (const auto& chip : _channelGain)
        for (double g : chip)
            _gainActive = _gainActive || g != 1.0;
}

void P2666Audio::SetAudioPartGain(size_t part, float gain)
{
    if (part >= kParts)
        return;
    const double g = std::clamp(static_cast<double>(gain), 0.0, 1.0);
    if (_partGain[part] == g)
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());
    _partGain[part] = g;
    UpdateGainActive();
}

void P2666Audio::AudioChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const
{
    if (type == AudioSourceType::AY1_All || type == AudioSourceType::AY2_All)
    {
        out.push_back({"a", "A (right)", true, false});
        out.push_back({"b", "B (both)", true, false});
        out.push_back({"c", "C (left)", true, false});
    }
}

void P2666Audio::SetAudioChannel(AudioSourceType type, size_t index, bool muted, float volume)
{
    const int chip = type == AudioSourceType::AY1_All ? 0 : type == AudioSourceType::AY2_All ? 1 : -1;
    if (chip < 0 || index >= 3)
        return;
    const double g = muted ? 0.0 : std::clamp(static_cast<double>(volume), 0.0, 1.0);
    double& current = _channelGain[static_cast<size_t>(chip)][index];
    if (current == g)
        return;
    if (_now && _timeKnown)
        AdvanceTo(_now());
    current = g;
    UpdateGainActive();
}

void P2666Audio::SetAudioPartTap(size_t part, bool on)
{
    if (part >= kParts)
        return;
    _partTap[part] = on;
    _tickTap[part].clear();
    _tapDc[part] = 0;
    _taps = std::any_of(_partTap.begin(), _partTap.end(), [](bool b) { return b; });
}

const int16_t* P2666Audio::AudioPartTap(size_t part) const
{
    return part < kParts && _partTap[part] ? _tapBuffer[part].data() : nullptr;
}

std::vector<std::pair<std::string, std::string>> P2666Audio::AudioStateFields() const
{
    auto hex = [](unsigned v) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02X", v & 0xFF);
        return std::string(buf);
    };
    std::vector<std::pair<std::string, std::string>> fields;
    fields.push_back({"chips", "2 x YM2149 (FPGA, linear DAC table)"});
    fields.push_back({"selected YM", std::to_string(_selected)});
    for (unsigned i = 0; i < kChips; i++)
    {
        std::string regs;
        for (unsigned r = 0; r < 14; r++)
            regs += (r ? " " : "") + hex(_regs[i][r]);
        fields.push_back({"YM " + std::to_string(i) + " regs 0-13", regs});
        fields.push_back({"YM " + std::to_string(i) + " address", hex(_addr[i])});
    }
    fields.push_back({"covox #FB", hex(_covox)});
    fields.push_back({"beeper / tape out", std::string(_fe & 0x10 ? "1" : "0") + " / " + (_fe & 0x08 ? "1" : "0")});
    fields.push_back({"DAC right / left", hex(_lastRight) + " / " + hex(_lastLeft)});
    fields.push_back({"stereo", "right A + B, left B + C, 8-bit sum wraps"});
    return fields;
}

/// endregion </Parts and channels>
