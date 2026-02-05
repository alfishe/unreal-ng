// MSVC: Must be defined BEFORE any headers that might include <cmath> for M_PI
#define _USE_MATH_DEFINES
#include "audiofilehelper_test.h"

#include <common/sound/audiofilehelper.h>

#include <cmath>
#include <string>
#include <vector>


/// region <SetUp / TearDown>

void AudioFileHelper_Test::SetUp() {}

void AudioFileHelper_Test::TearDown() {}

/// endregion </SetUp / TearDown>

TEST_F(AudioFileHelper_Test, FlowTestInterleaved)
{
    const std::string filepath = "test_interleaved.wav";
    const uint8_t NUM_CHANNELS = 2;
    const uint32_t SAMPLE_RATE = 44100;
    const float frequencyInHz = 440.f;
    const uint32_t durationInSec = 5;

    // Allocate buffer sufficient to hold 2 channels @44100 for 5 seconds
    std::vector<float> samples(NUM_CHANNELS * SAMPLE_RATE * durationInSec);

    // Generate 440 Hz sinusoidal signal
    for (int i = 0; i < SAMPLE_RATE * durationInSec; i++)
    {
        float sample = sin(((float)i / SAMPLE_RATE) * frequencyInHz * 2.0f * M_PI);
        samples[i * NUM_CHANNELS] = sample;      // Left channel
        samples[i * NUM_CHANNELS + 1] = sample;  // Right channel
    }

    AudioFileHelper helper;
    bool res = helper.openWavFile(filepath);
    EXPECT_EQ(res, true) << "Unable to create WAV file";

    res = helper.saveFloat32PCMInterleavedSamples(samples);
    EXPECT_EQ(res, true) << "Unable to save samples";

    helper.closeWavFile();
}

TEST_F(AudioFileHelper_Test, FlowTestSeparate)
{
    const std::string filepath = "test_separate.wav";
    const uint8_t NUM_CHANNELS = 2;
    const uint32_t SAMPLE_RATE = 44100;
    const float frequencyInHz = 880.f;
    const uint32_t durationInSec = 5;

    constexpr uint32_t samplesLen = SAMPLE_RATE * durationInSec;
    std::vector<float> samplesLeft(samplesLen);
    std::vector<float> samplesRight(samplesLen);

    for (int i = 0; i < samplesLen; i++)
    {
        float sample = sin(((float)i / SAMPLE_RATE) * frequencyInHz * 2.0f * M_PI);
        samplesLeft[i] = sample;
        samplesRight[i] = sample;
    }

    AudioFileHelper helper;
    bool res = helper.openWavFile(filepath);
    EXPECT_EQ(res, true) << "Unable to create WAV file";

    res = helper.saveFloat32PCMSamples(samplesLeft, samplesRight);
    EXPECT_EQ(res, true) << "Unable to save samples";

    helper.closeWavFile();
}

TEST_F(AudioFileHelper_Test, FlowTestMultiblock)
{
    const std::string filepath = "test_multiblock.wav";
    const uint8_t NUM_CHANNELS = 2;
    const uint32_t SAMPLE_RATE = 44100;
    const uint32_t durationInSec = 1;

    AudioFileHelper helper;
    bool res = helper.openWavFile(filepath);
    EXPECT_EQ(res, true) << "Unable to create WAV file";

    const std::vector<uint16_t> frequencies = {440, 880, 1200, 1500};

    for (auto frequencyInHz : frequencies)
    {
        // Allocate buffer sufficient to hold 2 channels for required duration and sample rate
        std::vector<float> samples(NUM_CHANNELS * SAMPLE_RATE * durationInSec);

        // Generate 440 Hz sinusoidal signal
        for (int i = 0; i < SAMPLE_RATE * durationInSec; i++)
        {
            float sample = sin(((float)i / SAMPLE_RATE) * frequencyInHz * 2.0f * M_PI);
            samples[i * NUM_CHANNELS] = sample;      // Left channel
            samples[i * NUM_CHANNELS + 1] = sample;  // Right channel
        }

        res = helper.saveFloat32PCMInterleavedSamples(samples);
        EXPECT_EQ(res, true) << "Unable to save samples";
    }

    helper.closeWavFile();
}

TEST_F(AudioFileHelper_Test, SquareTestInterleaved)
{
    const std::string filepath = "test_square_interleaved.wav";
    const uint8_t NUM_CHANNELS = 2;
    const uint32_t SAMPLE_RATE = 44100;
    const float frequencyInHz = 880.f;
    const uint32_t durationInSec = 5;

    // Allocate buffer sufficient to hold 2 channels @44100 for 5 seconds
    std::vector<float> samples(NUM_CHANNELS * SAMPLE_RATE * durationInSec);

    // Generate 440 Hz square-wave signal
    for (int i = 0; i < SAMPLE_RATE * durationInSec; i++)
    {
        float sample = (i % (int)(SAMPLE_RATE / frequencyInHz) < (int)(SAMPLE_RATE / frequencyInHz / 2)) ? 1.0 : -1.0;
        samples[i * NUM_CHANNELS] = sample;      // Left channel
        samples[i * NUM_CHANNELS + 1] = sample;  // Right channel
    }

    AudioFileHelper helper;
    bool res = helper.openWavFile(filepath);
    EXPECT_EQ(res, true) << "Unable to create WAV file";

    res = helper.saveFloat32PCMInterleavedSamples(samples);
    EXPECT_EQ(res, true) << "Unable to save samples";

    helper.closeWavFile();
}

