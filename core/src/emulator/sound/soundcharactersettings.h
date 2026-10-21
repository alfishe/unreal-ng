#pragma once

#include <string>
#include <string_view>
#include <vector>

class SoundManager;

/// The sound-character settings every automation surface serves from one
/// source (CLI 'setting', WebAPI /settings, Lua, Python; MCP through the
/// WebAPI router), so names, accepted values and errors are identical:
///
///   ay_voicing    classic (default) | headphones | flat | warm | tv | small_speaker (alias legacy = classic)  AY / SSG tone voicing
///   ay_punch      on | off                                AY transient enhancement (HQ only)
///   ay_room       9db (default) | off | 15db | 14db | 13db | 12db | 6db | 3db | 2db | 1db
///   beeper_punch  on | off                                beeper attack enhancement (HQ only)
///   ay_stereo     abc (default from [SOUND] AYStereo) | acb | mono   the AY socket's stereo mixing
///   ay_chip       ay8910 | ym2149                          the AY socket's DAC curve (a TurboSound FM: fixed, refused)
///   fm_trim_db    -12 .. +12 in 0.5 dB steps               YM2203 FM level over the board's (TSFM, MultiSound)
///   neogs_stereo  separated | gs | mono                    the NeoGS listening choice ([NGS] StereoMode)
///   covox_dc_removal on | off                              the Covox / SounDrive's DC blocker
///   fir_filter    on | off                                 Sound HQ: the AY FIR decimation and the character chains
///                                                          (the `soundhq` feature)
///
/// A setting the machine does not have (no YM2203 FM, no Covox) reads "unavailable" and refuses a value.
/// Values go through SoundManager's thread-safe setters: visible at once,
/// applied at the next frame boundary. Runtime only - never written to an ini
/// (frontends persist their own preference). Design:
/// docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md §7
class SoundCharacterSettings
{
public:
    struct Descriptor
    {
        const char* name;
        const char* description;
        bool isBool;    ///< on/off setting (JSON bool in the WebAPI)
        bool isNumber;  ///< a number (JSON number in the WebAPI; text on the CLI)
    };

    static const std::vector<Descriptor>& Descriptors();

    /// nullptr when @p name is not a sound-character setting
    static const Descriptor* Find(std::string_view name);

    /// Current (requested) value as text: "classic", "on", "15db", ...
    static std::string Get(const SoundManager& sound, std::string_view name);

    /// Accepted values for @p name (visible voicing profiles, room IDs, on/off)
    static std::vector<std::string> AllowedValues(std::string_view name);

    /// Set from text. Bool settings accept on/off/true/false/1/0. Returns
    /// false with a message listing the accepted values on bad input
    static bool Set(SoundManager& sound, std::string_view name, std::string_view value, std::string& error);
};
