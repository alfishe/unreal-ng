#pragma once

#include "dsd_types.h"
#include "delta_sigma_modulator.h"
#include <algorithm>
#include <vector>
#include <memory>

/// High-quality PCM to DSD converter with upsampling and noise shaping
/// Designed for TurboSound capture with punch and room processing
class PCMToDSDConverter
{
public:
    /// @param inputSampleRate PCM input sample rate (e.g., 44100, 48000)
    /// @param dsdRate Target DSD rate (DSD64, DSD128, etc.)
    /// @param channels Number of audio channels
    /// @param modulatorOrder Delta-sigma modulator order
    PCMToDSDConverter(uint32_t inputSampleRate,
                      DSDRate dsdRate = DSDRate::DSD64,
                      uint8_t channels = 2,
                      ModulatorOrder modulatorOrder = ModulatorOrder::Fifth);

    ~PCMToDSDConverter() = default;

    /// Reset converter state (call between tracks)
    void Reset();

    /// Set input gain in dB (e.g., -9.0 for room level)
    void SetInputGain(double gainDb);

    /// Enable/disable soft saturation for punch
    void SetPunchEnabled(bool enabled) { _punchEnabled = enabled; }

    /// Set punch amount (0.0 = clean, 1.0 = aggressive saturation)
    void SetPunchAmount(double amount) { _punchAmount = std::clamp(amount, 0.0, 1.0); }

    /// Process PCM samples to DSD
    /// @param pcmInput Interleaved PCM samples (int16_t)
    /// @param sampleCount Number of sample frames (not total samples)
    /// @param dsdOutput Output buffer for DSD data
    /// @return Number of DSD bytes written
    size_t Process(const int16_t* pcmInput, size_t sampleCount, std::vector<uint8_t>& dsdOutput);

    /// Process PCM samples (float input, already normalized)
    size_t ProcessFloat(const float* pcmInput, size_t sampleCount, std::vector<uint8_t>& dsdOutput);

    /// Get required output buffer size for given PCM sample count
    size_t GetOutputSize(size_t pcmSampleCount) const;

    /// Get statistics
    const DSDStats& GetStats() const { return _stats; }

    /// Get DSD sample rate
    uint32_t GetDSDSampleRate() const { return ::GetDSDSampleRate(_dsdRate); }

    /// Get channel count
    uint8_t GetChannels() const { return _channels; }

private:
    // Upsampling filter (polyphase FIR for 2x stages); each stage keeps
    // its own delay line per channel
    void Upsample2x(const float* input, float* output, size_t sampleCount, size_t stage, size_t channel);

    // Apply punch/saturation
    float ApplyPunch(float sample) const;

    DSDRate _dsdRate;
    uint8_t _channels;
    uint32_t _oversampleRatio;

    // One modulator per channel
    std::vector<std::unique_ptr<DeltaSigmaModulator>> _modulators;

    // Upsampling stages (each does 2x)
    uint32_t _upsampleStages;
    std::vector<std::vector<float>> _upsampleBuffers;

    // Polyphase filter coefficients for 2x upsampling
    std::vector<float> _upsampleFilter;
    static constexpr size_t UPSAMPLE_FILTER_TAPS = 48;

    // Filter delay lines per channel per stage
    std::vector<std::vector<std::vector<float>>> _filterStates;

    // Processing parameters
    double _inputGain = 1.0;        // Linear gain
    bool _punchEnabled = false;
    double _punchAmount = 0.3;

    // Statistics
    DSDStats _stats;
};
