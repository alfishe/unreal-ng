#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "3rdparty/tinywav/tinywav.h"

class AudioFileHelper
{
    /// region <Constants>
protected:
    static constexpr uint8_t NUM_CHANNELS = 2;
    static constexpr uint32_t SAMPLE_RATE = 44100;
    static constexpr uint32_t DISK_BLOCK_SIZE = 4096;

    /// endregion </Constants>

    /// region <Fields>
protected:
    TinyWav* _wavFileDescriptor = nullptr;
    bool _started = false;
    /// endregion </Fields>

    /// region <Methods>
public:
    bool openWavFile(const std::string& filepath);
    void closeWavFile();

    bool saveFloat32PCMSamples(const std::vector<float>& samplesLeft, const std::vector<float>& samplesRight);
    bool saveFloat32PCMInterleavedSamples(const std::vector<float>& samples);

    /// endregion </Methods>
};