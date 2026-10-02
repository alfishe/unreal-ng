#include "stdafx.h"

#include "covoxblaster.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "3rdparty/blip_buf/blip_buf.h"
#include "emulator/emulatorcontext.h"

CovoxBlaster::CovoxBlaster(EmulatorContext* context, size_t sampleRate) : _context(context), _sampleRate(sampleRate)
{
    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    AudioSetSampleRate(sampleRate);
    for (uint16_t& entry : _s.ring)
        entry = 0x8000;
    Reset();
}

CovoxBlaster::~CovoxBlaster()
{
    blip_delete(_blipL);
    blip_delete(_blipR);
}

void CovoxBlaster::Reset()
{
    // CBL_XX: MAME machine_reset (the PLD has no reset term; a new configuration starts at 0);
    // CBL_R[15] presets and [14..0] clear on /RESET: the DAC rests at #8000
    _s.control = 0;
    _s.cnt = 0;
    _s.wa = 0;
    _s.cbd = 0;
    _s.waeFlip = 1;
    _s.intPending = 0;
    _s.levelL = 0x8000;
    _s.levelR = 0x8000;
    _lastL = 0;
    _lastR = 0;
    if (_blipL)
        blip_clear(_blipL);
    if (_blipR)
        blip_clear(_blipR);
}

void CovoxBlaster::RestoreState(const CovoxBlasterState& state)
{
    _s = state;
    _lastL = Amplitude(_s.levelL);
    _lastR = Amplitude(_s.levelR);
    blip_clear(_blipL);
    blip_clear(_blipR);
    blip_add_delta(_blipL, 0, _lastL);
    blip_add_delta(_blipR, 0, _lastR);
}

double CovoxBlaster::RateHz(uint8_t control)
{
    if (!(control & kControlCbl))
        return 0.0;
    return 3500000.0 / static_cast<double>(TickTstates(control));
}

uint8_t CovoxBlaster::EffectiveWriteIndex() const
{
    if (!(_s.control & kControlCbl) || !(_s.control & kControlInt))
        return 0;  // CBL_WA clears while CBL or its INT is off
    if (_s.intPending)
        return static_cast<uint8_t>(~_s.cnt & 0x80);  // held at the start of the half to fill
    return _s.wa;
}

/// region <Timeline>

void CovoxBlaster::Advance(uint32_t t)
{
    if (t < _s.nextTick)
        return;

    if (!(_s.control & kControlCbl))
    {
        // CBL_CTX keeps counting with CBL off (only CBL_CNT is held at 0): skip the ticks in one step
        const uint32_t period = TickTstates(_s.control);
        const uint32_t ticks = (t - _s.nextTick) / period + 1;
        _s.nextTick += ticks * period;
        return;
    }

    const bool stereo = (_s.control & kControlStereo) != 0;
    const bool intOn = (_s.control & kControlInt) != 0;
    while (_s.nextTick <= t)
    {
        const uint32_t tick = _s.nextTick;
        const uint8_t before = _s.cnt;
        _s.cnt = static_cast<uint8_t>(_s.cnt + (stereo ? 2 : 1));
        _s.ticks++;
        // CBL_INT: clocked by CNT bit 6 falling (#7F -> #80, #FF -> #00), every half of the ring
        if (intOn && (before & 0x40) && !(_s.cnt & 0x40) && !_s.intPending)
        {
            _s.intPending = 1;
            _s.intRequests++;
        }
        OutputFromRing(tick);
        _s.nextTick += TickTstates(_s.control);
    }
}

void CovoxBlaster::EndFrame(uint32_t frameLength, size_t samples)
{
    if (frameLength > 0)
        Advance(frameLength - 1);

    if (!_synthesisSuppressed && _blipL && _blipR && frameLength > 0)
    {
        blip_end_frame(_blipL, frameLength * _host);
        blip_end_frame(_blipR, frameLength * _host);
        const int count = static_cast<int>(std::min<size_t>(samples, MAX_SAMPLES_PER_FRAME));
        int readL = count > 0 ? blip_read_samples(_blipL, &_buffer[0], count, 1) : 0;
        int readR = count > 0 ? blip_read_samples(_blipR, &_buffer[1], count, 1) : 0;
        // A shortfall (right after a clear) holds the level instead of dropping to 0
        for (int i = std::max(readL, 0); i < count; i++)
            _buffer[i * 2] = static_cast<int16_t>(std::clamp(_lastL, -32768, 32767));
        for (int i = std::max(readR, 0); i < count; i++)
            _buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(_lastR, -32768, 32767));
        if (count == 0)
        {
            // Nothing is consumed this frame: drop what was rendered so the stream does not lag
            blip_clear(_blipL);
            blip_clear(_blipR);
            blip_add_delta(_blipL, 0, _lastL);
            blip_add_delta(_blipR, 0, _lastR);
        }
    }

    // Rebase to the next frame (an instruction that ran past the end already took its ticks)
    _s.nextTick = _s.nextTick >= frameLength ? _s.nextTick - frameLength : 0;
}

