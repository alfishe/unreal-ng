#pragma once

/// @file wildsoundaudio.h
/// @brief The Wild Sound card's sound (docs/inprogress/2026-10-09-wildsound/design.md §6, §7): its two AY chips and
/// its XM player as mixer rows of SoundManager (ICardAudio).
///
/// Per frame (FrameEnd) the core's events are applied at their time inside the frame: AY register writes land on
/// the output sample their T-state falls in; DIGPLAY / CLEAR switch the card between its AY chips and the XM player
/// (one or the other, as on the card). The AY chips (SoundChip_AY8910, 1 774 400 Hz) tick at clock / 8 and are
/// averaged per output sample; their rows are SSG rows, so SoundManager voices them like the AY socket's chips.
///
/// Worked example: a frame of 71680 T (the Sprinter's 3.5 MHz-equivalent frame) mixed as 882 samples: an AY write at
/// T 35840 lands on sample 441; the generators run 1774400 / 8 / 44100 = 5.03 ticks per sample.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/sound/audio.h"
#include "emulator/sound/cardaudio.h"
#include "wildsoundcore.h"
#include "wildsoundxmplayer.h"

class EmulatorContext;
class SoundChip_AY8910;

namespace wildsound
{

class WildSoundAudio final : public ICardAudio
{
public:
    static constexpr uint32_t kAyClockHz = 1774400;

    WildSoundAudio(EmulatorContext* context, WildSoundCore& core);
    ~WildSoundAudio() override;
    WildSoundAudio(const WildSoundAudio&) = delete;
    WildSoundAudio& operator=(const WildSoundAudio&) = delete;

    /// region <ICardAudio>
    void FrameEnd(size_t samples) override;
    void SetOutputRate(uint32_t rate) override;
    void MixerRows(std::vector<CardMixerRow>& out) const override;
    const int16_t* MixerBuffer(AudioSourceType type) const override;
    int16_t* SsgMixerBuffer(AudioSourceType type) override;
    uint64_t RenderEpoch() const override { return _epoch; }
    /// endregion </ICardAudio>

    /// Render `samples` frames of a frame lasting `frameT` host T-states (tests drive this without a machine)
    void Render(size_t samples, uint64_t frameT);

    const WildSoundXmPlayer& Player() const { return _player; }
    SoundChip_AY8910& Chip(int index) { return *_chips[index & 1]; }
    const int16_t* XmBuffer() const { return _xm.data(); }
    const int16_t* AyBuffer(int chip) const { return _ay[chip & 1].data(); }
    size_t LastSamples() const { return _lastSamples; }

    /// region <State>
    size_t StateSize() const;
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </State>

private:
    void ApplyEvent(const WildSoundEvent& e);

    EmulatorContext* _context;
    WildSoundCore& _core;
    WildSoundXmPlayer _player;
    std::array<std::unique_ptr<SoundChip_AY8910>, 2> _chips;
    uint32_t _rate = 44100;
    double _ticksPerSample = 0;
    double _tickPhase = 0;
    uint64_t _epoch = 0;
    size_t _lastSamples = 0;

    std::vector<int16_t> _xm;
    std::array<std::vector<int16_t>, 2> _ay;
};

}  // namespace wildsound
