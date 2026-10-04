// libsam2695 - the reverb: the chip's eight programs (datasheet p.23).
//
// Programs 0-5 (room1, room2, room3, hall1, hall2, plate) run one algorithm, J. Dattorro's plate
// ("Effect Design, Part 1", JAES 1997): predelay, input band limit, four input diffusers, then a
// figure-eight tank of two modulated all-pass / delay / damping / all-pass / delay halves, with
// fourteen output taps for a decorrelated stereo pair. Each program sets the tank size, the decay time
// at REV_TIME = 7Fh, the predelay, the input bandwidth and the damping; REV_TIME scales the decay
// time. Programs 6 (delay) and 7 (pan delay: echoes alternate left and right) are a feedback delay
// whose time is REV_TIME and feedback REV_FEED. The two layouts share one pool; changing the
// algorithm (the GS "character") clears it, changing time / feedback / level does not.
#pragma once

#include "fx/dsp.h"
#include "fx/fxparams.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sam2695
{

class Reverb
{
public:
    void Allocate();                      // outside the audio path (Configure)
    void Update(const FxParams& p);       // derive the coefficients; relayout + clear on a new character
    void Clear();
    // Mono send in, stereo return added to outL / outR (scaled by the program level)
    void Process(const float* in, float* outL, float* outR, uint32_t n);

    // Seconds for the tail to fall 60 dB at the current settings (tank programs), for tests and Describe
    double DecaySeconds() const { return _rt60; }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_layoutCharacter);
        ar(_predelay);
        ar(_diffuser);
        ar(_tank);
        ar(_echo);
        ar(_bandState);
        ar(_damp);
        ar(_echoDamp);
        ar(_lfoPhase);
        for (float& v : _pool)
            ar(v);
    }

    // After a state load: a line that does not fit the pool, or a layout that does not match the
    // loaded character, resets the reverb (a blob is not trusted).
    void Sanitize(const FxParams& p);

private:
    enum Line : int
    {
        Ap1,
        D1,
        Ap2,
        D2,
        Ap3,
        D3,
        Ap4,
        D4,
        TankLines
    };
    void Layout(uint8_t character);

    std::vector<float> _pool;
    uint8_t _layoutCharacter = 0xFF;
    DelayLine _predelay;
    std::array<DelayLine, 4> _diffuser{};
    std::array<DelayLine, TankLines> _tank{};
    std::array<DelayLine, 2> _echo{};      // delay programs: left, right
    float _bandState = 0.0f;
    std::array<float, 2> _damp{};
    std::array<float, 2> _echoDamp{};
    uint32_t _lfoPhase = 0;

    // derived (Update), not state
    bool _echoMode = false, _panEcho = false;
    uint32_t _predelayLength = 1;
    std::array<uint32_t, 4> _diffLength{};
    std::array<uint32_t, TankLines> _length{};
    std::array<std::array<uint32_t, 7>, 2> _tap{};
    float _excursion = 0.0f;
    uint32_t _lfoIncrement = 0;
    float _bandwidth = 1.0f, _damping = 0.0f, _decay = 0.5f, _decayDiffusion2 = 0.5f;
    float _inDiffusion1 = 0.75f, _inDiffusion2 = 0.625f;
    uint32_t _echoLength = 1;
    float _echoFeedback = 0.0f;
    float _level = 0.0f;
    double _rt60 = 0.0;
};

} // namespace sam2695
