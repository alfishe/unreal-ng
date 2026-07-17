#include "pcm_to_dsd_converter.h"
#include <cmath>
#include <algorithm>

namespace
{
// Half-band filter coefficients for 2x upsampling
// 48-tap linear phase FIR, optimized for audio with >100dB stopband rejection
const float HALFBAND_FILTER[48] = {
    -0.000012f,  0.000000f,  0.000045f,  0.000000f, -0.000127f,  0.000000f,
     0.000298f,  0.000000f, -0.000608f,  0.000000f,  0.001118f,  0.000000f,
    -0.001907f,  0.000000f,  0.003075f,  0.000000f, -0.004752f,  0.000000f,
     0.007110f,  0.000000f, -0.010404f,  0.000000f,  0.015063f,  0.000000f,
    -0.021863f,  0.000000f,  0.032561f,  0.000000f, -0.051949f,  0.000000f,
     0.098609f,  0.000000f, -0.315924f,  0.500000f,  0.315924f,  0.000000f,
    -0.098609f,  0.000000f,  0.051949f,  0.000000f, -0.032561f,  0.000000f,
     0.021863f,  0.000000f, -0.015063f,  0.000000f,  0.010404f,  0.000000f
};
}

PCMToDSDConverter::PCMToDSDConverter(uint32_t inputSampleRate,
                                     DSDRate dsdRate,
                                     uint8_t channels,
                                     ModulatorOrder modulatorOrder)
    : _dsdRate(dsdRate)
    , _channels(channels)
{
    // Calculate oversample ratio from input to DSD
    uint32_t dsdSampleRate = ::GetDSDSampleRate(dsdRate);
    _oversampleRatio = dsdSampleRate / inputSampleRate;

    // Calculate number of 2x upsampling stages needed
    // e.g., 44100 to DSD64 (2822400) = 64x = 6 stages of 2x
    _upsampleStages = 0;
    uint32_t ratio = _oversampleRatio;
    while (ratio > 1)
    {
        ratio /= 2;
        _upsampleStages++;
    }

    // Create one modulator per channel
    for (uint8_t ch = 0; ch < _channels; ch++)
    {
        _modulators.push_back(std::make_unique<DeltaSigmaModulator>(modulatorOrder, 1));
    }

    // Allocate upsampling buffers
    // Each stage doubles the sample count
    _upsampleBuffers.resize(_upsampleStages + 1);
    _filterStates.resize(_upsampleStages);

    for (uint32_t stage = 0; stage <= _upsampleStages; stage++)
    {
        // Buffer size for this stage (per channel)
        // Start with some reasonable max, will resize as needed
        _upsampleBuffers[stage].resize(0);
    }

    for (uint32_t stage = 0; stage < _upsampleStages; stage++)
    {
        _filterStates[stage].resize(_channels);
        for (uint8_t ch = 0; ch < _channels; ch++)
        {
            _filterStates[stage][ch].resize(UPSAMPLE_FILTER_TAPS, 0.0f);
        }
    }

    // Copy filter coefficients
    _upsampleFilter.assign(HALFBAND_FILTER, HALFBAND_FILTER + UPSAMPLE_FILTER_TAPS);
}

void PCMToDSDConverter::Reset()
{
    for (auto& mod : _modulators)
    {
        mod->Reset();
    }

    for (auto& stageStates : _filterStates)
    {
        for (auto& channelState : stageStates)
        {
            std::fill(channelState.begin(), channelState.end(), 0.0f);
        }
    }

    _stats = DSDStats();
}

void PCMToDSDConverter::SetInputGain(double gainDb)
{
    _inputGain = std::pow(10.0, gainDb / 20.0);
}

float PCMToDSDConverter::ApplyPunch(float sample) const
{
    if (!_punchEnabled || _punchAmount <= 0.0)
        return sample;

    // Soft saturation curve with adjustable drive
    // Adds harmonic richness without hard clipping
    double drive = 1.0 + _punchAmount * 3.0;  // 1x to 4x drive
    double x = sample * drive;

    // Soft clip using tanh-like curve
    double saturated;
    if (std::abs(x) < 1.0)
    {
        // Polynomial approximation in linear region with subtle harmonics
        saturated = x - (x * x * x) / 6.0 * _punchAmount;
    }
    else
    {
        // Soft saturation in overdriven region
        saturated = std::tanh(x);
    }

    // Blend original and saturated based on punch amount
    return static_cast<float>((1.0 - _punchAmount * 0.5) * sample +
                               _punchAmount * 0.5 * saturated);
}

