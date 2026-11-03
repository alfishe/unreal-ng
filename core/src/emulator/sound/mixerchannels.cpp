#include "stdafx.h"

#include "mixerchannels.h"

#include <string>

#include "emulator/slots/card.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/modelaudiosource.h"
#include "emulator/sound/soundmanager.h"
#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"
#endif

namespace MixerChannels
{
namespace
{
void AyChannels(std::vector<MixerChannelDesc>& out)
{
    out.push_back({"a", "A", true, false});
    out.push_back({"b", "B", true, false});
    out.push_back({"c", "C", true, false});
}

void Numbered(std::vector<MixerChannelDesc>& out, int count, const char* prefix, bool hasVolume, bool hasMeter)
{
    for (int i = 1; i <= count; i++)
        out.push_back({std::to_string(i), std::string(prefix) + std::to_string(i), hasVolume, hasMeter});
}

/// The socket chip of an AY row, nullptr when the row is not one or the chip is absent
SoundChip_AY8910* SocketAy(const SoundManager& sound, AudioSourceType type)
{
    ITurboSoundDevice* ts = sound.getTurboSound();
    if (!ts)
        return nullptr;
    if (type == AudioSourceType::AY1_All)
        return ts->getChip(0);
    if (type == AudioSourceType::AY2_All)
        return ts->getChipCount() > 1 ? ts->getChip(1) : nullptr;
    return nullptr;
}

/// The TSFM chip of an FM row
TsfmChip* SocketFm(const SoundManager& sound, AudioSourceType type)
{
    auto* tsfm = dynamic_cast<SoundChip_TurboSoundFM*>(sound.getTurboSound());
    if (!tsfm || !tsfm->hasFm())
        return nullptr;
    if (type == AudioSourceType::FM1)
        return tsfm->chip(0);
    if (type == AudioSourceType::FM2)
        return tsfm->chip(1);
    return nullptr;
}

/// The model audio source owns the row: the COVOX row of a device without parts, or one of its parts
bool ModelOwns(const IModelAudioSource* model, AudioSourceType type)
{
    if (!model)
        return false;
    const size_t parts = model->AudioPartCount();
    if (parts == 0)
        return type == AudioSourceType::COVOX;
    for (size_t p = 0; p < parts; p++)
        if (model->AudioPartType(p) == type)
            return true;
    return false;
}
}  // namespace

void Describe(const SoundManager& sound, AudioSourceType type, std::vector<MixerChannelDesc>& out)
{
    IModelAudioSource* model = sound.getModelAudioSource();
    if (ModelOwns(model, type))
    {
        model->AudioChannels(type, out);
        return;
    }
    switch (type)
    {
        case AudioSourceType::AY1_All:
        case AudioSourceType::AY2_All:
            if (SocketAy(sound, type))
                AyChannels(out);
            return;
        case AudioSourceType::FM1:
        case AudioSourceType::FM2:
            if (SocketFm(sound, type))
                Numbered(out, 3, "FM ", true, false);
            return;
        case AudioSourceType::COVOX:
            if (sound.hasCovox())
            {
                out.push_back({"la", "Left A", true, false});
                out.push_back({"lb", "Left B", true, false});
                out.push_back({"ra", "Right A", true, false});
                out.push_back({"rb", "Right B", true, false});
            }
            return;
        case AudioSourceType::GeneralSound:
        {
            // From the slot's copy, not the card: any thread may ask, and only the emulation thread may touch the
            // card (a personality switch replaces it). NeoGS mixes eight channels (its 8CHANS mode), the others four
            const GSTypeKind kind = sound.generalSoundSlot().kind;
            const int channels = kind == GSTypeKind::NGS                            ? 8
                                 : kind == GSTypeKind::Z80 || kind == GSTypeKind::LW ? 4
                                                                                      : 0;
            Numbered(out, channels, "Channel ", true, false);
            return;
        }
#ifdef UNREALNG_HAVE_OPL4
        case AudioSourceType::Moonsound_FM:
            if (SoundChip_Moonsound* ms = sound.getMoonSound())
                Numbered(out, static_cast<int>(ms->channelCount(opl4::ChannelGroup::Fm)), "FM ", true, true);
            return;
        case AudioSourceType::Moonsound_PCM:
            if (SoundChip_Moonsound* ms = sound.getMoonSound())
                Numbered(out, static_cast<int>(ms->channelCount(opl4::ChannelGroup::Pcm)), "PCM ", true, true);
            return;
#endif
        default:
            for (ICardAudio* card : sound.slotCards())
                card->MixerChannels(type, out);
            return;
    }
}

size_t Count(const SoundManager& sound, AudioSourceType type)
{
    // Short keys and names: the strings stay in the small-string buffer, the scratch keeps its capacity
    thread_local std::vector<MixerChannelDesc> scratch;
    scratch.clear();
    Describe(sound, type, scratch);
    return scratch.size();
}

void Apply(SoundManager& sound, AudioSourceType type, size_t index, bool muted, float volume)
{
    IModelAudioSource* model = sound.getModelAudioSource();
    if (ModelOwns(model, type))
    {
        model->SetAudioChannel(type, index, muted, volume);
        return;
    }
    switch (type)
    {
        case AudioSourceType::AY1_All:
        case AudioSourceType::AY2_All:
            if (SoundChip_AY8910* ay = index < 3 ? SocketAy(sound, type) : nullptr)
            {
                ay->setChannelMuted(static_cast<uint8_t>(index), muted);
                ay->setChannelVolume(static_cast<uint8_t>(index), volume);
            }
            return;
        case AudioSourceType::FM1:
        case AudioSourceType::FM2:
            if (TsfmChip* chip = index < 3 ? SocketFm(sound, type) : nullptr)
                chip->fm.setChannelGain(static_cast<uint32_t>(index), MixerChannelGainQ15(muted, volume));
            return;
        case AudioSourceType::COVOX:
            if (Covox* covox = index < 4 ? sound.getCovox() : nullptr)
            {
                const auto ch = static_cast<Covox::Channel>(index);
                covox->setChannelMute(ch, muted);
                covox->setChannelGain(ch, MixerChannelGainQ15(false, volume));
            }
            return;
        case AudioSourceType::GeneralSound:
            if (GeneralSoundCard* gs = sound.getGeneralSound())
                gs->setHostChannelGain(static_cast<int>(index), MixerChannelGainQ15(muted, volume));
            return;
#ifdef UNREALNG_HAVE_OPL4
        case AudioSourceType::Moonsound_FM:
        case AudioSourceType::Moonsound_PCM:
            if (SoundChip_Moonsound* ms = sound.getMoonSound())
            {
                const opl4::ChannelGroup group =
                    type == AudioSourceType::Moonsound_FM ? opl4::ChannelGroup::Fm : opl4::ChannelGroup::Pcm;
                if (index < ms->channelCount(group))
                {
                    ms->setChannelMute(group, index, muted);
                    ms->setChannelGain(group, index, MixerChannelGainQ15(false, volume));
                }
            }
            return;
#endif
        default:
            for (ICardAudio* card : sound.slotCards())
                card->SetMixerChannel(type, index, muted, volume);
            return;
    }
}

bool Peak(const SoundManager& sound, AudioSourceType type, size_t index, float& peak)
{
#ifdef UNREALNG_HAVE_OPL4
    if (type == AudioSourceType::Moonsound_FM || type == AudioSourceType::Moonsound_PCM)
    {
        SoundChip_Moonsound* ms = sound.getMoonSound();
        const opl4::ChannelGroup group =
            type == AudioSourceType::Moonsound_FM ? opl4::ChannelGroup::Fm : opl4::ChannelGroup::Pcm;
        if (!ms || index >= ms->channelCount(group))
            return false;
        peak = ms->channelPeak(group, index);
        return true;
    }
#else
    (void)sound;
    (void)type;
    (void)index;
    (void)peak;
#endif
    return false;
}
}  // namespace MixerChannels
