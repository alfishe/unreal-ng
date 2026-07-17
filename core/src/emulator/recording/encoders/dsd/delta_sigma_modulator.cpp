#include "delta_sigma_modulator.h"

#include <algorithm>
#include <cstring>

DeltaSigmaModulator::DeltaSigmaModulator(ModulatorOrder order, uint32_t oversampleRatio)
    : _order(order)
    , _oversampleRatio(oversampleRatio)
{
    _integrators.resize(static_cast<size_t>(order), 0.0);
    InitializeCoefficients();
}

void DeltaSigmaModulator::Reset()
{
    std::fill(_integrators.begin(), _integrators.end(), 0.0);
    _lastOutput = 0.0;
    _load = 0.0;
    _peakIntegrator = 0.0;
}

void DeltaSigmaModulator::InitializeCoefficients()
{
    // CIFF topology: Chain of Integrators with Feed-Forward summation.
    //
    //   s1 += (u - v)            <- only the first stage sees the error
    //   s2 += s1 - g1*s3         <- resonator feedback creates NTF notch
    //   s3 += s2
    //   s4 += s3 - g2*s5
    //   s5 += s4
    //   y  = c1*s1 + c2*s2 + c3*s3 + c4*s4 + c5*s5
    //   v  = sign(y)
    //
    // Unlike parallel-integrator schemes, the chained loop has a single
    // DC-balance condition (mean(v) = mean(u)), which keeps every state
    // bounded for inputs up to ~0.7 FS.
    //
    // The 5th-order coefficients follow Reefman & Janssen,
    // "Signal processing for Direct Stream Digital" (Philips, 2002) —
    // the classic SDM design used for SACD mastering. Corner ~100 kHz
    // at 64 FS; resonators place NTF zeros in the upper audio band.

    std::memset(&_coef, 0, sizeof(_coef));

    switch (_order)
    {
        case ModulatorOrder::First:
            // Plain error-feedback integrator: NTF = (1 - z^-1)
            _coef.a[0] = 1.0;
            break;

        case ModulatorOrder::Second:
            // Boser-Wooley equivalent feedforward: unconditionally stable
            _coef.a[0] = 1.0;
            _coef.a[1] = 0.5;
            break;

        case ModulatorOrder::Third:
            _coef.a[0] = 1.0;
            _coef.a[1] = 0.44;
            _coef.a[2] = 0.08;
            _coef.g[0] = 0.0018;  // NTF zero ~14 kHz at 64 FS
            break;

        case ModulatorOrder::Fifth:
        default:
            // Reefman/Janssen SDM2-style coefficient set
            _coef.a[0] = 0.791882;
            _coef.a[1] = 0.304545;
            _coef.a[2] = 0.069930;
            _coef.a[3] = 0.009496;
            _coef.a[4] = 0.000607;

            // Resonator gains: NTF zeros near 12 kHz and 20 kHz at 64 FS
            _coef.g[0] = 0.000496;
            _coef.g[1] = 0.001789;
            break;
    }
}

void DeltaSigmaModulator::Process(double pcmSample, uint8_t* dsdBits, size_t numBits)
{
    const size_t order = _integrators.size();

    // Per-stage state bounds, sized ~3x the natural peak swing of each
    // integrator (measured: s1~3, s2~8, s3~27, s4~110, s5~2100 for hot
    // in-band content). 1-bit SDMs of order >2 are conditionally stable;
    // the clamps turn a divergence into graceful overload distortion.
    static constexpr double STATE_LIMITS[5] = {6.0, 16.0, 64.0, 320.0, 6000.0};

    // Input limiter: this loop is stable for DC inputs up to ~0.60 FS
    // (diverges at 0.62). Pass-through below 0.5, soft knee to a 0.58
    // ceiling above — upstream gain staging should keep peaks under 0.5.
    double u = pcmSample;
    if (u > 0.5)
        u = 0.5 + 0.08 * std::tanh((u - 0.5) / 0.08);
    else if (u < -0.5)
        u = -0.5 - 0.08 * std::tanh((-u - 0.5) / 0.08);

    for (size_t i = 0; i < numBits; i++)
    {
        // First integrator sees the quantization error
        _integrators[0] += u - _lastOutput;

        // Remaining integrators chain; even stages receive resonator
        // feedback from the following stage (g[0]: s2<-s3, g[1]: s4<-s5)
        for (size_t j = 1; j < order; j++)
        {
            double resonator = 0.0;
            if ((j & 1) && j + 1 < order)  // stages 2 and 4 (0-indexed 1 and 3)
            {
                resonator = _coef.g[j / 2] * _integrators[j + 1];
            }

            _integrators[j] += _integrators[j - 1] - resonator;
        }

        // Clamp states and track the worst normalized excursion for load
        for (size_t j = 0; j < order; j++)
        {
            _integrators[j] = std::clamp(_integrators[j], -STATE_LIMITS[j], STATE_LIMITS[j]);

            double normalized = std::abs(_integrators[j]) / STATE_LIMITS[j];
            if (normalized > _peakIntegrator)
                _peakIntegrator = normalized;
        }

        // Feed-forward summation
        double sum = 0.0;
        for (size_t j = 0; j < order; j++)
        {
            sum += _coef.a[j] * _integrators[j];
        }

        // 1-bit quantizer: output +1 or -1
        _lastOutput = (sum >= 0.0) ? 1.0 : -1.0;

        // Store bit (MSB first within each byte)
        size_t byteIdx = i / 8;
        size_t bitIdx = 7 - (i % 8);
        if (_lastOutput > 0)
            dsdBits[byteIdx] |= (1 << bitIdx);
        else
            dsdBits[byteIdx] &= ~(1 << bitIdx);
    }

    // Update load estimate (exponential moving average).
    // _peakIntegrator is already normalized to the per-stage limits;
    // load approaching 1.0 means states are riding the clamps — overload.
    _load = 0.99 * _load + 0.01 * _peakIntegrator;
    _peakIntegrator *= 0.999;  // Slow decay
}

size_t DeltaSigmaModulator::ProcessBlock(const float* pcmSamples, size_t sampleCount, uint8_t* dsdOutput)
{
    size_t dsdBytesPerSample = _oversampleRatio / 8;
    size_t totalBytes = sampleCount * dsdBytesPerSample;

    // Clear output buffer
    std::memset(dsdOutput, 0, totalBytes);

    for (size_t i = 0; i < sampleCount; i++)
    {
        double sample = static_cast<double>(pcmSamples[i]);

        // Soft clip to prevent modulator overload
        if (sample > 0.95) sample = 0.95 + 0.05 * std::tanh((sample - 0.95) * 10.0);
        if (sample < -0.95) sample = -0.95 + 0.05 * std::tanh((sample + 0.95) * 10.0);

        Process(sample, dsdOutput + i * dsdBytesPerSample, _oversampleRatio);
    }

    return totalBytes;
}