/// endregion </Timeline>

/// region <Bus side>

void CovoxBlaster::WriteControl(uint32_t t, uint8_t value)
{
    Advance(t);
    _s.control = value;

    const bool cblOn = (value & kControlCbl) != 0;
    const bool intOn = (value & kControlInt) != 0;
    if (!intOn)
        _s.intPending = 0;  // CBL_INT presets (no request) while the INT is off
    if (!cblOn)
    {
        // CBL_CNT clears: a fall of bit 6 clocks CBL_INT (the PLD's flip-flop does not care why)
        if ((_s.cnt & 0x40) && intOn && !_s.intPending)
        {
            _s.intPending = 1;
            _s.intRequests++;
        }
        _s.cnt = 0;
    }
    if (!cblOn || !intOn)
        _s.wa = 0;

    if (cblOn)
        OutputFromRing(t);  // CBL_R follows the ring from the next clock
    // CBL off: CBL_R holds its last word until a Covox write
}

void CovoxBlaster::WriteData(uint32_t t, uint8_t value, uint8_t addrHigh)
{
    Advance(t);

    const bool cblOn = (_s.control & kControlCbl) != 0;
    const bool intOn = (_s.control & kControlInt) != 0;
    const bool mode16 = (_s.control & kControl16Bit) != 0;

    // CBL_WA: cleared while CBL or its INT is off, held at the start of the other half while a request is pending
    const bool held = !cblOn || !intOn || _s.intPending;
    if (!cblOn || !intOn)
        _s.wa = 0;
    else if (_s.intPending)
        _s.wa = static_cast<uint8_t>(~_s.cnt & 0x80);
    if (_s.intPending)
        _s.waeFlip = 1;  // the flip-flop presets while the request is pending

    // 16-bit: the first byte waits in CBD (CBL_WAE = 1), the second writes the entry
    const bool lowByte = mode16 && _s.waeFlip;
    if (lowByte)
    {
        _s.cbd = value;
    }
    else
    {
        const uint8_t address = static_cast<uint8_t>((intOn ? 0 : static_cast<uint8_t>(~addrHigh)) ^ _s.wa);
        const uint16_t entry = static_cast<uint16_t>(((value ^ (mode16 ? 0x80 : 0x00)) << 8) | (mode16 ? _s.cbd : 0x00));
        _s.ring[address] = entry;
        _s.ringWrites++;
        if (!held)
            _s.wa++;
        if (cblOn)
        {
            // The entry being played is heard at once
            const bool stereo = (_s.control & kControlStereo) != 0;
            if ((stereo ? (_s.cnt & 0xFE) : _s.cnt) == (stereo ? (address & 0xFE) : address))
                OutputFromRing(t);
        }
    }
    _s.waeFlip = _s.intPending ? 1 : static_cast<uint8_t>(!lowByte);

    if (!cblOn)
    {
        // Covox: CBL_R = D << 8, both channels
        _s.covoxWrites++;
        SetLevels(t, static_cast<uint16_t>(value << 8), static_cast<uint16_t>(value << 8));
    }
}

uint8_t CovoxBlaster::ApplyFeBits(uint32_t t, uint8_t keyboard)
{
    if (!(_s.control & kControlCbl))
        return keyboard;
    Advance(t);
    uint8_t value = static_cast<uint8_t>(keyboard & ~0xA0);
    if (t / kLineTstates >= kBelowPictureLine)
        value |= 0x20;
    value |= static_cast<uint8_t>((_s.cnt ^ EffectiveWriteIndex()) & 0x80);
    return value;
}

