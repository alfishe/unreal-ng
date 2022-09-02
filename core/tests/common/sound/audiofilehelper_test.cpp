#include "audiofilehelper_test.h"
#include <cmath>
#include <string>
#include <vector>
#include <common/sound/audiofilehelper.h>


/// region <SetUp / TearDown>

void AudioFileHelper_Test::SetUp()
{

}

void AudioFileHelper_Test::TearDown()
{

}

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
        samples[i * NUM_CHANNELS] = sample;         // Left channel
        samples[i * NUM_CHANNELS + 1] = sample;     // Right channel
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

    const std::vector<uint16_t> frequencies = { 440, 880, 1200, 1500 };

    for (auto frequencyInHz : frequencies) {
        // Allocate buffer sufficient to hold 2 channels for required duration and sample rate
        std::vector<float> samples(NUM_CHANNELS * SAMPLE_RATE * durationInSec);

        // Generate 440 Hz sinusoidal signal
        for (int i = 0; i < SAMPLE_RATE * durationInSec; i++)
        {
            float sample = sin(((float) i / SAMPLE_RATE) * frequencyInHz * 2.0f * M_PI);
            samples[i * NUM_CHANNELS] = sample;         // Left channel
            samples[i * NUM_CHANNELS + 1] = sample;     // Right channel
        }

        res = helper.saveFloat32PCMInterleavedSamples(samples);
        EXPECT_EQ(res, true) << "Unable to save samples";
    }

    helper.closeWavFile();
}
