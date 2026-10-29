#pragma once

/// @file cardaudio.h
/// @brief The audio face of a card SoundManager mixes per frame: its rows, buffers and frame hooks, independent of
/// the bus the card sits on. A slot-built ZX-bus card (ICard, emulator/slots/card.h) has it, and so has a Sprinter ISA
/// card with sound (the Wild Sound, docs/inprogress/2026-10-09-wildsound/design.md §7). SoundManager::attachSlotCard
/// takes this interface.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/sound/audiodeviceinfo.h"
#include "emulator/sound/mixerchannel.h"

/// One mixer row a card contributes to SoundManager
struct CardMixerRow
{
    AudioSourceType type = AudioSourceType::Custom;
    std::string name;
    /// An AY / SSG row: SoundManager runs it through the AY / SSG tone voicing ([SOUND] AYVoicing) and the AY character
    /// chain (punch, room; Sound HQ), exactly like the AY socket's chips (the card hands the row over writable through
    /// SsgMixerBuffer)
    bool ssgRow = false;
};

class ICardAudio
{
public:
    virtual ~ICardAudio() = default;

    /// Bus /RESET (the machine's reset)
    virtual void BusReset()
    {
    }

    /// region <Frames and audio (SoundManager)>
    virtual void FrameStart()
    {
    }
    /// The frame ended: samples = the output frames SoundManager mixes this frame (0: none are mixed, the card still
    /// runs to the frame's end)
    virtual void FrameEnd(size_t samples)
    {
        (void)samples;
    }
    /// The mixer's core rate changed (frame boundary)
    virtual void SetOutputRate(uint32_t rate)
    {
        (void)rate;
    }
    /// The card's mixer rows, in display order
    virtual void MixerRows(std::vector<CardMixerRow>& out) const
    {
        (void)out;
    }
    /// A row's interleaved stereo int16 buffer of the last FrameEnd (the frames SoundManager asked for); nullptr for
    /// a type the card does not own
    virtual const int16_t* MixerBuffer(AudioSourceType type) const
    {
        (void)type;
        return nullptr;
    }
    /// The writable buffer of a row declared ssgRow (the same one MixerBuffer returns), voiced and run through the AY
    /// character chain in place by SoundManager after FrameEnd; nullptr for any other row
    virtual int16_t* SsgMixerBuffer(AudioSourceType type)
    {
        (void)type;
        return nullptr;
    }
    /// Changes whenever the card restarted its render layers (a TTD restore: its rows no longer continue the audio
    /// before): the host's processing of the rows (the SSG voicing and character chain) restarts with them
    virtual uint64_t RenderEpoch() const
    {
        return 0;
    }
    /// Full-scale sources: the mixer sums on its wide float bus with the master limiter while the card is fitted
    virtual bool WantsWideMix() const
    {
        return false;
    }
    /// YM2203 FM loudness trim in dB ([SOUND] TSFM_FmLevelDb, the audio settings' FM trim) for a card with YM2203 FM;
    /// false: the card has none
    virtual bool SetFmTrimDb(double db)
    {
        (void)db;
        return false;
    }
    virtual bool FmTrimDb(double& db) const
    {
        (void)db;
        return false;
    }
    /// The channels of one of the card's rows (the mixer's channel layer: ay3.a, ms_fm1.2, ms_midi.10); a card adds
    /// nothing for a row it does not own
    virtual void MixerChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const
    {
        (void)type;
        (void)out;
    }
    /// One channel's effective state (muted: by the user or another channel's solo; volume 0..1), host-side; called at
    /// frame boundaries for every row, a card ignores a row it does not own
    virtual void SetMixerChannel(AudioSourceType type, size_t index, bool muted, float volume)
    {
        (void)type;
        (void)index;
        (void)muted;
        (void)volume;
    }
    /// endregion </Frames and audio (SoundManager)>
};
