#include "audiohelper.h"



uint32_t AudioHelper::detectBaseFrequencyFFT(AudioSamplesArray samples, uint32_t sampleRate)
{
    uint32_t result = std::numeric_limits<uint32_t>().max();

    // Sampling rate and number of samples
    const double samplingRate = 48000;
    const double frequency = 4230;
    const int N = 65536;

    /// region <Sanity checks>

    /// region <Check if data size is power of 2>
    const int check = N & (N - 1);
    if (check != 0)
    {
        assert("N must be power of 2");
    }

    if (samples.size() < N)
    {
        assert("There must be at least N = 65536 samples provided");
    }

    /// endregion </Check if data size is power of 2>

    /// endregion </Sanity checks>

    ComplexArray1D pcmInput(samples.begin(), samples.end());
    ComplexArray1D fftOutput;
    fftOutput.reserve(N);

    /// region <Perform the FFT>

    const char* error = nullptr;
    simple_fft::FFT(pcmInput, fftOutput, N, error);

    /// endregion </Perform the FFT>

    int max_bin = 0;
    double max_magnitude = -std::numeric_limits<double>::infinity();;
    for (int k = 0; k < N / 2; k++)
    {
        double magnitude = std::abs(fftOutput[k]);
        if (magnitude > max_magnitude)
        {
            max_magnitude = magnitude;
            max_bin = k;
        }
    }

    // Convert the frequency bin to frequency
    double detectedFrequency = max_bin * (samplingRate / (double)N);

    // Output the frequency
    result = round(detectedFrequency);

    return result;
}

uint32_t AudioHelper::detectBaseFrequencyZeroCross(AudioSamplesArray samples, uint32_t sampleRate)
{
    uint32_t result = std::numeric_limits<uint32_t>().max();
    //uint32_t N = sampleRate; // Use only 1 second of samples
    uint32_t N = samples.size();

    // Count rising edges only
    int risingEdges = 0;
    for (int i = 0; i < N - 1; i++) {
        if (samples[i] < 0 && samples[i + 1] > 0) {
            risingEdges++;
        }
    }

    double timeInterval = (double)N / (double)sampleRate;
    double frequency = (double)risingEdges / timeInterval;

    result = std::round(frequency);

    return result;
}