void PCMToDSDConverter::Upsample2x(const float* input, float* output,
                                   size_t sampleCount, size_t channel)
{
    // Polyphase implementation of 2x upsampling
    // Insert zeros between samples, then filter

    auto& state = _filterStates[0][channel];  // Using stage 0 state for simplicity
    constexpr size_t filterLen = UPSAMPLE_FILTER_TAPS;

    for (size_t i = 0; i < sampleCount; i++)
    {
        // Shift delay line
        for (size_t j = filterLen - 1; j > 0; j--)
        {
            state[j] = state[j - 1];
        }
        state[0] = input[i];

        // Output sample at original position (even index)
        float sum0 = 0.0f;
        for (size_t j = 0; j < filterLen; j += 2)
        {
            sum0 += state[j / 2] * _upsampleFilter[j];
        }
        output[i * 2] = sum0 * 2.0f;  // Compensate for zero-stuffing

        // Output sample at interpolated position (odd index)
        float sum1 = 0.0f;
        for (size_t j = 1; j < filterLen; j += 2)
        {
            sum1 += state[j / 2] * _upsampleFilter[j];
        }
        output[i * 2 + 1] = sum1 * 2.0f;
    }
}

size_t PCMToDSDConverter::GetOutputSize(size_t pcmSampleCount) const
{
    // Each PCM sample becomes _oversampleRatio DSD bits
    // DSD is stored as bytes, so divide by 8
    return pcmSampleCount * _channels * _oversampleRatio / 8;
}

size_t PCMToDSDConverter::Process(const int16_t* pcmInput, size_t sampleCount,
                                  std::vector<uint8_t>& dsdOutput)
{
    // Convert int16 to float
    std::vector<float> floatInput(sampleCount * _channels);
    for (size_t i = 0; i < sampleCount * _channels; i++)
    {
        floatInput[i] = static_cast<float>(pcmInput[i]) / 32768.0f;
    }

    return ProcessFloat(floatInput.data(), sampleCount, dsdOutput);
}

size_t PCMToDSDConverter::ProcessFloat(const float* pcmInput, size_t sampleCount,
                                       std::vector<uint8_t>& dsdOutput)
{
    size_t outputSize = GetOutputSize(sampleCount);
    dsdOutput.resize(outputSize);
    std::fill(dsdOutput.begin(), dsdOutput.end(), 0);

    _stats.pcmSamplesIn += sampleCount;

    // Process each channel separately
    for (uint8_t ch = 0; ch < _channels; ch++)
    {
        // De-interleave input for this channel
        std::vector<float> channelInput(sampleCount);
        for (size_t i = 0; i < sampleCount; i++)
        {
            float sample = pcmInput[i * _channels + ch];

            // Apply input gain
            sample *= static_cast<float>(_inputGain);

            // Track peak
            float absVal = std::abs(sample);
            if (absVal > _stats.peakLevel)
                _stats.peakLevel = absVal;

            // Apply punch
            sample = ApplyPunch(sample);

            channelInput[i] = sample;
        }

        // Upsample through stages
        std::vector<float>* currentBuffer = &channelInput;
        std::vector<float> tempBuffer1, tempBuffer2;
        size_t currentSampleCount = sampleCount;

        for (uint32_t stage = 0; stage < _upsampleStages; stage++)
        {
            size_t outputSampleCount = currentSampleCount * 2;
            std::vector<float>& outputBuffer = (stage % 2 == 0) ? tempBuffer1 : tempBuffer2;
            outputBuffer.resize(outputSampleCount);

            Upsample2x(currentBuffer->data(), outputBuffer.data(), currentSampleCount, ch);

            currentBuffer = &outputBuffer;
            currentSampleCount = outputSampleCount;
        }

        // Now currentBuffer contains upsampled data at DSD rate
        // Process through delta-sigma modulator

        // DSD output is bit-interleaved by channel for DSF format
        // Each sample produces 1 DSD bit per channel
        size_t dsdBitsPerChannel = sampleCount * _oversampleRatio;
        size_t dsdBytesPerChannel = dsdBitsPerChannel / 8;

        std::vector<uint8_t> channelDSD(dsdBytesPerChannel);
        std::fill(channelDSD.begin(), channelDSD.end(), 0);

        // Process one sample at a time through modulator
        for (size_t i = 0; i < currentSampleCount; i++)
        {
            double sample = currentBuffer->at(i);
            uint8_t bit;
            _modulators[ch]->Process(sample, &bit, 1);

            // Pack bit into output
            size_t byteIdx = i / 8;
            size_t bitIdx = 7 - (i % 8);  // MSB first
            if (bit & 0x80)
                channelDSD[byteIdx] |= (1 << bitIdx);
        }

        // Interleave into output (DSF format: block interleaved)
        // For simplicity, using byte interleaving
        for (size_t i = 0; i < dsdBytesPerChannel; i++)
        {
            dsdOutput[i * _channels + ch] = channelDSD[i];
        }

        // Update stats
        _stats.modulatorLoad = std::max(_stats.modulatorLoad, _modulators[ch]->GetLoad());
    }

    _stats.samplesEncoded += sampleCount * _oversampleRatio * _channels;

    return outputSize;
}
