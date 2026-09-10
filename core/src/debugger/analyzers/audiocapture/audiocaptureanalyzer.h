#pragma once

#include "debugger/analyzers/ianalyzer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
class AnalyzerManager;
class EmulatorContext;

/// AudioCaptureAnalyzer: sample-counted stereo PCM capture of the emulator's
/// master mix. Subscribes to the audio-sample dispatch wired into
/// SoundManager::handleFrameEnd (post-mix, pre-mute/DRC — the same vantage
/// point as the recording tap).
///
/// Intended automation flow: arm with a target sample count, let the emulator
/// run (the capture self-completes when the target is reached), then read the
/// buffer for offline analysis (RMS/peak/dominant frequency) or WAV export.
///
/// Thread-safety: the buffer is appended only by the emulation thread while
/// armed; readers may access it only after the capture stopped or completed
/// (the WebAPI result endpoint enforces this).
class AudioCaptureAnalyzer : public IAnalyzer
{
public:
    /// Hard ceiling: 30 seconds of interleaved stereo at 96 kHz
    static constexpr size_t MAX_CAPTURE_SAMPLES = 30 * 96000 * 2;

    explicit AudioCaptureAnalyzer(EmulatorContext* context);
    ~AudioCaptureAnalyzer() override;

    // IAnalyzer interface
    std::string getName() const override { return "AudioCaptureAnalyzer"; }
    std::string getUUID() const override { return _uuid; }

    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;

    /// region <Session control (cold path — WebAPI)>

    /// Arm a capture for targetSamples interleaved samples (rounded down to a
    /// stereo pair; clamped to MAX_CAPTURE_SAMPLES). Resets any previous data.
    void startCapture(size_t targetSamples);

    /// Stop appending (freezes the buffer even if the target was not reached)
    void stopCapture();

    /// Drop captured data and reset all counters
    void clearCapture();

    bool isCaptureArmed() const { return _armed.load(std::memory_order_relaxed); }
    bool isCaptureComplete() const { return _complete.load(std::memory_order_relaxed); }
    size_t getTargetSamples() const { return _target; }
    size_t getCapturedSamples() const { return _received.load(std::memory_order_relaxed); }

    /// Interleaved L/R sample buffer — valid only when stopped or complete
    const std::vector<int16_t>& getBuffer() const { return _buffer; }

    /// endregion </Session control>

private:
    /// Audio-sample event handler (emulation thread)
    void onSample(int16_t left, int16_t right);

    std::string _uuid;
    std::atomic<bool> _armed{false};
    std::atomic<bool> _complete{false};
    std::atomic<size_t> _received{0};
    size_t _target = 0;                  // Interleaved samples to capture (even)
    std::vector<int16_t> _buffer;        // Interleaved L/R; written only while armed
};
