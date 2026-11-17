#pragma once

/// @file vdac4audio.h
/// @brief The VDAC4 board's ESP32-S3 sound (the firmware's I2S audio player) as a mixer row of SoundManager.
///
/// The app's PCM leaves the ESP's player ring in ESP time at the rate the app opened (zm pcm_open: DOOM 11025 Hz,
/// R-Type 22050 Hz, ...). Push() resamples it (linear) to the mixer's rate into a FIFO; FrameEnd(samples) takes the
/// frame's samples from it. The ESP runs from the machine's frame end and on every SPI access, so the FIFO holds up
/// to about one frame ahead; a short FIFO is padded with the last sample (no click), a FIFO beyond kMaxLagFrames
/// frames drops its oldest samples (the latency never grows).
///
/// Worked example: DOOM at 11025 Hz into a 44100 Hz mixer: one ESP frame becomes 4 output frames; a 50 Hz machine
/// frame of 882 output samples takes 220.5 ESP frames.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "emulator/sound/cardaudio.h"

class Vdac4Audio final : public ICardAudio
{
public:
    static constexpr size_t kMaxLagFrames = 3;

    Vdac4Audio() = default;

    /// The ESP's player output: interleaved int16 stereo frames at `rate`
    void Push(const int16_t* frames, size_t count, uint32_t rate);

    /// region <ICardAudio>
    void BusReset() override;
    void FrameEnd(size_t samples) override;
    void SetOutputRate(uint32_t rate) override;
    void MixerRows(std::vector<CardMixerRow>& out) const override;
    const int16_t* MixerBuffer(AudioSourceType type) const override;
    /// endregion </ICardAudio>

    size_t Queued() const { return (_fifo.size() - _fifoHead) / 2; }
    bool HadSoundLastFrame() const { return _hadSound; }
    const int16_t* Buffer() const { return _buffer.data(); }

    /// Published at each FrameEnd; any thread may read them (diagnostics)
    struct Stats
    {
        uint32_t inputRate = 0;   ///< the app's PCM rate (0: never opened)
        uint32_t outputRate = 0;
        uint32_t queued = 0;      ///< output frames ahead of the mixer after the frame
        uint64_t underruns = 0;   ///< frames the FIFO ran short
        uint64_t dropped = 0;     ///< output frames dropped (the ESP ran too far ahead)
        bool sound = false;       ///< the last frame was not silent
    };
    Stats GetStats() const;

private:
    void Compact();

    uint32_t _outRate = 44100;
    uint32_t _inRate = 0;
    double _phase = 0;              ///< position between _prev and the next input frame, 0..1
    int16_t _prev[2] = {0, 0};      ///< last input frame (the interpolation's left side)
    int16_t _last[2] = {0, 0};      ///< last output frame (pads an underrun)
    std::vector<int16_t> _fifo;     ///< resampled stereo frames at _outRate
    size_t _fifoHead = 0;           ///< first unread int16 in _fifo
    std::vector<int16_t> _buffer;   ///< the last FrameEnd's samples
    uint64_t _underruns = 0;
    uint64_t _dropped = 0;
    bool _hadSound = false;

    std::atomic<uint32_t> _pubInRate{0};
    std::atomic<uint32_t> _pubOutRate{44100};
    std::atomic<uint32_t> _pubQueued{0};
    std::atomic<uint64_t> _pubUnderruns{0};
    std::atomic<uint64_t> _pubDropped{0};
    std::atomic<bool> _pubSound{false};
};
