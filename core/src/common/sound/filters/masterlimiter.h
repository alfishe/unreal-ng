#pragma once

#include <cstddef>

/// Master bus DC blocker + soft limiter (MoonSound integration design 5.2/D7).
///
/// Two stages:
///  1. One-pole DC blocker (~5 Hz, rate-compensated in Configure) so a source
///     stuck at DC cannot eat the limiter's headroom.
///  2. Static piecewise transfer curve: exactly linear below the knee,
///     smoothly compressive above it (unit slope at the knee), with a hard
///     asymptotic ceiling just below full scale that no finite input can
///     reach - the master mix cannot clip.
///
/// The curve is memoryless on purpose: gain never falls below unity, so a
/// percussive hit passes with only instantaneous soft compression above the
/// knee and cannot be ducked or pumped ("attack and release ... so that a
/// percussive PCM hit is not audibly ducked" - no attack/release state means
/// nothing to tune and nothing to duck with). Memoryless also keeps the
/// output replay-deterministic for TTD.
///
/// Samples are int16-scaled floats (32768.0f == +FS, matching the mix bus).
/// While the wide mix bus stays under the knee the output equals the
/// DC-blocked input. R6 (MoonSound absent must not change a single sample)
/// is enforced by the caller: SoundManager keeps the legacy integer path
/// whenever the wide path is not enabled.
class MasterLimiter
{
public:
    /// Linear-region ceiling: 0.75 FS (-2.5 dBFS). Below it the curve is the
    /// identity - nominal mixes never touch the compressor.
    static constexpr float KNEE_LINEAR = 24576.0f;

    /// Asymptotic magnitude ceiling: ~0.9766 FS. Leaves ~2% intersample
    /// headroom for the DRC resampler that follows the mix.
    static constexpr float CEILING = 32000.0f;

    /// Derives the DC-blocker pole for the given rate (keeps the ~5 Hz cutoff
    /// constant in Hz across core rates, same approach as Covox).
    void Configure(double sampleRate);

    /// Clears DC-blocker state only (the transfer curve has no state).
    void Reset();

    /// Processes interleaved stereo in place, `frames` stereo pairs.
    void Process(float* interleavedStereo, size_t frames);

    /// Memoryless transfer curve for a single sample (DC blocker not
    /// applied). Public for tests and gain-staging documentation.
    static float LimitSample(float x);

private:
    // One-pole highpass y = x - x1 + R * y1, per channel
    float _dcR = 0.99928843f;  // 5 Hz pole at 44100 Hz; Configure() re-derives
    float _dcX1L = 0.0f;
    float _dcY1L = 0.0f;
    float _dcX1R = 0.0f;
    float _dcY1R = 0.0f;
};
