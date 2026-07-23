#include "dsd_encoder.h"

#include <chrono>
#include <vector>

#include "emulator/recording/encoder_config.h"
#include "emulator/sound/native_audio_tap.h"

DSDEncoder::DSDEncoder()
{
}

DSDEncoder::~DSDEncoder()
{
    if (_isRecording)
    {
        Stop();
    }
}

bool DSDEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    if (_isRecording)
    {
        _lastError = "Already recording";
        return false;
    }

    _sampleRate = config.audioSampleRate;
    _channels = static_cast<uint8_t>(config.audioChannels);

    if (_channels == 0)
    {
        _lastError = "DSD encoder requires at least 1 audio channel";
        return false;
    }

    if (_channels > 6)
    {
        _lastError = "DSD encoder supports max 6 channels";
        return false;
    }

    if (_nativeTap && _nativeSampleRate == 0)
    {
        _lastError = "Native tap set but native sample rate is 0";
        return false;
    }

    // Create the converter for the selected input mode
    if (_nativeTap)
    {
        // Native mode: tap delivers stereo frames at PSG_CLOCK_RATE / 8
        _channels = 2;

        _nativeConverter = std::make_unique<NativeDSDConverter>(
            _nativeSampleRate,
            _dsdRate,
            _channels,
            _modulatorOrder
        );

        _nativeConverter->SetInputGain(_inputGainDb);
        _nativeConverter->SetPunchEnabled(_punchEnabled);
        _nativeConverter->SetPunchAmount(_punchAmount);
    }
    else
    {
        _converter = std::make_unique<PCMToDSDConverter>(
            _sampleRate,
            _dsdRate,
            _channels,
            _modulatorOrder
        );

        _converter->SetInputGain(_inputGainDb);
        _converter->SetPunchEnabled(_punchEnabled);
        _converter->SetPunchAmount(_punchAmount);
    }

    // Create DSF writer
    _writer = std::make_unique<DSFWriter>();

    uint32_t dsdSampleRate = _nativeTap ? _nativeConverter->GetDSDSampleRate()
                                        : _converter->GetDSDSampleRate();

    if (!_writer->Open(filename, dsdSampleRate, _channels, 1))
    {
        _lastError = "Failed to open output file: " + filename;
        _converter.reset();
        _nativeConverter.reset();
        _writer.reset();
        return false;
    }

    _samplesEncoded = 0;
    _isRecording = true;
    _lastError.clear();

    // Native mode: open the tap and start the conversion worker
    if (_nativeTap)
    {
        _workerStop = false;
        _nativeTap->activate();
        _nativeWorker = std::thread(&DSDEncoder::nativeWorkerMain, this);
    }

    return true;
}

void DSDEncoder::Stop()
{
    if (!_isRecording)
        return;

    if (_nativeTap)
    {
        // Ordering matters: close the tap first (producer stops feeding),
        // then ask the worker to drain what's left and exit
        _nativeTap->deactivate();
        _workerStop = true;

        if (_nativeWorker.joinable())
            _nativeWorker.join();
    }

    _isRecording = false;

    if (_writer)
    {
        _writer->Close();
        _writer.reset();
    }

    _converter.reset();
    _nativeConverter.reset();
}

void DSDEncoder::nativeWorkerMain()
{
    // Pop chunk size: ~23 ms at 218.75 kHz. The tap ring holds ~300 ms.
    constexpr size_t CHUNK_FRAMES = 5000;

    std::vector<float> frames(CHUNK_FRAMES * 2);
    std::vector<uint8_t> dsdData;

    auto convertAndWrite = [this, &frames, &dsdData](size_t frameCount) {
        dsdData.clear();
        _nativeConverter->Process(frames.data(), frameCount, dsdData);

        if (!dsdData.empty() && !_writer->Write(dsdData))
        {
            _lastError = "Failed to write DSD data";
        }

        _samplesEncoded += frameCount;
        _bytesWritten.store(_writer->GetBytesWritten(), std::memory_order_relaxed);
    };

    while (!_workerStop.load(std::memory_order_acquire))
    {
        size_t got = _nativeTap->pop(frames.data(), CHUNK_FRAMES);
        if (got == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        convertAndWrite(got);
    }

    // Drain whatever the producer pushed before the tap closed
    size_t got;
    while ((got = _nativeTap->pop(frames.data(), CHUNK_FRAMES)) > 0)
    {
        convertAndWrite(got);
    }

    // Flush partial byte columns
    dsdData.clear();
    _nativeConverter->Flush(dsdData);
    if (!dsdData.empty())
    {
        _writer->Write(dsdData);
    }
}

void DSDEncoder::OnVideoFrame(const FramebufferDescriptor& /*framebuffer*/, double /*timestampSec*/)
{
    // DSD is audio-only, ignore video frames
}

void DSDEncoder::OnAudioSamples(const int16_t* samples, size_t sampleCount, double /*timestampSec*/)
{
    // Native mode: audio arrives via the tap worker; ignore the 44.1 kHz mix
    if (_nativeTap)
        return;

    if (!_isRecording || !_converter || !_writer)
        return;

    // Convert PCM to DSD
    std::vector<uint8_t> dsdData;
    _converter->Process(samples, sampleCount, dsdData);

    // Write to file
    if (!_writer->Write(dsdData))
    {
        _lastError = "Failed to write DSD data";
        // Don't stop recording, just log error
    }

    _samplesEncoded += sampleCount;
}

std::string DSDEncoder::GetDisplayName() const
{
    std::string name = "DSD";
    switch (_dsdRate)
    {
        case DSDRate::DSD64:  name += "64"; break;
        case DSDRate::DSD128: name += "128"; break;
        case DSDRate::DSD256: name += "256"; break;
        case DSDRate::DSD512: name += "512"; break;
    }

    if (_nativeTap)
        name += " Native";

    name += " Encoder";
    return name;
}

uint64_t DSDEncoder::GetOutputFileSize() const
{
    // Native mode: the worker thread owns the writer/stream — report the
    // atomically mirrored counter instead of touching the stream
    if (_nativeTap)
        return _bytesWritten.load(std::memory_order_relaxed);

    if (_writer)
        return _writer->GetFileSize();
    return 0;
}

const DSDStats& DSDEncoder::GetDSDStats() const
{
    static DSDStats emptyStats;
    if (_nativeConverter)
        return _nativeConverter->GetStats();
    if (_converter)
        return _converter->GetStats();
    return emptyStats;
}

double DSDEncoder::GetModulatorLoad() const
{
    if (_nativeConverter)
        return _nativeConverter->GetStats().modulatorLoad;
    if (_converter)
        return _converter->GetStats().modulatorLoad;
    return 0.0;
}
