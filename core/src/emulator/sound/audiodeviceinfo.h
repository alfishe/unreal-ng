#pragma once

#include <string>

/// Audio source types for device/channel selection (shared with recording)
enum class AudioSourceType
{
    MasterMix,
    Beeper,
    AY1_All,
    AY2_All,
    AY3_All,
    COVOX,
    GeneralSound,
    Moonsound_FM,
    Moonsound_PCM,
    AY1_ChannelA, AY1_ChannelB, AY1_ChannelC,
    AY2_ChannelA, AY2_ChannelB, AY2_ChannelC,
    AY3_ChannelA, AY3_ChannelB, AY3_ChannelC,
    FM1,  // TSFM chip 0 FM-only buffer (design §7.2)
    FM2,  // TSFM chip 1 FM-only buffer
    GeneralSoundMp3, // NeoGS MP3 decoder output (a separate analogue path on the board)
    CdAudio0, CdAudio1, CdAudio2, CdAudio3,  // CD-DA line output of the ATAPI CD drive on IDE unit 0..3 (ide0.master .. ide1.slave)
    Custom
};

/// The mixer source of the CD drive on IDE unit 0..3
inline AudioSourceType CdAudioSourceFor(int unit)
{
    return static_cast<AudioSourceType>(static_cast<int>(AudioSourceType::CdAudio0) + unit);
}
/// The IDE unit of a CD audio source, -1 for any other source
inline int CdAudioUnitOf(AudioSourceType type)
{
    const int unit = static_cast<int>(type) - static_cast<int>(AudioSourceType::CdAudio0);
    return unit >= 0 && unit < 4 ? unit : -1;
}

/// Per-device descriptor for the registry-driven mixer
struct AudioDeviceInfo
{
    AudioSourceType type;
    std::string     name;

    // Monitor state (runtime, per emulator instance)
    bool  mute   = false;
    bool  solo   = false;
    float volume = 1.0f;

    // Read-only status for UI (updated each frame)
    float peak           = 0.0f;
    bool  activeRecently = false;
};
