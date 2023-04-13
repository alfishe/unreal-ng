#include "audiohelper_test.h"

#include <cmath>
#include <string>
#include <vector>
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