#include "frameclock.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

const char* FrameClockSourceName(FrameClockSource source)
{
    switch (source)
    {
        case FrameClockSource::Audio:
            return "audio";
        case FrameClockSource::Emulated:
            return "emulated";
        case FrameClockSource::Latch:
            return "latch";
        case FrameClockSource::None:
            break;
    }
    return "none";
}

FrameTimingResult FrameClock::Next(const FrameTimingInput& in)
{
    FrameTimingResult result;
    int64_t raw = 0;
    if (in.haveAudio)
    {
        result.source = FrameClockSource::Audio;
        raw = in.audioNs;
    }
    else if (in.haveEmulated)
    {
        result.source = FrameClockSource::Emulated;
        raw = in.emulatedNs + in.emulatedDelayNs;
    }
    else
    {
        result.source = FrameClockSource::Latch;
        raw = in.latchNs + in.latchDelayNs;
    }
    raw += in.offsetNs;
    // The screen holds a frame only so long: a sound heard later than that cannot be waited for
    int64_t limit = INT64_MAX;
    if (in.maxDelayNs > 0)
    {
        limit = (in.haveEmulated ? in.emulatedNs : in.latchNs) + in.maxDelayNs;
        if (raw > limit)
        {
            raw = limit;
            result.clamped = true;
        }
    }

    const int64_t period = in.periodNs > 0 ? in.periodNs : 20'000'000;
    const int64_t step = raw - (_lastRawNs + period);  // how far this frame's time left the cadence
    // Turbo frames have no cadence to keep, and leaving turbo starts a new one
    const bool cut = !_started || in.latchNs - _lastLatchNs > kGapPeriods * period ||
                     result.source == FrameClockSource::Latch || _lastSource == FrameClockSource::Latch ||
                     std::llabs(step) > kMaxCarryNs;

    int64_t target = raw;
    if (cut)
    {
        _carryNs = 0;
        if (_started && !(result.source == FrameClockSource::Latch && _lastSource == FrameClockSource::Latch))
            _stats.cuts++;
    }
    else
    {
        if (result.source != _lastSource || std::llabs(step) > kJumpNs)
        {
            // Carry the step: this frame keeps the cadence of the last one
            _carryNs = _lastTargetNs + period - raw;
            _stats.carried++;
        }
        if (_carryNs > kSlewNs)
            _carryNs -= kSlewNs;
        else if (_carryNs < -kSlewNs)
            _carryNs += kSlewNs;
        else
            _carryNs = 0;
        target = std::min(raw + _carryNs, limit);
        // Never out of order, never two frames at one time
        if (target < _lastTargetNs + period / 2)
            target = _lastTargetNs + period / 2;
    }

    _started = true;
    _lastSource = result.source;
    _lastRawNs = raw;
    _lastTargetNs = target;
    _lastLatchNs = in.latchNs;

    _stats.frames++;
    if (result.source == FrameClockSource::Audio)
        _stats.audioFrames++;
    else if (result.source == FrameClockSource::Emulated)
        _stats.emulatedFrames++;
    else
        _stats.latchFrames++;
    _stats.carryNs = _carryNs;
    _stats.clamped += result.clamped ? 1 : 0;
    _stats.lastClamped = result.clamped;
    _stats.source = result.source;

    result.targetNs = target;
    return result;
}

void FrameClock::Reset()
{
    _started = false;
    _carryNs = 0;
    _lastSource = FrameClockSource::None;
}
