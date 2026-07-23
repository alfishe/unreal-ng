#pragma once

#include "dsd_types.h"
#include <vector>
#include <cmath>

/// Delta-sigma modulator for PCM to DSD conversion (orders 1/2/3/5)
/// CIFF topology (Chain of Integrators, Feed-Forward) with resonator
/// notches; 5th-order coefficients follow Reefman/Janssen's SACD SDM.
/// States are hard-clamped so overload degrades gracefully instead of
/// diverging.
class DeltaSigmaModulator
{
public:
    DeltaSigmaModulator(ModulatorOrder order = ModulatorOrder::Fifth,
                        uint32_t oversampleRatio = 64);
    ~DeltaSigmaModulator() = default;

    /// Reset modulator state (call between tracks or on discontinuity)
    void Reset();

    /// Process a single PCM sample, output DSD bits
    /// @param pcmSample Input sample normalized to [-1.0, +1.0]
    /// @param dsdBits Output buffer for DSD bits (1 bit per sample)
    /// @param numBits Number of DSD bits to generate (= oversample ratio)
    void Process(double pcmSample, uint8_t* dsdBits, size_t numBits);

    /// Process a block of PCM samples
    /// @param pcmSamples Input PCM samples (normalized float)
    /// @param sampleCount Number of PCM samples
    /// @param dsdOutput Output buffer (must be sampleCount * _oversampleRatio / 8 bytes)
    /// @return Number of DSD bytes written
    size_t ProcessBlock(const float* pcmSamples, size_t sampleCount, uint8_t* dsdOutput);

    /// Get current modulator load (0.0-1.0, >0.9 indicates potential instability)
    double GetLoad() const { return _load; }

    /// Get oversample ratio
    uint32_t GetOversampleRatio() const { return _oversampleRatio; }

private:
    void InitializeCoefficients();

    // Fifth-order CRFB coefficients (optimized for DSD64/128)
    // These push quantization noise above the audio band with a steep slope
    struct Coefficients
    {
        double a[6];    // Feed-forward coefficients
        double g[5];    // Resonator gains (for notches at specific frequencies)
        double b[6];    // Feedback coefficients
    };

    Coefficients _coef;
    ModulatorOrder _order;
    uint32_t _oversampleRatio;

    // Integrator states
    std::vector<double> _integrators;

    // Previous quantizer output for feedback
    double _lastOutput = 0.0;

    // Load tracking (for stability monitoring)
    double _load = 0.0;
    double _peakIntegrator = 0.0;
};
