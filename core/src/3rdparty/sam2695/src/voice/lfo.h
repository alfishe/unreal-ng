// libsam2695 - SF2 triangle LFO (generators 21-24): silent for its delay, then a triangle that starts
// at 0 and rises first, peak +-1. The phase is a 32-bit fixed-point fraction of a cycle (exact, no drift).
#pragma once

#include <cstdint>

namespace sam2695
{

struct Lfo
{
    uint32_t delay = 0;     // samples before the LFO starts
    uint32_t elapsed = 0;   // samples since note-on, saturating at delay
    uint32_t phase = 0;     // cycle fraction, 2^32 = one cycle
    uint32_t increment = 0; // per sample

    void Start(uint32_t delaySamples, uint32_t inc)
    {
        delay = delaySamples;
        elapsed = 0;
        phase = 0;
        increment = inc;
    }

    float Value() const
    {
        if (elapsed < delay)
            return 0.0f;
        // triangle: 0 -> +1 at 1/4, -1 at 3/4, back to 0
        const float p = static_cast<float>(phase) * (1.0f / 4294967296.0f);
        if (p < 0.25f)
            return 4.0f * p;
        if (p < 0.75f)
            return 2.0f - 4.0f * p;
        return 4.0f * p - 4.0f;
    }

    void Advance(uint32_t n)
    {
        if (elapsed < delay)
        {
            const uint32_t wait = delay - elapsed;
            if (n <= wait)
            {
                elapsed += n;
                return;
            }
            elapsed = delay;
            n -= wait;
        }
        phase += increment * n; // modulo 2^32
    }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(delay);
        ar(elapsed);
        ar(phase);
        ar(increment);
    }
};

} // namespace sam2695
