#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
};
