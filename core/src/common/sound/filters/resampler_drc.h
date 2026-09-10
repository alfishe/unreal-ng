#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

/// Dynamic-rate-control resampler (audio-sync design, Fix 2 stage).
///
/// Final output stage between the mixed CORE_RATE stream and the audio
/// device: produces output at ratio = (DEV_RATE/CORE_RATE) * (1 + trim),
/// where trim is the PI controller's +-0.5% max nudge. Input is already
/// band-limited (blip_buf / decimation filters), so 4-tap cubic Hermite
/// (Catmull-Rom) interpolation is sufficient: at ratios within 0.5% of
/// unity the interpolation error is below -90 dBFS.
///
/// Invariants (see design doc 5.2):
/// - _phase and the input-history carry PERSIST across frames. Resetting
///   either at frame boundaries produces a 48.8 Hz discontinuity.
/// - Boundary rule: an output sample needs input[i-1..i+2]; the last
///   HISTORY_FRAMES input frames are carried so no frame-edge sample is
///   interpolated against zeros or stale data.
/// - Unity bypass: at ratio exactly 1.0 the stage is a memcpy -
///   bit-identical to not having the stage at all. This keeps the default
///   configuration (CORE==DEV rate, controller disengaged) bit-exact and
///   keeps the RECORDING path assumption intact (recording taps UPSTREAM
///   of this stage and must never see trimmed audio).
///
/// Threading: emulation thread only. Not reentrant.
class ResamplerDRC
{
public:
    static constexpr size_t HISTORY_FRAMES = 3;  // Carried input stereo frames

    void reset()
    {
        _phase = 0.0;
        _carryCount = 0;
        memset(_carry, 0, sizeof(_carry));
    }

    void setRatio(double outputPerInput)
    {
        _ratio = outputPerInput;
        _bypass = std::abs(outputPerInput - 1.0) < 1e-12;
    }

    double getRatio() const { return _ratio; }
    bool isBypass() const { return _bypass; }

    /// Resample one frame of interleaved stereo int16.
    /// @param in Input stereo stream (frames*2 int16 values)
    /// @param inFrames Input length in stereo frames
    /// @param out Output buffer (interleaved stereo)
    /// @param outCapacityFrames Output capacity in stereo frames
    /// @return Output length in stereo frames
    size_t process(const int16_t* in, size_t inFrames, int16_t* out, size_t outCapacityFrames)
    {
        if (inFrames == 0)
            return 0;

        if (_bypass)
        {
            // Bit-exact path. Flush any carried samples from a previous
            // non-bypass period first so no audio is lost on transition.
            size_t produced = 0;
            for (size_t i = 0; i < _carryCount && produced < outCapacityFrames; i++, produced++)
            {
                out[produced * 2] = _carry[i * 2];
                out[produced * 2 + 1] = _carry[i * 2 + 1];
            }
            _carryCount = 0;
            _phase = 0.0;

            size_t copyFrames = std::min(inFrames, outCapacityFrames - produced);
            memcpy(out + produced * 2, in, copyFrames * 2 * sizeof(int16_t));
            return produced + copyFrames;
        }

        // Conceptual input = carry (up to HISTORY_FRAMES) + current frame.
        // An output at integer position i needs input[i-1 .. i+2], so valid
        // i range is [1, totalFrames-3]. The trailing samples roll into the
        // carry for the next call - nothing is interpolated against the
        // frame edge (design doc 5.2 boundary rule).
        const size_t total = _carryCount + inFrames;
        if (total < 4)
        {
            // Not enough context yet (only right after reset): accumulate
            appendCarry(in, inFrames);
            return 0;
        }

        auto sampleAt = [&](size_t idx, int ch) -> float {
            if (idx < _carryCount)
                return static_cast<float>(_carry[idx * 2 + ch]);
            return static_cast<float>(in[(idx - _carryCount) * 2 + ch]);
        };

        const double step = 1.0 / _ratio;  // Input frames per output frame
        size_t produced = 0;

        while (produced < outCapacityFrames)
        {
            const size_t i = static_cast<size_t>(_phase);
            if (i + 2 >= total || i < 1)
            {
                if (i < 1)
                {
                    _phase += step;
                    continue;
                }
                break;  // Need more input - carry the tail
            }

            const float t = static_cast<float>(_phase - static_cast<double>(i));
            for (int ch = 0; ch < 2; ch++)
            {
                const float xm1 = sampleAt(i - 1, ch);
                const float x0 = sampleAt(i, ch);
                const float x1 = sampleAt(i + 1, ch);
                const float x2 = sampleAt(i + 2, ch);

                // Catmull-Rom
                const float a = 0.5f * (3.0f * (x0 - x1) - xm1 + x2);
                const float b = x1 + x1 + xm1 - 0.5f * (5.0f * x0 + x2);
                const float c = 0.5f * (x1 - xm1);
                float v = ((a * t + b) * t + c) * t + x0;

                v = std::clamp(v, -32768.0f, 32767.0f);
                out[produced * 2 + ch] = static_cast<int16_t>(std::lrintf(v));
            }
            produced++;
            _phase += step;
        }

        // Roll the tail into the carry: keep the last HISTORY_FRAMES input
        // frames and rebase _phase onto the new conceptual stream
        const size_t keepFrom = (total >= HISTORY_FRAMES) ? total - HISTORY_FRAMES : 0;
        int16_t newCarry[HISTORY_FRAMES * 2];
        size_t newCount = total - keepFrom;
        for (size_t i = 0; i < newCount; i++)
        {
            newCarry[i * 2] = static_cast<int16_t>(sampleAt(keepFrom + i, 0));
            newCarry[i * 2 + 1] = static_cast<int16_t>(sampleAt(keepFrom + i, 1));
        }
        memcpy(_carry, newCarry, newCount * 2 * sizeof(int16_t));
        _carryCount = newCount;
        _phase -= static_cast<double>(keepFrom);
        if (_phase < 0.0)
            _phase = 0.0;

        return produced;
    }

private:
    void appendCarry(const int16_t* in, size_t inFrames)
    {
        size_t space = HISTORY_FRAMES - _carryCount;
        size_t copy = std::min(space, inFrames);
        memcpy(_carry + _carryCount * 2, in, copy * 2 * sizeof(int16_t));
        _carryCount += copy;
    }

    double _phase = 0.0;    // Fractional read position within the conceptual stream
    double _ratio = 1.0;    // Output frames per input frame
    bool _bypass = true;
    int16_t _carry[HISTORY_FRAMES * 2] = {0};
    size_t _carryCount = 0;
};
