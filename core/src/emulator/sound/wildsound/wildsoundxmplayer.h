#pragma once

/// @file wildsoundxmplayer.h
/// @brief The Wild Sound card's XM player, as intended: a FastTracker 2 replayer over what the Z80 uploaded
/// (docs/inprogress/2026-10-09-wildsound/design.md §5; owner decision 2026-10-09: FT2 semantics, not the 2021
/// firmware's deviations listed in reference-firmware.md §D).
///
/// Input: the track pack (header, order, XM-packed patterns) and one sample per instrument (8-bit unsigned, #80 =
/// silence, loop none / forward / ping-pong). The upload has no envelopes, fadeout or auto-vibrato: the player acts
/// as FT2 does for an instrument without them (key-off cuts the note).
///
/// Timing: a tick lasts 2.5 / BPM seconds, counted in exact fractional output samples (no drift at any rate); speed
/// = ticks per row. Pitch: FT2 linear or Amiga periods from the header flag, 8363 Hz at C-4 with finetune 0.
/// Mixing: linear interpolation, FT2 volume (sample x channel x global), square-root panning, a 32-sample volume
/// ramp on every volume change and note start, 0.5 / sqrt(channels) headroom, int16 stereo.
///
/// Worked example: speed 6, BPM 125, rate 44100: a tick is 882 samples, a row 5292, so 8.33 rows per second; a C-4
/// of a sample with relative note 0 steps 8363 / 44100 = 0.1896 source samples per output sample.

#include <array>
#include <cstddef>
#include <cstdint>

#include "wildsoundstorage.h"

namespace wildsound
{

class WildSoundXmPlayer
{
public:
    static constexpr int kMaxChannels = 32;
    static constexpr int kRampSamples = 32;

    /// One channel's replayer and mixer state (plain data: the TTD state is the struct's bytes)
    struct Channel
    {
        // Row data of the current row
        uint8_t note = 0, instrument = 0, volColumn = 0, effect = 0, param = 0;

        // Instrument and sample
        uint8_t ins = 0;          ///< 1-based instrument playing (0 = none)
        int8_t fineTune = 0;      ///< sample's, or E5x's
        int8_t relativeNote = 0;
        uint8_t baseNote = 0;     ///< last triggered note (1..96)

        // Pitch: FT2 period units (linear: 7680 - (note - 1) * 64 - finetune / 2; Amiga: 4 x ProTracker)
        int32_t period = 0;
        int32_t targetPeriod = 0;
        int32_t outPeriod = 0;    ///< with vibrato / arpeggio
        uint8_t portaSpeed = 0;   ///< 3xx memory (x 4)
        uint8_t portaUp = 0, portaDown = 0, finePortaUp = 0, finePortaDown = 0, xfinePortaUp = 0, xfinePortaDown = 0;
        bool glissando = false;

        // Volume and panning
        int32_t volume = 0;       ///< 0..64
        int32_t outVolume = 0;    ///< with tremolo / tremor
        int32_t panning = 128;    ///< 0..255
        uint8_t volSlide = 0, fineVolUp = 0, fineVolDown = 0, panSlide = 0, globalSlide = 0;
        bool keyOff = false;

        // Modulation
        uint8_t vibratoSpeed = 0, vibratoDepth = 0, vibratoPos = 0, vibratoWave = 0;
        uint8_t tremoloSpeed = 0, tremoloDepth = 0, tremoloPos = 0, tremoloWave = 0;
        uint8_t tremorParam = 0, tremorPos = 0;
        bool tremorOn = true;
        uint8_t retrigParam = 0, retrigCount = 0;
        uint8_t offsetMemory = 0;

        // Pattern loop (E6x is per channel in FT2)
        uint8_t loopRow = 0, loopCount = 0;

        // Note delay / cut / key-off timers (ticks; 0 = none)
        uint8_t delayTick = 0, cutTick = 0, keyOffTick = 0;

