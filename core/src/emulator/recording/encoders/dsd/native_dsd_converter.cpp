#include "native_dsd_converter.h"

#include <cmath>

NativeDSDConverter::NativeDSDConverter(uint32_t inputSampleRate,
                                       DSDRate dsdRate,
                                       uint8_t channels,
                                       ModulatorOrder modulatorOrder)
    : _dsdRate(dsdRate)
    , _channels(channels)
    , _inputRate(inputSampleRate)
    , _outputRate(::GetDSDSampleRate(dsdRate))
{
    for (uint8_t ch = 0; ch < _channels; ch++)
    {
        _modulators.push_back(std::make_unique<DeltaSigmaModulator>(modulatorOrder, 1));
    }

    _prevSample.resize(_channels, 0.0);
    _currentByte.resize(_channels, 0);
}

void NativeDSDConverter::Reset()
{
    for (auto& mod : _modulators)
    {
        mod->Reset();
    }

    std::fill(_prevSample.begin(), _prevSample.end(), 0.0);
    std::fill(_currentByte.begin(), _currentByte.end(), 0);
    _hasPrev = false;
    _phase = 0;
    _bitIndex = 0;
    _stats = DSDStats();
}

void NativeDSDConverter::SetInputGain(double gainDb)
{
    _inputGain = std::pow(10.0, gainDb / 20.0);
}

float NativeDSDConverter::ApplyPunch(float sample) const
{
    if (!_punchEnabled || _punchAmount <= 0.0)
        return sample;

    // Soft saturation with adjustable drive. Applied at the native rate
    // (~218.75 kHz), so generated harmonics stay below Nyquist instead of
    // aliasing back into the audio band as they would at 44.1 kHz.
    double drive = 1.0 + _punchAmount * 3.0;  // 1x to 4x drive
    double x = sample * drive;

    double saturated;
    if (std::abs(x) < 1.0)
    {
        saturated = x - (x * x * x) / 6.0 * _punchAmount;
    }
    else
    {
        saturated = std::tanh(x);
    }

    return static_cast<float>((1.0 - _punchAmount * 0.5) * sample +
                              _punchAmount * 0.5 * saturated);
}

void NativeDSDConverter::EmitBits(const double* channelSamples, std::vector<uint8_t>& dsdOutput)
{
    // One DSD bit per channel, packed LSB-first into per-channel bytes:
    // DSF's bitsPerSample=1 mandates oldest-sample-in-LSB. MSB-first packing
    // makes players time-reverse each byte, folding the ultrasonic shaped
    // noise into the audio band (~30 dB of broadband hiss, measured).
    // All channels share the same bit clock, so byte columns complete together.
    for (uint8_t ch = 0; ch < _channels; ch++)
    {
        uint8_t bit = 0;
        _modulators[ch]->Process(channelSamples[ch], &bit, 1);

        if (bit)
            _currentByte[ch] |= (1 << _bitIndex);
    }

    _bitIndex++;
    _stats.samplesEncoded += _channels;

    if (_bitIndex == 8)
    {
        // Flush the completed byte column, interleaved by channel
        for (uint8_t ch = 0; ch < _channels; ch++)
        {
            dsdOutput.push_back(_currentByte[ch]);
            _currentByte[ch] = 0;
        }
        _bitIndex = 0;
    }
}

size_t NativeDSDConverter::Process(const float* frames, size_t frameCount,
                                   std::vector<uint8_t>& dsdOutput)
{
    size_t bytesBefore = dsdOutput.size();

    // Expected output: ~frameCount * outputRate / inputRate bits per channel
    dsdOutput.reserve(bytesBefore +
                      (frameCount * _outputRate / _inputRate / 8 + 2) * _channels);

    std::vector<double> cur(_channels);
    std::vector<double> interp(_channels);

    for (size_t i = 0; i < frameCount; i++)
    {
        // Condition the incoming frame: gain, peak tracking, punch —
        // all in the native-rate domain
        for (uint8_t ch = 0; ch < _channels; ch++)
        {
            float sample = frames[i * _channels + ch] * static_cast<float>(_inputGain);

            float absVal = std::abs(sample);
            if (absVal > _stats.peakLevel)
                _stats.peakLevel = absVal;

            cur[ch] = ApplyPunch(sample);
        }

        if (!_hasPrev)
        {
            // First frame: prime the interpolator, no interval to emit yet
            for (uint8_t ch = 0; ch < _channels; ch++)
                _prevSample[ch] = cur[ch];
            _hasPrev = true;
            _stats.pcmSamplesIn++;
            continue;
        }

        // Emit all DSD bits that fall inside the [prev, cur) input interval.
        // _phase is the bit position scaled by _outputRate; one input sample
        // spans _outputRate units, one output bit advances _inputRate units.
        // Exact rational arithmetic — no drift (e.g. 218750→2822400 = 625:8064).
        while (_phase < _outputRate)
        {
            double frac = static_cast<double>(_phase) / _outputRate;

            for (uint8_t ch = 0; ch < _channels; ch++)
                interp[ch] = _prevSample[ch] + (cur[ch] - _prevSample[ch]) * frac;

            EmitBits(interp.data(), dsdOutput);

            _phase += _inputRate;
        }
        _phase -= _outputRate;

        for (uint8_t ch = 0; ch < _channels; ch++)
            _prevSample[ch] = cur[ch];

        _stats.pcmSamplesIn++;
    }

    // Track worst-case modulator load
    for (auto& mod : _modulators)
    {
        _stats.modulatorLoad = std::max(_stats.modulatorLoad, mod->GetLoad());
    }

    return dsdOutput.size() - bytesBefore;
}

size_t NativeDSDConverter::Flush(std::vector<uint8_t>& dsdOutput)
{
    size_t bytesBefore = dsdOutput.size();

    if (_bitIndex > 0)
    {
        // Pad the partial byte column with modulator idle bits would be ideal;
        // zero bits (all -1) for the few remaining positions are inaudible
        for (uint8_t ch = 0; ch < _channels; ch++)
        {
            dsdOutput.push_back(_currentByte[ch]);
            _currentByte[ch] = 0;
        }
        _bitIndex = 0;
    }

    return dsdOutput.size() - bytesBefore;
}
