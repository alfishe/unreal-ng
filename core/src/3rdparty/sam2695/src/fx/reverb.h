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
//
// Idle: once every line of the layout and every filter state holds +0.0, a block whose send is all
// +0.0 would compute +0.0 everywhere; Skip() then only moves the write positions and the LFO phase as
// processing would. The idle flag is derived from the state (Clear, Sanitize, and a run of +0.0 writes
// as long as the longest written line), never serialized.
//
// Tail floor: a block without input that leaves every line and filter value below kTailFloor (dsp.h)
// sets them to +0.0 - the tail ends there, as a fixed-point chip's would, and the reverb is idle.
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
    // The whole state is +0.0: a block with an all +0.0 send may Skip() instead of Process()
    bool Idle() const { return _idle; }
    void Skip(uint32_t n);
    uint64_t tailsOut = 0; // tails ended by the floor since Configure (Describe; diagnostics, not state)

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
    uint32_t LongestLine() const;
    bool FiltersZero() const;
    bool LinesZero() const;
    void TrackIdle(const float* in, uint32_t n);
    void TailOut(const float* in, uint32_t n);
    bool LinesBelowFloor();
    // f(line) for the lines of the current layout (the other layout's are size 1 at the pool's start)
    template <class F>
    void ForEachLine(F&& f)
    {
        if (_layoutCharacter >= 6)
        {
            f(_echo[0]);
            f(_echo[1]);
            return;
        }
        f(_predelay);
        for (DelayLine& l : _diffuser)
            f(l);
        for (DelayLine& l : _tank)
            f(l);
    }

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

    // derived from the state (see the header comment), not serialized
    bool _idle = false;
    uint32_t _zeroRun = 0;     // samples since the last non-zero write or filter state
    uint32_t _longestLine = 1; // of the lines the layout writes every sample
};

} // namespace sam2695
