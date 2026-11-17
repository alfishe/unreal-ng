#include "stdafx.h"

#include "vdac4audio.h"

#include <algorithm>
#include <cstring>

void Vdac4Audio::Push(const int16_t* frames, size_t count, uint32_t rate)
{
    if (!frames || count == 0 || rate == 0)
        return;
    if (rate != _inRate)
    {
        _inRate = rate;
        _phase = 0;
    }
    const double step = static_cast<double>(_inRate) / static_cast<double>(_outRate);
    for (size_t i = 0; i < count; i++)
    {
        const int16_t left = frames[i * 2];
        const int16_t right = frames[i * 2 + 1];
        // Output frames that fall between the previous input frame and this one
        while (_phase < 1.0)
        {
            const double l = _prev[0] + (left - _prev[0]) * _phase;
            const double r = _prev[1] + (right - _prev[1]) * _phase;
            _fifo.push_back(static_cast<int16_t>(l));
            _fifo.push_back(static_cast<int16_t>(r));
            _phase += step;
        }
        _phase -= 1.0;
        _prev[0] = left;
        _prev[1] = right;
    }
}

void Vdac4Audio::BusReset()
{
    _fifo.clear();
    _fifoHead = 0;
    _phase = 0;
    _prev[0] = _prev[1] = 0;
    _last[0] = _last[1] = 0;
}

void Vdac4Audio::FrameEnd(size_t samples)
{
    _buffer.resize(samples * 2);
    _hadSound = false;
    size_t available = Queued();
    const size_t take = std::min(available, samples);
    if (take > 0)
    {
        std::memcpy(_buffer.data(), _fifo.data() + _fifoHead, take * 2 * sizeof(int16_t));
        _fifoHead += take * 2;
        _last[0] = _buffer[take * 2 - 2];
        _last[1] = _buffer[take * 2 - 1];
    }
    if (take < samples)
    {
        // Underrun: hold the last level (a step to zero would click); counted once the player has played
        if (_inRate != 0)
            _underruns++;
        for (size_t i = take; i < samples; i++)
        {
            _buffer[i * 2] = _last[0];
            _buffer[i * 2 + 1] = _last[1];
        }
    }
    for (size_t i = 0; i < samples * 2 && !_hadSound; i++)
        _hadSound = _buffer[i] != 0;

    // The ESP runs ahead of the mixer by at most a frame or two: beyond that the oldest samples go
    available = Queued();
    const size_t limit = std::max<size_t>(samples, 1) * kMaxLagFrames;
    if (samples > 0 && available > limit)
    {
        const size_t drop = available - limit;
        _fifoHead += drop * 2;
        _dropped += drop;
    }
    Compact();

    _pubInRate.store(_inRate, std::memory_order_relaxed);
    _pubOutRate.store(_outRate, std::memory_order_relaxed);
    _pubQueued.store(static_cast<uint32_t>(Queued()), std::memory_order_relaxed);
    _pubUnderruns.store(_underruns, std::memory_order_relaxed);
    _pubDropped.store(_dropped, std::memory_order_relaxed);
    _pubSound.store(_hadSound, std::memory_order_relaxed);
}

Vdac4Audio::Stats Vdac4Audio::GetStats() const
{
    Stats s;
    s.inputRate = _pubInRate.load(std::memory_order_relaxed);
    s.outputRate = _pubOutRate.load(std::memory_order_relaxed);
    s.queued = _pubQueued.load(std::memory_order_relaxed);
    s.underruns = _pubUnderruns.load(std::memory_order_relaxed);
    s.dropped = _pubDropped.load(std::memory_order_relaxed);
    s.sound = _pubSound.load(std::memory_order_relaxed);
    return s;
}

void Vdac4Audio::SetOutputRate(uint32_t rate)
{
    if (rate == 0 || rate == _outRate)
        return;
    _outRate = rate;
    _fifo.clear();
    _fifoHead = 0;
    _phase = 0;
}

void Vdac4Audio::MixerRows(std::vector<CardMixerRow>& out) const
{
    out.push_back({AudioSourceType::Vdac4Esp, "VDAC4 ESP", false});
}

const int16_t* Vdac4Audio::MixerBuffer(AudioSourceType type) const
{
    return type == AudioSourceType::Vdac4Esp && !_buffer.empty() ? _buffer.data() : nullptr;
}

void Vdac4Audio::Compact()
{
    if (_fifoHead == 0)
        return;
    if (_fifoHead >= _fifo.size())
    {
        _fifo.clear();
        _fifoHead = 0;
        return;
    }
    _fifo.erase(_fifo.begin(), _fifo.begin() + static_cast<std::ptrdiff_t>(_fifoHead));
    _fifoHead = 0;
}
