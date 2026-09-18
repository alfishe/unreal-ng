#include "audiocaptureanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"

#include <algorithm>
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
    // AnalyzerManager automatically cleans up the subscription
    _armed.store(false, std::memory_order_release);
    _manager = nullptr;
}

void AudioCaptureAnalyzer::startCapture(size_t targetSamples)
{
    // Round down to a whole stereo pair and clamp to the ceiling
    targetSamples = std::min(targetSamples & ~size_t(1), MAX_CAPTURE_SAMPLES);

    _buffer.assign(targetSamples, 0);
    _target = targetSamples;
    _received.store(0, std::memory_order_relaxed);
    _complete.store(false, std::memory_order_relaxed);
    _armed.store(true, std::memory_order_release);
}

void AudioCaptureAnalyzer::stopCapture()
{
    _armed.store(false, std::memory_order_release);
    _complete.store(true, std::memory_order_release);
}

void AudioCaptureAnalyzer::clearCapture()
{
    _armed.store(false, std::memory_order_release);
    _complete.store(false, std::memory_order_relaxed);
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
