#pragma once

#include "dsd_types.h"
#include "delta_sigma_modulator.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

/// @brief Native-rate to DSD converter for TurboSound capture
///
/// Takes the AY chip's native generator-rate output (PSG_CLOCK_RATE / 8,
/// ~218.75 kHz) and converts directly to DSD — bypassing the lossy
/// decimation to 44.1 kHz that the normal audio path performs.
///
/// Resampling 218750 Hz → 2822400 Hz (DSD64) is a ratio of exactly 8064/625,
/// handled by an integer phase accumulator (zero drift) with linear
/// interpolation between input samples. The images this leaves sit above
/// ~109 kHz, inside DSD's shaped-noise region.
///
/// Optional punch (soft saturation) is applied in the native-rate domain,
/// where its harmonics stay below Nyquist (~109 kHz) instead of aliasing.
class NativeDSDConverter
{
public:
    /// @param inputSampleRate Native input rate in Hz (e.g., 218750)
    /// @param dsdRate Target DSD rate
    /// @param channels Channel count (2 for TurboSound stereo)
    /// @param modulatorOrder Delta-sigma modulator order
    NativeDSDConverter(uint32_t inputSampleRate,
                       DSDRate dsdRate = DSDRate::DSD128,
                       uint8_t channels = 2,
                       ModulatorOrder modulatorOrder = ModulatorOrder::Fifth);

    ~NativeDSDConverter() = default;

    /// Reset converter state
    void Reset();

    /// Set input gain in dB (default -6 dB: two summed AY chips can reach 2.0)
    void SetInputGain(double gainDb);

    /// Enable/disable punch (soft saturation) in the native-rate domain
    void SetPunchEnabled(bool enabled) { _punchEnabled = enabled; }

    /// Punch amount (0.0 = clean, 1.0 = aggressive)
    void SetPunchAmount(double amount) { _punchAmount = std::clamp(amount, 0.0, 1.0); }

    /// Convert native-rate frames to DSD
    /// @param frames Interleaved float frames at the native input rate
    /// @param frameCount Number of frames (not total samples)
    /// @param dsdOutput Output: DSD bytes, byte-interleaved by channel
    ///                  [ch0 ch1 ch0 ch1 ...], MSB-first within each byte
    /// @return Number of DSD bytes appended to dsdOutput
    size_t Process(const float* frames, size_t frameCount, std::vector<uint8_t>& dsdOutput);

    /// Flush remaining partial bytes (zero-padded). Call once before Close.
    size_t Flush(std::vector<uint8_t>& dsdOutput);

    const DSDStats& GetStats() const { return _stats; }
    uint32_t GetDSDSampleRate() const { return ::GetDSDSampleRate(_dsdRate); }
    uint8_t GetChannels() const { return _channels; }

private:
    float ApplyPunch(float sample) const;

    /// Emit one DSD bit for every channel; flush byte-columns when full
    void EmitBits(const double* channelSamples, std::vector<uint8_t>& dsdOutput);

    DSDRate _dsdRate;
    uint8_t _channels;
    uint32_t _inputRate;
    uint32_t _outputRate;

    // One modulator per channel
    std::vector<std::unique_ptr<DeltaSigmaModulator>> _modulators;

    // Rational resampler state: next output bit position relative to the
    // current input interval, scaled by _outputRate. Bit emitted while
    // _phase < _outputRate; advances by _inputRate per bit.
    uint64_t _phase = 0;

    // Previous input frame (per channel) for linear interpolation
    std::vector<double> _prevSample;
    bool _hasPrev = false;

    // Per-channel bit packing state (MSB first)
    std::vector<uint8_t> _currentByte;
    uint32_t _bitIndex = 0;  // 0..7, shared across channels (same bit clock)

    // Processing parameters
    double _inputGain = 0.5;  // -6 dB default
    bool _punchEnabled = false;
    double _punchAmount = 0.3;

    DSDStats _stats;
};
