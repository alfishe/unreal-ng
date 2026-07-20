#include "mf_aac_encoder.h"

#ifdef _WIN32

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <initguid.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

// AAC encoder CLSID
DEFINE_GUID(CLSID_AACMFTEncoder,
    0x93AF0C51, 0x2275, 0x45d2, 0xa3, 0x5b, 0xf2, 0xba, 0x21, 0xca, 0xed, 0x00);

struct MfAacEncoder::Impl
{
    IMFTransform* transform = nullptr;
    IMFMediaType* inputType = nullptr;
    IMFMediaType* outputType = nullptr;
    DWORD inputStreamId = 0;
    DWORD outputStreamId = 0;
    bool initialized = false;
    bool mfStarted = false;

    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t samplesPerFrame = 1024;  // AAC frame size

    std::vector<int16_t> pendingSamples;
    int64_t nextPts = 0;

    ~Impl() { cleanup(); }

    bool init(uint32_t rate, uint32_t ch, uint32_t bitrate, std::vector<uint8_t>& asc);
    void cleanup();
    bool processInput(const int16_t* samples, size_t count, int64_t pts);
    bool processOutput(std::function<void(const uint8_t*, size_t, int64_t)>& onOutput);
};

bool MfAacEncoder::Impl::init(uint32_t rate, uint32_t ch, uint32_t bitrate,
                               std::vector<uint8_t>& asc)
{
    sampleRate = rate;
    channels = ch;

    // Initialize Media Foundation
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return false;
    mfStarted = true;

    // Create AAC encoder MFT
    hr = CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&transform));
    if (FAILED(hr)) return false;

    // Get stream IDs
    hr = transform->GetStreamIDs(1, &inputStreamId, 1, &outputStreamId);
    if (hr == E_NOTIMPL)
    {
        inputStreamId = 0;
        outputStreamId = 0;
    }

    // Set output type (AAC)
    hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) return false;

    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitrate / 8);
    outputType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);  // Raw AAC
    outputType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);  // AAC-LC

    hr = transform->SetOutputType(outputStreamId, outputType, 0);
    if (FAILED(hr)) return false;

    // Set input type (PCM)
    hr = MFCreateMediaType(&inputType);
    if (FAILED(hr)) return false;

    inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    inputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    inputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
    inputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * channels * 2);

    hr = transform->SetInputType(inputStreamId, inputType, 0);
    if (FAILED(hr)) return false;

    // Get AudioSpecificConfig from output type
    UINT32 blobSize = 0;
    hr = outputType->GetBlobSize(MF_MT_USER_DATA, &blobSize);
    if (SUCCEEDED(hr) && blobSize > 0)
    {
        std::vector<uint8_t> userData(blobSize);
        hr = outputType->GetBlob(MF_MT_USER_DATA, userData.data(), blobSize, &blobSize);
        if (SUCCEEDED(hr))
        {
            // MF returns 12-byte structure, AudioSpecificConfig is last 2 bytes
            // Or we can build it ourselves
            if (blobSize >= 2)
            {
                asc.assign(userData.end() - 2, userData.end());
            }
        }
    }

    // If we didn't get ASC from MF, build it manually
    if (asc.empty())
    {
        // AudioSpecificConfig for AAC-LC
        // 5 bits: audioObjectType (2 = AAC-LC)
        // 4 bits: samplingFrequencyIndex
        // 4 bits: channelConfiguration
        // 3 bits: GASpecificConfig

        uint8_t freqIndex = 4;  // Default 44100
        switch (sampleRate)
        {
            case 96000: freqIndex = 0; break;
            case 88200: freqIndex = 1; break;
            case 64000: freqIndex = 2; break;
            case 48000: freqIndex = 3; break;
            case 44100: freqIndex = 4; break;
            case 32000: freqIndex = 5; break;
            case 24000: freqIndex = 6; break;
            case 22050: freqIndex = 7; break;
            case 16000: freqIndex = 8; break;
            case 12000: freqIndex = 9; break;
            case 11025: freqIndex = 10; break;
            case 8000:  freqIndex = 11; break;
        }

        uint8_t channelConfig = static_cast<uint8_t>(channels);

        // Pack: 00010 XXXX YYYY 000
        // audioObjectType=2 (5 bits), freqIndex (4 bits), channelConfig (4 bits), padding (3 bits)
        asc.resize(2);
        asc[0] = 0x10 | (freqIndex >> 1);
        asc[1] = ((freqIndex & 1) << 7) | (channelConfig << 3);
    }

    // Start streaming
    hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (FAILED(hr)) return false;

    hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) return false;

    initialized = true;
    return true;
}

