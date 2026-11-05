#include "audiocaptureanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"

#include "3rdparty/tinywav/tinywav.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

// Generate a stable ID for this analyzer instance (same pattern as CoverageAnalyzer)
static std::string generateAudioCaptureUUID()
{
    static int counter = 0;
    std::stringstream ss;
    ss << "audiocaptureanalyzer-" << std::hex << std::setw(8) << std::setfill('0') << counter++;
    return ss.str();
}

AudioCaptureAnalyzer::AudioCaptureAnalyzer(EmulatorContext* context)
    : _uuid(generateAudioCaptureUUID())
{
    (void)context;  // Kept for the uniform analyzer registration convention
}

AudioCaptureAnalyzer::~AudioCaptureAnalyzer()
{
    // Subscription is cleaned up by AnalyzerManager on deactivation
    _armed.store(false, std::memory_order_release);
    finishStream();
}

void AudioCaptureAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;

    // Warm-path subscription: ~44-96K events/sec while capturing, none otherwise
    manager->subscribeAudioSample(
        [this](int16_t left, int16_t right)
        {
            onSample(left, right);
        },
        _registrationId);
}

void AudioCaptureAnalyzer::onDeactivate()
{
    if (_manager)
        _manager->setAudioTapSource(static_cast<int>(AudioSourceType::MasterMix));
    // AnalyzerManager automatically cleans up the subscription
    _armed.store(false, std::memory_order_release);
    finishStream();
    _manager = nullptr;
}

void AudioCaptureAnalyzer::startCapture(size_t targetSamples, AudioSourceType source)
{
    _armed.store(false, std::memory_order_release);
    finishStream();
    _streaming = false;
    _source = source;
    if (_manager)
        _manager->setAudioTapSource(static_cast<int>(source));
    // Round down to a whole stereo pair and clamp to the ceiling
    targetSamples = std::min(targetSamples & ~size_t(1), MAX_CAPTURE_SAMPLES);

    _buffer.assign(targetSamples, 0);
    _target = targetSamples;
    _received.store(0, std::memory_order_relaxed);
    _complete.store(false, std::memory_order_relaxed);
    _armed.store(true, std::memory_order_release);
}

bool AudioCaptureAnalyzer::startStreamCapture(const std::string& path, uint32_t rate, size_t targetSamples,
                                              AudioSourceType source, std::string& error)
{
    _armed.store(false, std::memory_order_release);
    finishStream();

    // Open here so a bad path is reported to the caller; the writer thread owns the file afterwards
    _streamPath = path;
    _streamRate = rate;
    _streamFailed.store(false, std::memory_order_relaxed);
    // tinywav asserts on a file it cannot open: check the path with a plain stream first
    {
        std::ofstream probe(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!probe)
        {
            error = "cannot write " + path;
            return false;
        }
    }

    _source = source;
    if (_manager)
        _manager->setAudioTapSource(static_cast<int>(source));
    _buffer.clear();
    _buffer.shrink_to_fit();
    _target = targetSamples & ~size_t(1);   // 0: until stopCapture()
    _chunk.clear();
    _chunk.reserve(STREAM_CHUNK_SAMPLES);
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _queue.clear();
        _streamFinish = false;
    }
    _streaming = true;
    _streamClosed.store(false, std::memory_order_release);
    _writer = std::thread(&AudioCaptureAnalyzer::writerLoop, this);

    _received.store(0, std::memory_order_relaxed);
    _complete.store(false, std::memory_order_relaxed);
    _armed.store(true, std::memory_order_release);
    return true;
}

void AudioCaptureAnalyzer::writerLoop()
{
    TinyWav wav{};
    bool open = tinywav_open_write(&wav, 2, static_cast<int32_t>(_streamRate), TW_INT16, TW_INTERLEAVED,
                                   _streamPath.c_str()) == 0;
    if (!open)
        _streamFailed.store(true, std::memory_order_release);

    for (;;)
    {
        std::vector<int16_t> chunk;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueCv.wait(lock, [this] { return !_queue.empty() || _streamFinish; });
            if (_queue.empty())
                break;   // finish requested and everything is written
            chunk = std::move(_queue.front());
            _queue.pop_front();
        }
        if (open && tinywav_write_i(&wav, chunk.data(), static_cast<int>(chunk.size() / 2)) < 0)
        {
            _streamFailed.store(true, std::memory_order_release);
            open = false;   // drain the rest without writing (disk full)
        }
    }
    if (open)
        tinywav_close_write(&wav);
    _streamClosed.store(true, std::memory_order_release);
}

void AudioCaptureAnalyzer::flushChunk()
{
    if (_chunk.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _queue.push_back(std::move(_chunk));
    }
    _chunk = std::vector<int16_t>();
    _chunk.reserve(STREAM_CHUNK_SAMPLES);
    _queueCv.notify_one();
}

void AudioCaptureAnalyzer::finishStream()
{
    if (!_writer.joinable())
        return;
    flushChunk();
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _streamFinish = true;
    }
    _queueCv.notify_one();
    _writer.join();
}

void AudioCaptureAnalyzer::stopCapture()
{
    _armed.store(false, std::memory_order_release);
    finishStream();
    // The buffer is allocated for the whole target: cut it to what came, so a reader never sees a zero tail
    if (!_streaming)
        _buffer.resize(std::min(_buffer.size(), _received.load(std::memory_order_relaxed)));
    _complete.store(true, std::memory_order_release);
}

void AudioCaptureAnalyzer::clearCapture()
{
    _armed.store(false, std::memory_order_release);
    _complete.store(false, std::memory_order_relaxed);
    finishStream();
    _streaming = false;
    _received.store(0, std::memory_order_relaxed);
    _target = 0;
    _buffer.clear();
    _buffer.shrink_to_fit();
}

void AudioCaptureAnalyzer::onSample(int16_t left, int16_t right)
{
    // Fast exit for the idle common case (no capture armed)
    if (!_armed.load(std::memory_order_relaxed) || _complete.load(std::memory_order_relaxed))
        return;

    const size_t position = _received.load(std::memory_order_relaxed);
    if (_streaming)
    {
        _chunk.push_back(left);
        _chunk.push_back(right);
        _received.store(position + 2, std::memory_order_relaxed);
        if (_chunk.size() >= STREAM_CHUNK_SAMPLES)
            flushChunk();
        if (_target != 0 && position + 2 >= _target)
        {
            // Target reached: the writer drains the queue and closes the file by itself (the thread is joined later)
            flushChunk();
            {
                std::lock_guard<std::mutex> lock(_queueMutex);
                _streamFinish = true;
            }
            _queueCv.notify_one();
            _complete.store(true, std::memory_order_release);
        }
        return;
    }
    if (position + 2 > _target)
    {
        _complete.store(true, std::memory_order_release);
        return;
    }

    // Only the emulation thread appends while armed (see header note)
    _buffer[position] = left;
    _buffer[position + 1] = right;

    const size_t updated = position + 2;
    _received.store(updated, std::memory_order_relaxed);
    if (updated >= _target)
    {
        _complete.store(true, std::memory_order_release);
    }
}
