#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "emulator/sound/audiodeviceinfo.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
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
/// Two modes. Memory (startCapture): up to MAX_CAPTURE_SAMPLES in a vector, read back through getBuffer(). File
/// (startStreamCapture): samples go to a WAV file through a writer thread of its own (the emulation thread only
/// copies into a chunk), so the length is bounded by the disk, not by memory; getBuffer() stays empty.
///
/// Thread-safety: the buffer is appended only by the emulation thread while
/// armed; readers may access it only after the capture stopped or completed
/// (the WebAPI result endpoint enforces this).
class AudioCaptureAnalyzer : public IAnalyzer
{
public:
    /// Hard ceiling: 30 seconds of interleaved stereo at 96 kHz
    static constexpr size_t MAX_CAPTURE_SAMPLES = 30 * 96000 * 2;

    /// File mode ceiling: a day of audio (the disk is the real limit)
    static constexpr double MAX_STREAM_SECONDS = 86400.0;

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
    /// `source`: the master mix (default) or one mixer device's own buffer (beeper, an AY, the Covox / the
    /// Sprinter's DAC, GS, MoonSound ...) - before its mute / solo / volume (AudioMixer, /audio/mixer)
    void startCapture(size_t targetSamples, AudioSourceType source = AudioSourceType::MasterMix);
    AudioSourceType getSource() const { return _source; }

    /// Capture to a WAV file (16-bit stereo at `rate`) instead of memory. `targetSamples` interleaved samples,
    /// 0 = until stopCapture(). Returns false with `error` when the file cannot be opened. The file is complete
    /// (header patched) once isStreamClosed() is true; stopCapture() waits for that
    bool startStreamCapture(const std::string& path, uint32_t rate, size_t targetSamples, AudioSourceType source,
                            std::string& error);
    bool isStreaming() const { return _streaming; }
    bool isStreamClosed() const { return _streamClosed.load(std::memory_order_acquire); }
    const std::string& getStreamPath() const { return _streamPath; }

    /// Stop appending. Memory mode: the buffer is cut to what came (getBuffer() holds exactly the captured
    /// samples). File mode: the rest is written and the file closed
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

    /// File mode: hand the current chunk to the writer thread
    void flushChunk();
    void writerLoop();
    /// Flush, tell the writer to finish and join it (idempotent; not for the emulation thread's hot path)
    void finishStream();

    std::string _uuid;
    std::atomic<bool> _armed{false};
    std::atomic<bool> _complete{false};
    std::atomic<size_t> _received{0};
    size_t _target = 0;                  // Interleaved samples to capture (even)
    std::vector<int16_t> _buffer;        // Interleaved L/R; written only while armed
    AudioSourceType _source = AudioSourceType::MasterMix;

    // File mode
    static constexpr size_t STREAM_CHUNK_SAMPLES = 16384;   // interleaved; ~85 ms at 96 kHz
    bool _streaming = false;
    std::string _streamPath;
    uint32_t _streamRate = 0;
    std::vector<int16_t> _chunk;                            // filled by the emulation thread
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<std::vector<int16_t>> _queue;                // chunks waiting for the writer
    bool _streamFinish = false;                             // guarded by _queueMutex
    std::thread _writer;
    std::atomic<bool> _streamClosed{true};
    std::atomic<bool> _streamFailed{false};

public:
    bool hasStreamFailed() const { return _streamFailed.load(std::memory_order_acquire); }
};