void MfAacEncoder::Impl::cleanup()
{
    if (transform)
    {
        transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        transform->Release();
        transform = nullptr;
    }
    if (inputType) { inputType->Release(); inputType = nullptr; }
    if (outputType) { outputType->Release(); outputType = nullptr; }

    if (mfStarted)
    {
        MFShutdown();
        mfStarted = false;
    }

    initialized = false;
}

bool MfAacEncoder::Impl::processInput(const int16_t* samples, size_t count, int64_t pts)
{
    if (!initialized) return false;

    // Append to pending buffer
    size_t oldSize = pendingSamples.size();
    pendingSamples.resize(oldSize + count);
    memcpy(pendingSamples.data() + oldSize, samples, count * sizeof(int16_t));

    if (nextPts == 0)
        nextPts = pts;

    return true;
}

bool MfAacEncoder::Impl::processOutput(std::function<void(const uint8_t*, size_t, int64_t)>& onOutput)
{
    if (!initialized) return false;

    size_t samplesPerFrame = 1024 * channels;  // AAC frame = 1024 samples per channel

    while (pendingSamples.size() >= samplesPerFrame)
    {
        // Create input sample
        IMFSample* inputSample = nullptr;
        IMFMediaBuffer* inputBuffer = nullptr;

        DWORD bufferSize = static_cast<DWORD>(samplesPerFrame * sizeof(int16_t));

        HRESULT hr = MFCreateMemoryBuffer(bufferSize, &inputBuffer);
        if (FAILED(hr)) return false;

        BYTE* bufferData = nullptr;
        hr = inputBuffer->Lock(&bufferData, nullptr, nullptr);
        if (SUCCEEDED(hr))
        {
            memcpy(bufferData, pendingSamples.data(), bufferSize);
            inputBuffer->Unlock();
            inputBuffer->SetCurrentLength(bufferSize);
        }

        hr = MFCreateSample(&inputSample);
        if (FAILED(hr)) { inputBuffer->Release(); return false; }

        inputSample->AddBuffer(inputBuffer);
        inputBuffer->Release();

        // Set timestamp (100ns units)
        LONGLONG duration = static_cast<LONGLONG>(1024) * 10000000 / sampleRate;
        LONGLONG timestamp = nextPts * 10000000 / sampleRate;  // Convert from sample count to 100ns
        inputSample->SetSampleDuration(duration);
        inputSample->SetSampleTime(timestamp);

        // Process input
        hr = transform->ProcessInput(inputStreamId, inputSample, 0);
        inputSample->Release();

        if (FAILED(hr)) return false;

        // Remove processed samples
        pendingSamples.erase(pendingSamples.begin(), pendingSamples.begin() + samplesPerFrame);

        // Try to get output
        MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {};
        outputDataBuffer.dwStreamID = outputStreamId;

        // Create output sample
        MFT_OUTPUT_STREAM_INFO streamInfo = {};
        hr = transform->GetOutputStreamInfo(outputStreamId, &streamInfo);

        IMFSample* outputSample = nullptr;
        IMFMediaBuffer* outputBuffer = nullptr;

        if (!(streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES))
        {
            hr = MFCreateSample(&outputSample);
            if (FAILED(hr)) return false;

            hr = MFCreateMemoryBuffer(streamInfo.cbSize ? streamInfo.cbSize : 8192, &outputBuffer);
            if (FAILED(hr)) { outputSample->Release(); return false; }

            outputSample->AddBuffer(outputBuffer);
            outputBuffer->Release();
            outputDataBuffer.pSample = outputSample;
        }

        DWORD status = 0;
        hr = transform->ProcessOutput(0, 1, &outputDataBuffer, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            if (outputSample) outputSample->Release();
            nextPts += 1024;  // Advance by one AAC frame
            continue;
        }

        if (FAILED(hr))
        {
            if (outputSample) outputSample->Release();
            return false;
        }

        // Extract output data
        IMFSample* resultSample = outputDataBuffer.pSample;
        if (resultSample)
        {
            IMFMediaBuffer* resultBuffer = nullptr;
            hr = resultSample->GetBufferByIndex(0, &resultBuffer);
            if (SUCCEEDED(hr))
            {
                BYTE* data = nullptr;
                DWORD dataLen = 0;
                hr = resultBuffer->Lock(&data, nullptr, &dataLen);
                if (SUCCEEDED(hr) && dataLen > 0)
                {
                    // Get PTS from sample
                    LONGLONG sampleTime = 0;
                    resultSample->GetSampleTime(&sampleTime);
                    int64_t outPts = sampleTime * sampleRate / 10000000;

                    onOutput(data, dataLen, outPts);
                    resultBuffer->Unlock();
                }
                resultBuffer->Release();
            }
            resultSample->Release();
        }

        nextPts += 1024;
    }

    return true;
}