void CovoxBlaster::Acknowledge(uint32_t t)
{
    if (!_s.intPending)
        return;
    Advance(t);
    _s.wa = static_cast<uint8_t>(~_s.cnt & 0x80);
    _s.intPending = 0;
}

/// endregion </Bus side>

/// region <Output>

void CovoxBlaster::OutputFromRing(uint32_t t)
{
    if (_s.control & kControlStereo)
        SetLevels(t, _s.ring[_s.cnt & 0xFE], _s.ring[_s.cnt | 0x01]);
    else
        SetLevels(t, _s.ring[_s.cnt], _s.ring[_s.cnt]);
}

void CovoxBlaster::SetLevels(uint32_t t, uint16_t left, uint16_t right)
{
    _s.levelL = left;
    _s.levelR = right;
    if (_synthesisSuppressed)
        return;

    const int32_t newL = Amplitude(left);
    const int32_t newR = Amplitude(right);
    const unsigned time = t * _host;
    if (newL != _lastL)
    {
        blip_add_delta(_blipL, time, newL - _lastL);
        _frameHadSound = true;
    }
    if (newR != _lastR)
    {
        blip_add_delta(_blipR, time, newR - _lastR);
        _frameHadSound = true;
    }
    _lastL = newL;
    _lastR = newR;
}

void CovoxBlaster::AudioFrameStart(bool synthesisSuppressed)
{
    _hadSoundLastFrame = _frameHadSound;
    _frameHadSound = false;

    uint32_t host = 1;
    if (_context)
    {
        host = _context->emulatorState.HostSpeedMultiplier();
        host = host ? host : 1;
    }
    _host = host;

    if (synthesisSuppressed != _synthesisSuppressed)
    {
        _synthesisSuppressed = synthesisSuppressed;
        if (!synthesisSuppressed)
        {
            // Resume from the current levels
            blip_clear(_blipL);
            blip_clear(_blipR);
            _lastL = Amplitude(_s.levelL);
            _lastR = Amplitude(_s.levelR);
            blip_add_delta(_blipL, 0, _lastL);
            blip_add_delta(_blipR, 0, _lastR);
        }
    }
}

void CovoxBlaster::AudioFrameEnd(size_t samples)
{
    const uint32_t frame = _context ? _context->config.frame : 0;
    EndFrame(frame, samples);
}

void CovoxBlaster::AudioSetSampleRate(size_t rate)
{
    _sampleRate = rate;
    blip_set_rates(_blipL, static_cast<double>(CPU_CLOCK_RATE), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(CPU_CLOCK_RATE), static_cast<double>(_sampleRate));
    blip_clear(_blipL);
    blip_clear(_blipR);
    blip_add_delta(_blipL, 0, _lastL);
    blip_add_delta(_blipR, 0, _lastR);
}

std::vector<std::pair<std::string, std::string>> CovoxBlaster::AudioStateFields() const
{
    char buf[64];
    auto hex = [&buf](unsigned value, int digits) {
        std::snprintf(buf, sizeof(buf), "0x%0*X", digits, value);
        return std::string(buf);
    };
    const bool on = (_s.control & kControlCbl) != 0;
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("machine", "Sprinter: Covox / Covox-Blaster, one 16-bit stereo DAC with the AY");
    fields.emplace_back("control", hex(_s.control, 2));
    fields.emplace_back("mode", on ? "covox-blaster" : "covox");
    if (on)
    {
        std::snprintf(buf, sizeof(buf), "%.4f", RateHz(_s.control));
        fields.emplace_back("rate_hz", buf);
        fields.emplace_back("stereo", (_s.control & kControlStereo) ? "true" : "false");
        fields.emplace_back("bits", (_s.control & kControl16Bit) ? "16" : "8");
        fields.emplace_back("int_enabled", (_s.control & kControlInt) ? "true" : "false");
        fields.emplace_back("play_index", hex(_s.cnt, 2));
        fields.emplace_back("write_index", hex(EffectiveWriteIndex(), 2));
        fields.emplace_back("int_pending", _s.intPending ? "true" : "false");
    }
    fields.emplace_back("dac_left", hex(_s.levelL, 4));
    fields.emplace_back("dac_right", hex(_s.levelR, 4));
    fields.emplace_back("see", "GET /state/sprinter: sound.covox_blaster (the full state)");
    return fields;
}

/// endregion </Output>
