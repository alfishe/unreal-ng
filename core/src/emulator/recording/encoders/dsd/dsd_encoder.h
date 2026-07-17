#pragma once

#include "emulator/recording/encoder_base.h"
#include "dsd_types.h"
#include "pcm_to_dsd_converter.h"
#include "native_dsd_converter.h"
#include "dsf_writer.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class NativeAudioTap;

/// DSD encoder for high-quality audio recording
/// Converts PCM audio to DSD64/128/256 in DSF container
/// Designed for TurboSound capture with audiophile-grade output
///
/// Two input modes:
/// - PCM mode (default): consumes the 44.1 kHz mixed output via OnAudioSamples()
/// - Native mode: consumes the AY's pre-decimation 218.75 kHz stream from a
///   NativeAudioTap on a dedicated worker thread (TurboSound only — beeper,
///   COVOX etc. never pass through the native path)
class DSDEncoder : public EncoderBase
{
public:
    DSDEncoder();
    ~DSDEncoder() override;

    /// region <Configuration (call before Start)>

    /// Set DSD rate (DSD64, DSD128, etc.)
    void SetDSDRate(DSDRate rate) { _dsdRate = rate; }

    /// Set input gain in dB (e.g., -9.0 for room level headroom)
    void SetInputGain(double gainDb) { _inputGainDb = gainDb; }

    /// Enable punch/saturation processing
    void SetPunchEnabled(bool enabled) { _punchEnabled = enabled; }

    /// Set punch amount (0.0 = clean, 1.0 = aggressive)
    void SetPunchAmount(double amount) { _punchAmount = amount; }

    /// Set delta-sigma modulator order
    void SetModulatorOrder(ModulatorOrder order) { _modulatorOrder = order; }

    /// Enable native-rate capture from a TurboSound tap.
    /// The tap is activated on Start() and deactivated on Stop().
    /// @param tap Shared tap owned by SoundChip_TurboSound
    /// @param nativeSampleRate Tap sample rate in Hz (PSG_CLOCK_RATE / 8)
    void SetNativeTap(std::shared_ptr<NativeAudioTap> tap, uint32_t nativeSampleRate)
    {
        _nativeTap = std::move(tap);
        _nativeSampleRate = nativeSampleRate;
    }

    /// endregion </Configuration>

    /// region <EncoderBase interface>

    bool Start(const std::string& filename, const EncoderConfig& config) override;
    void Stop() override;

    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec) override;
    void OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec) override;

    bool IsRecording() const override { return _isRecording; }
    std::string GetType() const override { return "dsd"; }
    std::string GetDisplayName() const override;
    bool SupportsVideo() const override { return false; }
    bool SupportsAudio() const override { return true; }

    uint64_t GetFramesEncoded() const override { return 0; }
    uint64_t GetAudioSamplesEncoded() const override { return _samplesEncoded; }
    uint64_t GetOutputFileSize() const override;

    std::string GetLastError() const override { return _lastError; }

    /// endregion </EncoderBase interface>

    /// region <DSD-specific>

    /// Get DSD statistics
    const DSDStats& GetDSDStats() const;

    /// Get current modulator load (0.0-1.0, >0.9 indicates potential issues)
    double GetModulatorLoad() const;

    /// True when recording from the native TurboSound tap
    bool IsNativeMode() const { return _nativeTap != nullptr; }

    /// endregion </DSD-specific>

private:
    /// Native mode worker: pops tap frames, converts, writes DSF
    void nativeWorkerMain();

    // Configuration
    DSDRate _dsdRate = DSDRate::DSD64;
    double _inputGainDb = -6.0;     // Default -6dB headroom
    bool _punchEnabled = false;
    double _punchAmount = 0.3;
    ModulatorOrder _modulatorOrder = ModulatorOrder::Fifth;

    // Components (PCM mode)
    std::unique_ptr<PCMToDSDConverter> _converter;

    // Components (native mode)
    std::shared_ptr<NativeAudioTap> _nativeTap;
    uint32_t _nativeSampleRate = 0;
    std::unique_ptr<NativeDSDConverter> _nativeConverter;
    std::thread _nativeWorker;
    std::atomic<bool> _workerStop{false};

    // Shared
    std::unique_ptr<DSFWriter> _writer;

    // State
    bool _isRecording = false;
    std::atomic<uint64_t> _samplesEncoded{0};
    // File size mirror for the UI stats thread — the worker owns _writer,
    // so tellp() on the shared stream would be a data race in native mode
    std::atomic<uint64_t> _bytesWritten{0};
    std::string _lastError;

    // Audio config
    uint32_t _sampleRate = 44100;
    uint8_t _channels = 2;
};