// Public interface

MfAacEncoder::MfAacEncoder() : _impl(std::make_unique<Impl>()) {}
MfAacEncoder::~MfAacEncoder() = default;

bool MfAacEncoder::init(uint32_t sampleRate, uint32_t channels, uint32_t bitrate)
{
    _sampleRate = sampleRate;
    _channels = channels;
    _bitrate = bitrate;
    return _impl->init(sampleRate, channels, bitrate, _audioSpecificConfig);
}

bool MfAacEncoder::encode(const int16_t* samples, size_t sampleCount, int64_t pts,
                          std::function<void(const uint8_t* data, size_t size, int64_t pts)> onOutput)
{
    if (!_impl->processInput(samples, sampleCount, pts))
        return false;
    return _impl->processOutput(onOutput);
}

bool MfAacEncoder::flush(std::function<void(const uint8_t* data, size_t size, int64_t pts)> onOutput)
{
    if (!_impl->initialized) return false;

    // Send drain message
    _impl->transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

    // Keep processing output until no more
    MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {};
    outputDataBuffer.dwStreamID = _impl->outputStreamId;

    MFT_OUTPUT_STREAM_INFO streamInfo = {};
    _impl->transform->GetOutputStreamInfo(_impl->outputStreamId, &streamInfo);

    while (true)
    {
        IMFSample* outputSample = nullptr;
        IMFMediaBuffer* outputBuffer = nullptr;

        if (!(streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES))
        {
            MFCreateSample(&outputSample);
            MFCreateMemoryBuffer(streamInfo.cbSize ? streamInfo.cbSize : 8192, &outputBuffer);
            outputSample->AddBuffer(outputBuffer);
            outputBuffer->Release();
            outputDataBuffer.pSample = outputSample;
        }

        DWORD status = 0;
        HRESULT hr = _impl->transform->ProcessOutput(0, 1, &outputDataBuffer, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            if (outputSample) outputSample->Release();
            break;
        }

        if (FAILED(hr))
        {
            if (outputSample) outputSample->Release();
            break;
        }

        IMFSample* resultSample = outputDataBuffer.pSample;
        if (resultSample)
        {
            IMFMediaBuffer* resultBuffer = nullptr;
            if (SUCCEEDED(resultSample->GetBufferByIndex(0, &resultBuffer)))
            {
                BYTE* data = nullptr;
                DWORD dataLen = 0;
                if (SUCCEEDED(resultBuffer->Lock(&data, nullptr, &dataLen)) && dataLen > 0)
                {
                    LONGLONG sampleTime = 0;
                    resultSample->GetSampleTime(&sampleTime);
                    int64_t outPts = sampleTime * _sampleRate / 10000000;
                    onOutput(data, dataLen, outPts);
                    resultBuffer->Unlock();
                }
                resultBuffer->Release();
            }
            resultSample->Release();
        }
    }

    return true;
}

#endif // _WIN32
