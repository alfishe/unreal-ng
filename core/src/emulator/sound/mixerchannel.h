#pragma once

#include <cstdint>
#include <string>

/// @file mixerchannel.h
/// @brief One sub-channel of a mixer row (AY A / B / C, a Covox DAC, an OPL4 FM channel or PCM slot, a YM2203 FM
/// channel, a General MIDI part, a GS DAC channel): what a device tells the mixer about it, and the host gain a device
/// applies to it.
///
/// The mixer (SoundManager, the channel layer) keeps each channel's mute / solo / volume and hands the device the
/// effective result at a frame boundary: `muted` (muted by the user, or another channel of the same row soloed) and
/// `volume` (0..1). Host-side only: none of it is machine state, so TTD, snapshots and the program never see it, and a
/// row whose channels were never touched renders exactly as before (unity everywhere: bit-exact).

/// What a device says about one of its channels
struct MixerChannelDesc
{
    std::string key;      ///< "a", "1", "la": the part after the dot of the full key ("ay1.a", "moonsound_fm.7")
    std::string name;     ///< "A", "FM 7", "Left A"
    bool hasVolume = true;  ///< false: mute (and solo) only (the SAM2695's parts)
    bool hasMeter = false;  ///< the device measures the channel's level (MixerChannelPeak)
};

/// A channel's host gain in Q15 (32768 = unity, 0 = silent) for the integer render paths: exact on every compiler
inline int32_t MixerChannelGainQ15(bool muted, float volume)
{
    if (muted || !(volume > 0.0f))
        return 0;
    if (volume >= 1.0f)
        return 32768;
    return static_cast<int32_t>(volume * 32768.0f + 0.5f);
}

/// `value` x a Q15 gain, truncated toward zero; the value itself at unity
inline int32_t ApplyMixerGainQ15(int32_t value, int32_t gainQ15)
{
    if (gainQ15 == 32768)
        return value;
    return static_cast<int32_t>((static_cast<int64_t>(value) * gainQ15) / 32768);
}
