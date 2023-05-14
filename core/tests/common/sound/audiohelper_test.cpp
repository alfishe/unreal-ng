#include "audiohelper_test.h"

#include <cmath>
#include <string>
#include <vector>
#include <common/dumphelper.h>
#include <common/sound/audiohelper.h>

/// region <SetUp / TearDown>

void AudioHelper_Test::SetUp()
{

}

void AudioHelper_Test::TearDown()
{

}

/// endregion </SetUp / TearDown>


TEST_F(AudioHelper_Test, DetectBaseFrequencyFFTTest)
{
    // Sampling rate and number of samples
    const double samplingRate = 48000;
    const double frequency = 337;
    const int N = 65536;

    /// region <Check if data size is power of 2>
    const int check = N & (N - 1);
    if (check != 0)
    {
        FAIL() << "N must be power of 2";
        assert("N must be power of 2");
    }
    /// endregion </Check if data size is power of 2>

    AudioSamplesArray pcmInput(N);

    /// region <Generate input signal>


    for (int n = 0; n < N; n++)
    {
        //pcmInput[n] = sin(((float)n / samplingRate) * frequency * 2.0f * M_PI);

        pcmInput[n] = (n % (int)(samplingRate / frequency) < (int)(samplingRate / frequency / 2.0)) ? 1.0 : -1.0;
    }
    /// endregion </Generate input signal>

    uint32_t detectedFrequency = AudioHelper::detectBaseFrequencyFFT(pcmInput, samplingRate);
    std::cout << "Frequency: " << detectedFrequency << " Hz" << std::endl;
}

TEST_F(AudioHelper_Test, DetectBaseFrequencyZeroCrossTest)
{
    // Sampling rate and number of samples
    const double samplingRate = 48000;
    const double frequency = 440;
    const int N = samplingRate * 0.5;

    AudioSamplesArray pcmInput(N);

    /// region <Generate input signal>
    for (int n = 0; n < N; n++)
    {
        //pcmInput[n] = sin(((float)n / samplingRate) * frequency * 2.0f * M_PI);

        pcmInput[n] = (n % (int)(samplingRate / frequency) < (int)(samplingRate / frequency / 2.0)) ? 1.0 : -1.0;
    }
    /// endregion </Generate input signal>

    uint32_t detectedFrequency = AudioHelper::detectBaseFrequencyZeroCross(pcmInput, samplingRate);
    std::cout << "Frequency: " << detectedFrequency << " Hz" << std::endl;
}

TEST_F(AudioHelper_Test, convertInt16ToFloat)
{
    const int16_t int16Samples[] =
    {
        INT16_MIN,      INT16_MIN,
        INT16_MAX,      INT16_MAX,
        INT16_MIN,      INT16_MAX,
        INT16_MIN / 2,  INT16_MAX / 2
    };
    const float referenceFloatSamples[] =
    {
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
        -0.5f,  0.5f
    };
    const size_t samplesLen = sizeof(int16Samples) / sizeof(int16Samples[0]);

    float floatSamples[samplesLen] = {};
    AudioHelper::convertInt16ToFloat((const int16_t*)&int16Samples, (float*)&floatSamples, samplesLen);

    std::cout << "Source Int16 samples:" << std::endl;
    std::cout << AudioHelper::dumpInterleavedSamples(int16Samples, samplesLen);
    std::cout << std::endl;
    std::cout << "Target IEEE Float32 samples:" << std::endl;
    std::cout << AudioHelper::dumpInterleavedSamples(floatSamples, samplesLen);

    ASSERT_FLOAT_ARRAY_NEAR(referenceFloatSamples, floatSamples, samplesLen, 0.01);
}

TEST_F(AudioHelper_Test, filterDCRejectionStereoInterleaved)
{
    // Input data samples in stereo interleaved format (L + R)
    const int16_t inputSamples[] =
    {
          0,     0,
        1000, 1000,
          0,     0,
          0,     0,
          0,     0,
          0,     0,
          0,     0,
          0,     0,
          0,     0
    };

    const int16_t referenceOutput[] =
    {
          0,   0,
        995, 995,
         -9,  -9,
         -9,  -9,
         -9,  -9,
         -9,  -9,
         -9,  -9,
         -9,  -9,
         -9,  -9
    };

    constexpr size_t inputArraySizeInBytes = sizeof(inputSamples);
    constexpr size_t inputArrayLength = sizeof(inputSamples) / sizeof(inputSamples[0]);
    constexpr size_t inputArrayLengthInSamples = inputArrayLength / 2;  // Since we store interleaved stereo

    std::cout << "Input:" << std::endl;
    std::cout << DumpHelper::HexDumpBuffer(inputSamples, inputArrayLength, 2);

    // Apply DC filter
    AudioHelper::filterDCRejectionStereoInterleaved((int16_t*)inputSamples, inputArrayLengthInSamples);

    std::cout << std::endl;
    std::cout << "Output:" << std::endl;
    std::cout << DumpHelper::HexDumpBuffer(inputSamples, inputArrayLength, 2);

    ASSERT_EQ(sizeof(inputSamples), sizeof(referenceOutput)) << "Array sizes must be equal for input/output and reference vectors";

    // Compare filter output and reference vector
    bool resultsAreEqual = std::equal(std::begin(referenceOutput), std::end(referenceOutput), std::begin(inputSamples), std::end(inputSamples));

    ASSERT_EQ(resultsAreEqual, true);
}