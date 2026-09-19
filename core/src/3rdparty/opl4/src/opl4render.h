// libopl4 — render layer (core TDD §8). Strictly downstream of the chip
// boundary (D11): float state, never serialised, reset on restore.
//
//   chip stereo → [rate conversion] → [board analog] → [punch] → [room]
//               → [DC blocker] → out
//
// Rate conversion: runtime-designed Kaiser windowed-sinc polyphase FIR,
// the same designer math as the host's AY decimator (FirDesigner::kaiser)
// so the tonal character matches (§8.2). At (44100, Authentic, no post
// stages) the path is a bit-exact bypass (R6).
#pragma once

#include "opl4/opl4config.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace opl4
{

// Kaiser windowed-sinc lowpass, DC-normalised — same math as the host's
// fir_designer.h, standalone so the library stays dependency-free (R10).
// Bit-identical for shared parameter sets (asserted by test).
std::vector<double> DesignKaiser(size_t taps, double fc, double fs, double beta);

// Polyphase resampler / interpolator. Input and output rates are runtime
// parameters; the anti-alias / anti-image filter is designed at Configure()
// against the *output* Nyquist when interpolating (upsampling) and against
// the *input* Nyquist when decimating (§8.2 note).
class PolyphaseResampler
{
public:
    void Configure(double inputRate, double outputRate, Quality q,
                   double cutoffHz = 20000.0);
    // Push one input frame (stereo), emit 0..n output frames into out
    // (interleaved stereo). Returns frames written.
    size_t Process(float inL, float inR, float* out);
    void Reset();

    double PhaseStep() const { return _phaseStep; }
    size_t Taps() const { return _taps; }
    const std::vector<double>& Coeffs() const { return _coeffs; }

private:
    double Convolve(const double* hist, size_t phase) const;

    std::vector<double> _coeffs;
    std::vector<double> _table; // (_phases + 1) x _taps kernels, one per fractional offset
    size_t _phases = 0;
    size_t _taps = 0;
    double _phaseStep = 1.0;
    double _phase = 0.0;
    std::vector<double> _histL, _histR;   // input history ring
    std::vector<double> _orderedL, _orderedR; // scratch (allocation-free path)
    size_t _histPos = 0;
};

// YAC513 + LF347 board analog model (§8.3): 1st-order RC low-pass at
// 4.08 kHz cascaded with a 2nd-order Sallen-Key low-pass (f0 = 27.7 kHz,
// Q = 1.306). Realised as a minimum-phase FIR designed at Configure() from
// the analog magnitude up to Nyquist (cepstral folding): a bilinear biquad
// cannot represent the Sallen-Key corner above the 44.1/48 kHz Nyquist
// (its forced Nyquist zero cost up to 35 dB at 20 kHz). 64 taps match the
// analog magnitude to 0.003 dB over 0-20 kHz at 44.1..192 kHz.
class BoardAnalog
{
public:
    static constexpr size_t kTaps = 64;

    void Configure(double sampleRate);
    void Process(float& l, float& r);
    void Reset();

private:
    float _h[kTaps] = {1.0f};        // identity until configured
    float _xl[kTaps] = {}, _xr[kTaps] = {}; // history ring, newest at _pos
    size_t _pos = 0;
};

// Punch chain (§8.4): hybrid transient designer + exciter. First-difference
// tilt blended by edgeBlend plus envelope-gated transient boost, with
// coefficients rate-normalised to the 44.1 kHz presets via coeff^(44100/fs).
class CharacterChain
{
public:
    void SetPunch(PunchPreset p, double sampleRate);
    void SetRoom(RoomMode m, double sampleRate);
    void Process(float& l, float& r);
    void Reset();

    bool Active() const { return _punchOn || _roomOn; }

private:
    bool _punchOn = false;
    bool _roomOn = false;
    float _edgeBlend = 0;
    float _transBoost = 0;
    float _envAttack = 0.3f;
    float _envRelease = 0.9995f;
    float _prevL = 0, _prevR = 0;
    float _envL = 0, _envR = 0;
    // room: delayed opposite-channel bleed, one-pole lowpass
    float _roomGain = 0;
    float _roomLp = 0;
    std::vector<float> _delayL, _delayR;
    size_t _delayPos = 0;
    float _roomZL = 0, _roomZR = 0;
};

class Opl4Render
{
public:
    void Configure(const Opl4Config& cfg);
    void ResetRenderState();

    // Authentic: single chip stream (44100) through stages.
    // Returns frames written to out (interleaved stereo); *consumedFrames
    // (optional) receives how many input frames were eaten.
    size_t ProcessChip(const int32_t* chipStereo, size_t frames, float* out,
                       size_t maxOutFrames, size_t* consumedFrames = nullptr);
    // HiFi: FM at 49516.4 and PCM at 44100, resampled and summed.
    size_t ProcessSplit(const int32_t* fmStereo, size_t fmFrames,
                        const int32_t* pcmStereo, size_t pcmFrames,
                        float* out, size_t maxOutFrames,
                        size_t* consumedFm = nullptr, size_t* consumedPcm = nullptr);

    // One mixer source rendered alone (host split render, integration D5):
    // group stream resampled to the output rate and passed through that
    // group's chain, its own board-analog instance and its own DC blocker —
    // the two sources never share filter state. Unity bypass (same rate,
    // no stages) is a pure int32 -> float copy (R6 mirror).
    size_t ProcessGroup(ChannelGroup g, const int32_t* stereo, size_t frames,
                        float* out, size_t maxOutFrames,
                        size_t* consumedFrames = nullptr);

    void SetBoardAnalog(bool on) { _boardAnalogOn = on; }
    void SetPunchPreset(ChannelGroup g, PunchPreset p);
    void SetRoom(RoomMode m) { _chainFm.SetRoom(m, _outputRate); _chainPcm.SetRoom(m, _outputRate); _room = m; }

    // Unity-bypass query (R6): true when ProcessChip is a pure int16->float
    // memcpy with no filtering at all.
    bool UnityBypass() const;

    const PolyphaseResampler& MainResampler() const { return _main; }

private:
    void PostStages(float& l, float& r);

    // Group input rate: HiFi carries FM on its native 49516.4 Hz grid;
    // every other group/mode combination is on the 44100 Hz chip grid.
    double GroupInputRate(ChannelGroup g) const;
    // Unity-bypass query for one group's ProcessGroup path.
    bool GroupBypass(ChannelGroup g) const;

    // Per-group render state for ProcessGroup: each mixer source gets its
    // own board-analog and DC-blocker state so the streams stay independent.
    // Resampler outputs that did not fit the caller's buffer: one input frame
    // emits up to floor(out/in)+1 frames (4-5 at 192 kHz), so an output cap
    // can land mid-input. The tail is carried to the next call - dropping it
    // lost samples and cut the waveform at every upsampling rate.
    struct ResampleCarry
    {
        float frame[24] = {};
        size_t count = 0, pos = 0;
        void Reset() { count = pos = 0; }
    };

    struct GroupStage
    {
        BoardAnalog analog;
        ResampleCarry carry;
        float dcX1L = 0, dcX1R = 0, dcY1L = 0, dcY1R = 0;
        void Reset()
        {
            analog.Reset();
            carry.Reset();
            dcX1L = dcX1R = dcY1L = dcY1R = 0;
        }
    };
    ResampleCarry _carryMain; // ProcessChip (mixed Authentic path)

    uint32_t _outputRate = 44100;
    RenderMode _mode = RenderMode::Authentic;
    Quality _quality = Quality::Reference;
    // User character settings: survive Configure() (mode/quality changes).
    bool _boardAnalogOn = false;
    RoomMode _room = RoomMode::Off;
    PunchPreset _punchFm = PunchPreset::Off;
    PunchPreset _punchPcm = PunchPreset::Off;

    PolyphaseResampler _main;   // chip 44100 -> output
    PolyphaseResampler _fm;     // HiFi: 49516.4 -> output
    PolyphaseResampler _pcm;    // HiFi: 44100 -> output
    PolyphaseResampler _resFm;  // split source: group rate -> output
    PolyphaseResampler _resPcm; // split source: group rate -> output
    BoardAnalog _analog;
    CharacterChain _chainFm;
    CharacterChain _chainPcm;
    GroupStage _groupFm;
    GroupStage _groupPcm;

    // DC blocker (~5 Hz one-pole highpass, §8.6)
    float _dcX1L = 0, _dcX1R = 0, _dcY1L = 0, _dcY1R = 0;
    float _dcR = 0.9995f;

    // split-mode staging
    std::vector<float> _fmStage, _pcmStage;
};

} // namespace opl4
