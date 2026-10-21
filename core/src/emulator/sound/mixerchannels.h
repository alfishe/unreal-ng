#pragma once

#include <cstddef>
#include <vector>

#include "emulator/sound/audiodeviceinfo.h"
#include "emulator/sound/mixerchannel.h"

class SoundManager;

/// @file mixerchannels.h
/// @brief The mixer's channel layer: which channels each mixer row has on this machine and where a channel's mute /
/// volume go. One table for every device, so SoundManager keeps one store of the user's channel state and the
/// automation planes name every channel the same way (`<row key>.<channel key>`: ay1.a, covox.lb, gs.3,
/// moonsound_fm.18, fm1.2, ms_midi.10, next_dac.c).
///
/// | Row | Channels | Where they go |
/// |:--|:--|:--|
/// | ay1, ay2 (AY / TurboSound, TSFM SSG) | a, b, c | SoundChip_AY8910 channel mute / volume |
/// | fm1, fm2 (TSFM FM) | 1..3 | Ym2203Engine channel gain |
/// | ay3 (Fuller AY), ms_ssg1/2, ms_fm1/2, ms_midi | the card's (ICard::MixerChannels) | the card |
/// | covox (Covox / SounDrive) | la, lb, ra, rb | Covox channel mute / gain |
/// | gs (GS / NeoGS) | 1..4 (NeoGS 1..8) | GeneralSoundCard host channel gain |
/// | moonsound_fm, moonsound_pcm | 1..18, 1..24 (metered) | the OPL4 tap path (mute, gain) |
/// | next_ay1..3, next_dac | a, b, c / a..d | NextAudio (IModelAudioSource channels) |
///
/// Emulation thread for Apply (the frame start); Describe reads the fitted devices only and runs on any thread.
namespace MixerChannels
{
/// The channels of row `type` (appended to `out`); nothing when the row has none
void Describe(const SoundManager& sound, AudioSourceType type, std::vector<MixerChannelDesc>& out);
/// How many Describe gives (no allocation in the steady state)
size_t Count(const SoundManager& sound, AudioSourceType type);
/// One channel's effective state: muted (by the user, or another channel of the row soloed) and volume 0..1
void Apply(SoundManager& sound, AudioSourceType type, size_t index, bool muted, float volume);
/// The channel's last-frame level 0..1 for a channel whose desc has hasMeter; false otherwise
bool Peak(const SoundManager& sound, AudioSourceType type, size_t index, float& peak);
}  // namespace MixerChannels