        // Mixer
        bool active = false;
        bool backward = false;    ///< ping-pong direction
        uint64_t pos = 0;         ///< 32.32 source position
        uint64_t step = 0;        ///< 32.32 per output sample
        int32_t rampLeft = 0, rampRight = 0;  ///< current gain, 16.16
        int32_t targetLeft = 0, targetRight = 0;
        int32_t rampLeftStep = 0, rampRightStep = 0;
        uint16_t rampRemaining = 0;
    };

    /// The whole replayer state
    struct State
    {
        bool playing = false;
        uint8_t speed = 6;
        uint8_t bpm = 125;
        uint8_t tick = 0;
        uint8_t row = 0;
        uint8_t order = 0;
        uint8_t globalVolume = 64;
        uint8_t patternDelay = 0;      ///< EEx rows left
        uint8_t patternDelaySet = 0;
        bool jump = false;             ///< Bxx / Dxx pending at the row's end
        uint8_t jumpOrder = 0, jumpRow = 0;
        bool loopJump = false;         ///< E6x jump pending
        uint8_t loopJumpRow = 0;
        uint32_t patternOffset = 0;    ///< read position in the current pattern's packed rows (from its start)
        uint64_t tickRemainder = 0;    ///< output samples left in the tick, 32.32
        uint32_t outputRate = 44100;
        uint64_t rows = 0;             ///< rows played (report)
        bool linear = false;           ///< the track's frequency table
        bool inDelay = false;          ///< repeating a row for EEx
        uint8_t channelCount = 0;
        std::array<Channel, kMaxChannels> channels{};
    };

    WildSoundXmPlayer() = default;

    /// DIGPLAY: start from order 0, row 0 with the track's speed and BPM
    void Start(const WildSoundStorage& storage);
    /// CLEAR / ERASE / RESET
    void Stop();
    bool Playing() const { return _s.playing; }

    void SetOutputRate(uint32_t rate);
    uint32_t OutputRate() const { return _s.outputRate; }

    /// Render `frames` stereo frames (interleaved L, R) into `out`; silence when not playing
    void Render(const WildSoundStorage& storage, int16_t* out, size_t frames);

    const State& GetState() const { return _s; }
    /// The state as bytes (TTD)
    static constexpr size_t StateSize() { return sizeof(State); }
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);

    /// region <Pitch, public for tests>
    /// FT2 period of `note` (1..96, relative note applied) with `fineTune` (-128..127)
    static int32_t NotePeriod(bool linear, int note, int fineTune);
    /// Sample rate a period plays at
    static double PeriodToHz(bool linear, int32_t period);
    /// endregion </Pitch, public for tests>

private:
    void StartRow(const WildSoundStorage& storage);
    void ReadRow(const WildSoundStorage& storage);
    void RowTick(const WildSoundStorage& storage, Channel& ch);
    void OtherTick(const WildSoundStorage& storage, Channel& ch);
    void TriggerNote(const WildSoundStorage& storage, Channel& ch);
    void SetInstrument(const WildSoundStorage& storage, Channel& ch);
    void ApplyVolumeColumnRow(Channel& ch);
    void ApplyVolumeColumnTick(Channel& ch);
    void DoVolumeSlide(Channel& ch, uint8_t param);
    void DoPortaToNote(Channel& ch);
    void DoVibrato(Channel& ch);
    void DoTremolo(Channel& ch);
    void DoTremor(Channel& ch);
    void DoRetrig(const WildSoundStorage& storage, Channel& ch);
    void NextTick(const WildSoundStorage& storage);
    void UpdateOutput(const WildSoundStorage& storage, Channel& ch);
    void AdvanceRow(const WildSoundStorage& storage);
    void SeekOrder(const WildSoundStorage& storage, unsigned order, unsigned row);
    void ProcessNote(const WildSoundStorage& storage, Channel& ch);
    void RowEffects(const WildSoundStorage& storage, Channel& ch, bool firstPass);
    static uint32_t SkipRows(const uint8_t* data, uint32_t size, int channels, unsigned rows);
    static int Wave(uint8_t wave, uint8_t pos);

    const WildSoundInstrument* Instrument(const WildSoundStorage& storage, uint8_t ins) const;
    int MixChannel(const WildSoundStorage& storage, Channel& ch, int32_t* acc, size_t frames);

    State _s;
};

}  // namespace wildsound
