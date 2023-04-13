#include "audiofilehelper.h"

#include "common/logger.h"
#include "common/filehelper.h"

/// region <Methods>
bool AudioFileHelper::openWavFile(const std::string& filepath)
{
    bool result = false;

    if (_started)
    {
        closeWavFile();
    }

    _wavFileDescriptor = new TinyWav();

    int res = tinywav_open_write(_wavFileDescriptor, NUM_CHANNELS, SAMPLE_RATE, TW_INT16, TW_INTERLEAVED, filepath.data());
    if (res == 0)
    {
        _started = true;
        result = true;
    }

    return result;
}

void  AudioFileHelper::closeWavFile()
{
    if (_wavFileDescriptor)
    {
        tinywav_close_write(_wavFileDescriptor);

        delete _wavFileDescriptor;
        _wavFileDescriptor = nullptr;
    }

    _started = false;
}

bool AudioFileHelper::saveFloat32PCMSamples(const std::vector<float>& samplesLeft, const std::vector<float>& samplesRight)
{
    bool result = false;

    if (samplesLeft.size() == samplesRight.size() && samplesLeft.size() > 0)
    {
        std::vector<float> samplesInterleaved;
        samplesInterleaved.reserve(samplesLeft.size() + samplesRight.size());

        auto itLeft = samplesLeft.begin();
        auto itRight = samplesRight.begin();
        for (int i = 0; i < samplesLeft.size(); i++)
        {
            samplesInterleaved.push_back(*itLeft);
            samplesInterleaved.push_back(*itRight);

            itLeft++;
            itRight++;
        }

        result = saveFloat32PCMInterleavedSamples(samplesInterleaved);
    }

    return result;
}

bool AudioFileHelper::saveFloat32PCMInterleavedSamples(const std::vector<float>& samples)
{
    bool result = false;

    if (_started && samples.size() > 0 && samples.size() % 2 == 0)
    {
        const int samplesCount = samples.size() / 2;
        int res = tinywav_write_f(_wavFileDescriptor, (void *)samples.data(), samplesCount);

        if (res == samplesCount)
        {
            result = true;
        }
    }

    return result;
}


/// endregion </Methods>