#include <cmath>
#include <complex>
#include <iostream>

#define __USE_SQUARE_BRACKETS_FOR_ELEMENT_ACCESS_OPERATOR
#include <3rdparty/simple-fft/include/simple_fft/fft.h>
#include <3rdparty/simple-fft/include/simple_fft/fft_settings.h>

typedef std::vector<complex_type> ComplexArray1D;

TEST_F(AudioFileHelper_Test, DetermineBaseFrequencySinus)
{
    // Sampling rate and number of samples
    const double samplingRate = 48000;
    const double frequency = 18956;
    const int N = 65536;
    const double coeff = 2.0 * M_PI * (double)frequency;

    /// region <Check if data size is power of 2>
    const int check = N & (N - 1);
    if (check != 0)
    {
        FAIL() << "N must be power of 2";
        assert("N must be power of 2");
    }
    /// endregion </Check if data size is power of 2>

    ComplexArray1D pcmInput;
    ComplexArray1D fftOutput;
    pcmInput.resize(N);   // Use resize() instead of reserve() to actually allocate elements
    fftOutput.resize(N);  // Use resize() instead of reserve() to actually allocate elements

    /// region <Generate input signal>

    for (int n = 0; n < N; n++)
    {
        pcmInput[n] = sin(coeff * (double)n / samplingRate);
    }
    /// endregion </Generate input signal>

    /// region <Perform the FFT>

    const char* error = nullptr;
    simple_fft::FFT(pcmInput, fftOutput, N, error);
    /// endregion </Perform the FFT>

    // Find the frequency bin with the highest magnitude

    /*
    ComplexArray1D::iterator result = std::max_element(fftOutput.begin(), fftOutput.end(), [](complex_type a,
    complex_type b) { bool res = std::abs(a) < std::abs(b); return res;
    });

    int max_bin = std::distance(fftOutput.begin(), result);
    double max_magnitude = std::abs(*result);
     */

    int max_bin = 0;
    double max_magnitude = -std::numeric_limits<double>::infinity();
    ;
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
    std::cout << "Frequency: " << round(detectedFrequency) << " Hz\n";
}

TEST_F(AudioFileHelper_Test, DetermineBaseFrequencySquare)
{
    // Sampling rate and number of samples
    const double samplingRate = 48000;
    const double frequency = 4230;
    const int N = 65536;

    /// region <Check if data size is power of 2>
    const int check = N & (N - 1);
    if (check != 0)
    {
        FAIL() << "N must be power of 2";
        assert("N must be power of 2");
    }
    /// endregion </Check if data size is power of 2>

    ComplexArray1D pcmInput;
    ComplexArray1D fftOutput;
    pcmInput.resize(N);   // Use resize() instead of reserve() to actually allocate elements
    fftOutput.resize(N);  // Use resize() instead of reserve() to actually allocate elements

    /// region <Generate input signal>

    for (int n = 0; n < N; n++)
    {
        pcmInput[n] = (n % (int)(samplingRate / frequency) < (int)(samplingRate / frequency / 2.0)) ? 1.0 : -1.0;
    }
    /// endregion </Generate input signal>

    /// region <Perform the FFT>

    const char* error = nullptr;
    simple_fft::FFT(pcmInput, fftOutput, N, error);
    /// endregion </Perform the FFT>

    // Find the frequency bin with the highest magnitude

    /*
    ComplexArray1D::iterator result = std::max_element(fftOutput.begin(), fftOutput.end(), [](complex_type a,
    complex_type b) { bool res = std::abs(a) < std::abs(b); return res;
    });

    int max_bin = std::distance(fftOutput.begin(), result);
    double max_magnitude = std::abs(*result);
     */

    int max_bin = 0;
    double max_magnitude = -std::numeric_limits<double>::infinity();
    ;
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
    std::cout << "Frequency: " << round(detectedFrequency) << " Hz\n";
}

TEST_F(AudioFileHelper_Test, DetermineBaseFrequency)
{
    // Sampling rate and number of samples
    constexpr int sampling_rate = 44100;
    constexpr int frequency = 440;
    constexpr int N = 2048;
    double coeff = 2.0 * M_PI * (double)frequency;

    // Input signal - use std::vector for MSVC compatibility (no VLAs)
    std::vector<double> x(N);
    for (int n = 0; n < N; n++)
    {
        x[n] = sin(coeff * (double)n / (double)sampling_rate);
    }

    // Perform the FFT - use std::vector for MSVC compatibility
    std::vector<std::complex<double>> X(N);
    const std::complex single = std::complex<double>(0, 1);
    const std::complex normalized = single * -2.0 * M_PI;

    for (int k = 0; k < N; k++)
    {
        for (int n = 0; n < N; n++)
        {
            std::complex cplx = normalized * (double)k * (double)n / (double)N;
            X[k] += x[n] * std::exp(cplx);
        }
    }

    // Find the frequency bin with the highest magnitude
    int max_bin = 0;
    double max_magnitude = 0;
    for (int k = 0; k < N / 2; k++)
    {
        double magnitude = std::abs(X[k]);
        if (magnitude > max_magnitude)
        {
            max_magnitude = magnitude;
            max_bin = k;
        }
    }

    // Convert the frequency bin to frequency
    double detectedFrequency = max_bin * (sampling_rate / N);

    // Output the frequency
    std::cout << "Frequency: " << detectedFrequency << " Hz\n";
}
