#include "stdafx.h"

#include "tapeaudio/tapeaudiowavencoder.h"

#include "common/filehelper.h"

bool TapeAudioWavEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    if (_started)
    {
        _lastError = "encoder already started";
        return false;
    }

    // Mono int16 is the tape-audio contract (design §4.2); the config rate is
    // whatever the render request chose (44100 default, 48000/96000 accepted).
    int rc = tinywav_open_write(&_wav, 1, static_cast<int32_t>(config.audioSampleRate),
                                TW_INT16, TW_INTERLEAVED, filename.c_str());
    if (rc != 0)
    {
        _lastError = "tinywav_open_write failed (rc " + std::to_string(rc) + ") for '" + filename + "'";
        return false;
    }

    _outputPath = filename;
    _started = true;
    return true;
}

void TapeAudioWavEncoder::OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec)
{
    (void)timestampSec;

    if (!_started || samples == nullptr || sampleCount == 0)
    {
        return;
    }

    int written = tinywav_write_i(&_wav, const_cast<int16_t*>(samples), static_cast<int>(sampleCount));
    if (written < 0)
    {
        _lastError = "tinywav_write_i failed (rc " + std::to_string(written) + ")";
        return;
    }

    _samplesWritten += static_cast<uint64_t>(written);
}

void TapeAudioWavEncoder::Stop()
{
    if (_started)
    {
        tinywav_close_write(&_wav);
        _started = false;
    }
}

TapeAudioWavEncoder::~TapeAudioWavEncoder()
{
    Stop();
}

uint64_t TapeAudioWavEncoder::GetOutputFileSize() const
{
    if (!_outputPath.empty() && FileHelper::FileExists(_outputPath))
    {
        return static_cast<uint64_t>(FileHelper::GetFileSize(_outputPath));
    }
    return 0;
}
