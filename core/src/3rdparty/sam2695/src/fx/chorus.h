// libsam2695 - the chorus: the chip's eight programs (datasheet p.24) on one modulated delay line.
//
// Mono send in, stereo out: two taps read the line at base delay + depth x (1 + LFO) / 2, the right
// tap's LFO a quarter cycle behind the left one; the taps' mean feeds back into the line. The programs
// differ only in their parameters (CHR_DEL / FEED / RATE / DEPTH defaults): chorus 1-4 short modulated
// delays, the feedback chorus and the flanger with feedback, the short delay and the feedback delay
// without modulation. Parameter mapping (the datasheet gives the 7-bit ranges only):
//   delay  d: 40 ms x 2^((d - 127) / 24)    (1 ms at 0, 6.2 ms at 40h, 40 ms at 7Fh)
//   rate   r: 0.05 Hz + r x 9.95 / 127 Hz   (0.05 .. 10 Hz)
//   depth  p: p x 0.1 ms peak to peak
//   feedback f: f / 127 x 0.85
//   level  l: l / 127
//
// Idle (as the reverb, reverb.h): the chorus and the spatial effect are idle once their line holds
// only +0.0 (a run of +0.0 writes as long as the line, or Clear / Sanitize); a block with no input
// then skips them, moving the write position (and the chorus LFO) as processing would. The equalizer
// is idle when its filter memory is +0.0. Derived, never serialized.
#pragma once

#include "fx/dsp.h"
#include "fx/fxparams.h"

#include <cstdint>
#include <vector>

namespace sam2695
{

class Chorus
{
public:
    void Allocate();
    void Update(const FxParams& p);
    void Clear();
    void Process(const float* in, float* outL, float* outR, uint32_t n);
    void Sanitize(const FxParams& p);
    bool Idle() const { return _idle; }
    void Skip(uint32_t n);

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_line);
        ar(_lfoPhase);
        for (float& v : _pool)
            ar(v);
    }

private:
    std::vector<float> _pool;
    DelayLine _line;
    uint32_t _lfoPhase = 0;
    // derived
    float _base = 40.0f, _depth = 0.0f, _feedback = 0.0f, _level = 0.0f;
    uint32_t _lfoIncrement = 0;
    // idle (derived)
    bool _idle = false;
    uint32_t _zeroRun = 0;
};

// The spatial effect (datasheet p.16 and the block diagram p.42): L - R (stereo wide) or L + R
// (mono to pseudo stereo) through a delay, scaled by the volume, added to the left and subtracted
// from the right. Delay v: 0.25 ms + v x 0.15 ms (4.6 ms at the default 1Dh); volume v / 127.
class Spatial
{
public:
    void Allocate();
    void Update(const FxParams& p);
    void Clear();
    void Process(float* left, float* right, uint32_t n);
    void Sanitize(const FxParams& p);
    bool Idle() const { return _idle; }
    void Skip(uint32_t n) { _line.Advance(n); }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_line);
        for (float& v : _pool)
            ar(v);
    }

private:
    std::vector<float> _pool;
    DelayLine _line;
    uint32_t _delay = 1;
    float _volume = 0.0f;
    bool _mono = false;
    // idle (derived)
    bool _idle = false;
    uint32_t _zeroRun = 0;
};

// The 4-band stereo equalizer (NRPN 3700h-370Bh, datasheet p.18): a low shelf, two peaking bands
// (Q 0.707, the DreamBlaster X16's documented default) and a high shelf, RBJ cookbook biquads.
// Gains 00h = -12 dB, 40h = 0 dB, 3/16 dB per step; corners linear over 0-4.7 kHz (low), 0-4.2 kHz
// (the mid bands), 0-18.75 kHz (high), held inside 20 Hz .. 0.45 fs. The 2-band mode keeps the shelves.
class Equalizer
{
public:
    void Update(const FxParams& p);
    void Clear();
    void Process(float* left, float* right, uint32_t n, bool fourBand);
    // The memory of the bands in use is +0.0 (a block of +0.0 then leaves it and the signal as they are)
    bool Idle(bool fourBand) const;

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_state);
    }

    static double BandFrequency(int band, uint8_t value);
    static double BandGainDb(uint8_t value);

private:
    std::array<Biquad, 4> _band{};
    std::array<double, 16> _state{}; // [channel][band][s1, s2]
};

} // namespace sam2695
