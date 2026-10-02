#pragma once

#include <string>

#include "emulator/sound/audiodeviceinfo.h"

class EmulatorContext;

/// @file audiomixer.h
/// @brief The per-device mixer by name on every automation interface (automation audit G13):
/// SoundManager's device registry (mute, solo, volume, peak, activity) behind one set of source
/// keys, so the WebAPI (/audio/mixer), the CLI (`audio mixer`), Lua / Python (audio_mixer /
/// audio_mixer_set), MCP (aspect audio_mixer) and the per-source audio capture name the same
/// devices. The reports are DeviceState::AudioMixer / AudioChannels.
///
/// Keys: master, beeper, ay1, ay2, fm1, fm2, covox (the Covox / SoundDrive, or the machine's own
/// DAC: the Sprinter's Covox-Blaster), gs, gs_mp3, moonsound_fm, moonsound_pcm, cd0..cd3 (the CD drive on IDE unit 0..3).
///
/// Worked example: {source: covox, muted: true} silences the Sprinter's DAC in the mix while the
/// AY plays on; /audio/capture {source: covox} still records the DAC (the device's own buffer).
namespace AudioMixer
{
const char* Key(AudioSourceType type);
bool FromKey(const std::string& key, AudioSourceType& type);

/// What to change; -1 / a negative volume = keep
struct Change
{
    int muted = -1;
    int solo = -1;
    double volume = -1.0;   ///< 0..1 (linear)
    bool hasGainDb = false; ///< gain_db instead of volume: 0 dB = 1.0, at most 0 dB
    double gainDb = 0.0;
};

/// muted / solo "0" / "1" (on / off, true / false), volume "0".."1", gain_db "-60".."0"; empty = keep
bool ChangeFromStrings(const std::string& muted, const std::string& solo, const std::string& volume,
                       const std::string& gainDb, Change& change, std::string& error);

/// Apply to a device (or "master": muted only); false with `error` on an unknown or absent device
bool Apply(EmulatorContext* context, const std::string& key, const Change& change, std::string& error);

/// A device whose own buffer the audio capture can record (fitted, has a buffer); master always
bool Capturable(EmulatorContext* context, const std::string& key, AudioSourceType& type, std::string& error);
}  // namespace AudioMixer
