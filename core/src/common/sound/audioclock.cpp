#include "audioclock.h"

void AudioClock::OnDeviceCallback(int64_t hostNs, uint64_t streamFrame, uint32_t frameCount, bool streamHasData,
                                  uint32_t sampleRate, uint32_t latencyFrames)
{
    if (sampleRate == 0)
        return;

    // A reset asked for by another thread: start over
    const uint32_t wanted = _resetGeneration.load(std::memory_order_acquire);
    if (wanted != _generation)
    {
        _generation = wanted;
        _device = 0;
        _callbacks = 0;
        _haveOffset = false;
    }

    if (_callbacks == 0)
    {
        _originDevice = _device;
        _originNs = hostNs;
        _weight = 0.0;
        _meanX = _meanY = _sxx = _sxy = 0.0;
    }

    // Keep the fit's numbers small: move the origin to the current mean every ~10 minutes of audio
    if (_device - _originDevice > static_cast<uint64_t>(sampleRate) * 600)
    {
        const auto shiftX = static_cast<int64_t>(_meanX);
        const auto shiftY = static_cast<int64_t>(_meanY);
        _originDevice += static_cast<uint64_t>(shiftX);
        _originNs += shiftY;
        _meanX -= static_cast<double>(shiftX);
        _meanY -= static_cast<double>(shiftY);
    }

    // Exponentially weighted least squares, the incremental (West) form: numerically stable, O(1) per callback
    const double x = static_cast<double>(_device - _originDevice);
    const double y = static_cast<double>(hostNs - _originNs);
    _weight = _weight * FORGET + 1.0;
    const double dx = x - _meanX;
    const double dy = y - _meanY;
    _meanX += dx / _weight;
    _meanY += dy / _weight;
    _sxx = _sxx * FORGET + dx * (x - _meanX);
    _sxy = _sxy * FORGET + dx * (y - _meanY);
    _callbacks++;

    // The stream's next frame is the one the device plays from this callback
    if (streamHasData)
    {
        _streamToDevice = static_cast<int64_t>(_device - streamFrame);
        _haveOffset = true;
    }
    _device += frameCount;

    const double nominal = 1e9 / sampleRate;
    double slope = _sxx > 0.0 ? _sxy / _sxx : nominal;
    if (slope < nominal * (1.0 - MAX_RATE_ERROR))
        slope = nominal * (1.0 - MAX_RATE_ERROR);
    else if (slope > nominal * (1.0 + MAX_RATE_ERROR))
        slope = nominal * (1.0 + MAX_RATE_ERROR);

    const uint32_t seq = _seq.load(std::memory_order_relaxed);
    _seq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    _pubOriginDevice.store(_originDevice, std::memory_order_relaxed);
    _pubOriginNs.store(_originNs, std::memory_order_relaxed);
    _pubMeanX.store(_meanX, std::memory_order_relaxed);
    _pubMeanY.store(_meanY, std::memory_order_relaxed);
    _pubSlope.store(slope, std::memory_order_relaxed);
    _pubStreamToDevice.store(_streamToDevice, std::memory_order_relaxed);
    _pubLatencyNs.store(static_cast<int64_t>(latencyFrames * nominal), std::memory_order_relaxed);
    _pubLastCallbackNs.store(hostNs, std::memory_order_relaxed);
    _pubGeneration.store(_generation, std::memory_order_relaxed);
    _pubValid.store(_haveOffset && _callbacks >= MIN_CALLBACKS, std::memory_order_relaxed);
    _seq.store(seq + 2, std::memory_order_release);
}

void AudioClock::Reset()
{
    _resetGeneration.fetch_add(1, std::memory_order_acq_rel);
}

bool AudioClock::ReadFit(Fit* fit) const
{
    for (int attempt = 0; attempt < 64; attempt++)
    {
        const uint32_t before = _seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const bool valid = _pubValid.load(std::memory_order_relaxed);
        fit->originDevice = _pubOriginDevice.load(std::memory_order_relaxed);
        fit->originNs = _pubOriginNs.load(std::memory_order_relaxed);
        fit->meanX = _pubMeanX.load(std::memory_order_relaxed);
        fit->meanY = _pubMeanY.load(std::memory_order_relaxed);
        fit->slope = _pubSlope.load(std::memory_order_relaxed);
        fit->streamToDevice = _pubStreamToDevice.load(std::memory_order_relaxed);
        fit->latencyNs = _pubLatencyNs.load(std::memory_order_relaxed);
        fit->lastCallbackNs = _pubLastCallbackNs.load(std::memory_order_relaxed);
        fit->generation = _pubGeneration.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (_seq.load(std::memory_order_relaxed) == before)
            return valid && fit->generation == _resetGeneration.load(std::memory_order_acquire);
    }
    return false;  // the writer kept the lock for 64 reads: treat as not valid this time
}

bool AudioClock::TimeOfStreamFrame(uint64_t streamFrame, int64_t nowNs, int64_t* hostNs) const
{
    Fit fit;
    if (!ReadFit(&fit) || nowNs - fit.lastCallbackNs > STALE_NS)
        return false;
    const int64_t device = static_cast<int64_t>(streamFrame) + fit.streamToDevice;
    const double x = static_cast<double>(device - static_cast<int64_t>(fit.originDevice));
    const double y = fit.meanY + fit.slope * (x - fit.meanX);
    if (hostNs)
        *hostNs = fit.originNs + static_cast<int64_t>(y) + fit.latencyNs;
    return true;
}

double AudioClock::FittedRate(int64_t nowNs) const
{
    Fit fit;
    if (!ReadFit(&fit) || nowNs - fit.lastCallbackNs > STALE_NS || fit.slope <= 0.0)
        return 0.0;
    return 1e9 / fit.slope;
}
