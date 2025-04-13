#include "audiohelper.h"


/// Detect base frequency using 65536 points FFT
/// @param samples Mono audio samples in IEEE Float32 format
/// @param sampleRate Sampling rate in Hz
/// @return Determined base frequency (in Hz)
uint32_t AudioHelper::detectBaseFrequencyFFT(AudioSamplesArray samples, [[maybe_unused]] uint32_t sampleRate)
{
    uint32_t result = std::numeric_limits<uint32_t>().max();

    // Sampling rate and number of samples
    const double samplingRate = 48000;
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

    // Determine frequency band / bin for base frequency found
    int maxBin = 0;
    double maxMagnitude = -std::numeric_limits<double>::infinity();;
    for (int k = 0; k < N / 2; k++)
    {
        double magnitude = std::abs(fftOutput[k]);
        if (magnitude > maxMagnitude)
        {
            maxMagnitude = magnitude;
            maxBin = k;
        }
    }

    // Convert the frequency bin to frequency
    double detectedFrequency = maxBin * (samplingRate / (double)N);

    // Output the frequency
    result = round(detectedFrequency);

    return result;
}

/// Detect base frequency using zero cross algorithm
/// @param samples Mono audio samples in IEEE Float32 format
/// @param sampleRate Sampling rate in Hz
/// @return Determined base frequency (in Hz)
uint32_t AudioHelper::detectBaseFrequencyZeroCross(AudioSamplesArray samples, uint32_t sampleRate)
{
    uint32_t result = std::numeric_limits<uint32_t>().max();
    //uint32_t N = sampleRate; // Use only 1 second of samples
    uint32_t N = samples.size();

    // Count rising edges only
    int risingEdges = 0;
    for (uint32_t i = 0; i < N - 1; i++)
    {
        if (samples[i] < 0 && samples[i + 1] > 0)
        {
            risingEdges++;
        }
    }

    double timeInterval = (double)N / (double)sampleRate;
    double frequency = (double)risingEdges / timeInterval;

    result = std::round(frequency);

    return result;
}

/// Converts all requested samples from Int16 format to IEEE Float32
/// @param source Source samples in Int16 format
/// @param destination Destination samples in IEEE Float32 format
/// @param sampleLen Length of both sample frames
/// @return Result if conversion was successful
bool AudioHelper::convertInt16ToFloat(const int16_t* source, float* destination, size_t sampleLen)
{
    bool result = false;
    constexpr static float MAX_INT16_VALUE = INT16_MAX;

    if (source == nullptr || destination == nullptr || sampleLen == 0)
        return result;

    // Signed Int16 -> IEEE Float32 for each sample
    for (size_t i = 0; i < sampleLen; i++)
    {
        destination[i] = (float)source[i] / MAX_INT16_VALUE;
    }
    result = true;

    return result;
}

/// First-order IIR filter to remove the DC component from an input signal
/// Filter applied independently for both left and right stereo channels
/// @see https://www.dsprelated.com/freebooks/filters/DC_Blocker.html
/// @see https://www.degruyter.com/document/doi/10.1515/freq-2020-0177/html?lang=en
/// @param buffer Samples buffer (Int16 sample format)
/// @param samplesLen Length in samples (length L + length R)
void AudioHelper::filterDCRejectionMono(int16_t* const buffer, size_t samplesLen)
{
    // This filter uses a simple first-order IIR filter to remove the DC component from an input signal.
    // The difference equation for the DC blocker can be written as follows:
    //    y = x - xm1 + 0.995 * ym1;
    //    xm1 = x;
    //    ym1 = y;
    // Where:
    // x - input sample
    // y - output sample

    int16_t x;   // Input sample
    float y;     // Output sample

    int16_t xm1 = 0;
    float ym1 = 0.0;

    for (size_t k = 0; k < samplesLen; k++)
    {
        // Read input sample
        x = buffer[k];

        y = 0.995f * (x - xm1) + 0.99f * ym1;

        xm1 = x;
        ym1 = y;

        // Store output sample
        buffer[k] = y;
    }
}

/// First-order IIR filter to remove the DC component from an input signal
/// Filter applied independently for both left and right stereo channels
/// @see https://www.dsprelated.com/freebooks/filters/DC_Blocker.html
/// @see https://www.degruyter.com/document/doi/10.1515/freq-2020-0177/html?lang=en
/// @param buffer Samples buffer (Int16 sample format)
/// @param samplesLen Length in samples (length L + length R)
void AudioHelper::filterDCRejectionStereoInterleaved(int16_t* const buffer, size_t samplesLen)
{
    // This filter uses a simple first-order IIR filter to remove the DC component from an input signal.
    // The difference equation for the DC blocker can be written as follows:
    //    y = x - xm1 + 0.995 * ym1;
    //    xm1 = x;
    //    ym1 = y;
    // Where:
    // x - input sample
    // y - output sample

    int16_t x[2];   // Input samples (left, right)
    float y[2];     // Output samples (left, right)

    int16_t xm1[2] = { 0, 0 };
    float ym1[2] = { 0.0, 0.0 };

    for (size_t k = 0; k < samplesLen * 2; k+= 2)
    {
        // Read input samples
        x[0] = buffer[k];
        x[1] = buffer[k + 1];

        y[0] = 0.995f * (x[0] - xm1[0]) + 0.99f * ym1[0];
        y[1] = 0.995f * (x[1] - xm1[1]) + 0.99f * ym1[1];

        xm1[0] = x[0];
        xm1[1] = x[1];
        ym1[0] = y[0];
        ym1[1] = y[1];

        // Store output samples
        buffer[k] = y[0];
        buffer[k + 1] = y[1];
    }
}