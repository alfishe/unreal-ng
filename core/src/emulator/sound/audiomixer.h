#pragma once

#include <optional>
#include <string>

#include "emulator/sound/audiodeviceinfo.h"
#include "emulator/state/statenode.h"

class EmulatorContext;

/// @file audiomixer.h
/// @brief The per-device mixer by name on every automation interface (automation audit G13):
/// SoundManager's device registry (mute, solo, volume, peak, activity) behind one set of source
/// keys, so the WebAPI (/audio/mixer), the CLI (`audio mixer`), Lua / Python (audio_mixer /
/// audio_mixer_set), MCP (aspect audio_mixer) and the per-source audio capture name the same
/// devices. The reports are DeviceState::AudioMixer / AudioChannels.
///
/// Keys: master, beeper, ay1, ay2, ay3 (the Fuller AY), fm1, fm2, covox (the Covox / SoundDrive, or the machine's own
/// DAC: the Sprinter's Covox-Blaster), gs, gs_mp3, moonsound_fm, moonsound_pcm, cd0..cd3 (the CD drive on IDE unit
/// 0..3), ms_ssg1, ms_ssg2, ms_fm1, ms_fm2, ms_saa, ms_pcm, ms_midi (the ZX-MultiSound), next_ay1..3, next_dac (the
/// ZX Spectrum Next's three AYs and its DAC).
///
/// Channels: `<source>.<channel>` names one channel of a row (mixerchannels.h): ay1.a, covox.lb, gs.3, fm1.2,
/// moonsound_fm.18, moonsound_pcm.24, ms_midi.10, next_dac.c. A channel takes muted, solo (within its row: a soloed
/// channel silences the row's other channels) and volume / gain_db (the MIDI parts: muted and solo only). Applied to
/// the device at the next frame start; host-side only (never machine state).
///
/// Worked example: {source: covox, muted: true} silences the Sprinter's DAC in the mix while the
/// AY plays on; /audio/capture {source: covox} still records the DAC (the device's own buffer).
/// {source: ay1.b, solo: true}: only channel B of the first AY is heard of that chip.
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

/// Apply to a device (or "master": muted only) or a channel ("ay1.a"); false with `error` on an unknown or absent
/// device or channel
bool Apply(EmulatorContext* context, const std::string& key, const Change& change, std::string& error);

/// Split "ay1.a" into the row "ay1" and the channel "a" (no dot: the channel is empty)
void SplitKey(const std::string& key, std::string& row, std::string& channel);

/// One source's or channel's state as the mixer report has it: {source, name, muted, solo, audible, volume, gain_db,
/// peak, active (rows), channels (a row's)}; false with `error` on an unknown or absent source / channel. "master":
/// {source, muted}
bool Read(EmulatorContext* context, const std::string& key, StateNode& out, std::string& error);

/// The current state of a source / channel for a toggle: muted or solo (`what`), nullopt when it is not fitted
std::optional<bool> Flag(EmulatorContext* context, const std::string& key, const char* what);

/// The levels of every row and channel (the WebSocket topic `audio.mixer`, the readout of the same name): compact,
/// what a meter or a mute key shows - {master: {muted}, solo, devices: [{source, muted, solo, audible, volume, peak,
/// active, channels: [{key, muted, solo, audible, volume, peak?}]}]}; published state only
StateNode Levels(EmulatorContext* context);

/// A device whose own buffer the audio capture can record (fitted, has a buffer); master always
bool Capturable(EmulatorContext* context, const std::string& key, AudioSourceType& type, std::string& error);
}  // namespace AudioMixer
