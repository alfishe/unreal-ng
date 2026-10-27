#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "emulator/sound/audiodeviceinfo.h"
#include "emulator/sound/mixerchannel.h"

/// A machine's own sound device that the port decoder owns and SoundManager
/// mixes (the Sprinter's Covox / Covox-Blaster DAC). The device keeps its
/// machine state on the emulated timeline; SoundManager only drives its frame
/// lifecycle and reads its stereo frame buffer, in the mixer's COVOX slot.
///
/// Zero cost for the other machines: SoundManager holds a null pointer and
/// tests it once per frame.
///
/// Frame contract (emulation thread):
///   - AudioFrameStart(suppressed) at every frame start, in every mode;
///   - AudioFrameEnd(samples) at every frame end, in every mode - samples = 0
///     when nothing is rendered (turbo without audio, sound off): the device
///     still advances its state to the end of the frame, because what it does
///     in emulated time (play position, interrupts) is machine state.
class IModelAudioSource
{
public:
    virtual ~IModelAudioSource() = default;

    /// Mixer row name ("Covox-Blaster")
    virtual const char* AudioSourceName() const = 0;

    virtual void AudioFrameStart(bool synthesisSuppressed) = 0;
    virtual void AudioFrameEnd(size_t samples) = 0;

    /// Interleaved stereo int16, `samples` pairs after AudioFrameEnd
    virtual int16_t* AudioBuffer() = 0;

    /// A level change landed in the last finished frame (the mixer LED)
    virtual bool AudioHadSoundLastFrame() const = 0;

    /// Core audio rate change (frame boundary)
    virtual void AudioSetSampleRate(size_t rate) = 0;

    /// Name / value pairs for the shared DAC report (GET /state/audio/covox and its CLI / MCP / Lua / Python
    /// twins), in display order; the machine's own report has the full state
    virtual std::vector<std::pair<std::string, std::string>> AudioStateFields() const { return {}; }

    /// region <Parts and channels (the mixer's rows and channel layer)>
    /// Parts: sources the device mixes into its one buffer that the mixer shows as rows of their own (the Next: three
    /// AYs and the DAC). A device with parts has no COVOX row: one row per part (AudioPartType), its mute / solo /
    /// volume folded by the mixer into the part's gain, which the device applies in its own mix (the buffer is mixed
    /// once, at unity). 0: the device is one row (COVOX)
    virtual size_t AudioPartCount() const { return 0; }
    virtual AudioSourceType AudioPartType(size_t part) const
    {
        (void)part;
        return AudioSourceType::Custom;
    }
    virtual std::string AudioPartName(size_t part) const
    {
        (void)part;
        return {};
    }
    /// The part rendered alone for the haptic feed (SoundManager::setHapticTaps): mono int16 at the core rate, the
    /// frame's `samples` after AudioFrameEnd, before the part's gain; nullptr when the device cannot. Off: no work
    virtual void SetAudioPartTap(size_t part, bool on)
    {
        (void)part;
        (void)on;
    }
    virtual const int16_t* AudioPartTap(size_t part) const
    {
        (void)part;
        return nullptr;
    }
    /// The part's gain in the device's mix (0 = silent, 1 = as the hardware mixes it); frame boundary
    virtual void SetAudioPartGain(size_t part, float gain)
    {
        (void)part;
        (void)gain;
    }
    /// The part's level in the last frame, 0..1, before its gain (the row's meter) and whether
    /// it carried sound
    virtual float AudioPartPeak(size_t part) const
    {
        (void)part;
        return 0.0f;
    }
    virtual bool AudioPartActive(size_t part) const
    {
        (void)part;
        return false;
    }
    /// The channels of one of the device's rows (a part, or COVOX); none by default
    virtual void AudioChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const
    {
        (void)type;
        (void)out;
    }
    /// One channel's effective state (muted: by the user or another channel's solo; volume 0..1); frame boundary
    virtual void SetAudioChannel(AudioSourceType type, size_t index, bool muted, float volume)
    {
        (void)type;
        (void)index;
        (void)muted;
        (void)volume;
    }
    /// endregion </Parts and channels>
};
