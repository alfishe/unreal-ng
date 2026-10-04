// libsam2695 - SF2 2.04 generator table and default modulators.
#include "bank/generators.h"

#include <algorithm>

namespace sam2695
{

namespace
{

constexpr int16_t kMin = -32768;
constexpr int16_t kMax = 32767;

// { default, min, max, presetAllowed, valid } per generator number (SF2 2.04 section 8.1.3)
constexpr GenInfo kGenTable[kGenCount] = {
    {0, kMin, kMax, false, true},          // 0 startAddrsOffset
    {0, kMin, kMax, false, true},          // 1 endAddrsOffset
    {0, kMin, kMax, false, true},          // 2 startloopAddrsOffset
    {0, kMin, kMax, false, true},          // 3 endloopAddrsOffset
    {0, kMin, kMax, false, true},          // 4 startAddrsCoarseOffset
    {0, -12000, 12000, true, true},        // 5 modLfoToPitch
    {0, -12000, 12000, true, true},        // 6 vibLfoToPitch
    {0, -12000, 12000, true, true},        // 7 modEnvToPitch
    {13500, 1500, 13500, true, true},      // 8 initialFilterFc
    {0, 0, 960, true, true},               // 9 initialFilterQ
    {0, -12000, 12000, true, true},        // 10 modLfoToFilterFc
    {0, -12000, 12000, true, true},        // 11 modEnvToFilterFc
    {0, kMin, kMax, false, true},          // 12 endAddrsCoarseOffset
    {0, -960, 960, true, true},            // 13 modLfoToVolume
    {0, 0, 0, false, false},               // 14 unused1
    {0, 0, 1000, true, true},              // 15 chorusEffectsSend
    {0, 0, 1000, true, true},              // 16 reverbEffectsSend
    {0, -500, 500, true, true},            // 17 pan
    {0, 0, 0, false, false},               // 18 unused2
    {0, 0, 0, false, false},               // 19 unused3
    {0, 0, 0, false, false},               // 20 unused4
    {-12000, -12000, 5000, true, true},    // 21 delayModLFO
    {0, -16000, 4500, true, true},         // 22 freqModLFO
    {-12000, -12000, 5000, true, true},    // 23 delayVibLFO
    {0, -16000, 4500, true, true},         // 24 freqVibLFO
    {-12000, -12000, 5000, true, true},    // 25 delayModEnv
    {-12000, -12000, 8000, true, true},    // 26 attackModEnv
    {-12000, -12000, 5000, true, true},    // 27 holdModEnv
    {-12000, -12000, 8000, true, true},    // 28 decayModEnv
    {0, 0, 1000, true, true},              // 29 sustainModEnv
    {-12000, -12000, 8000, true, true},    // 30 releaseModEnv
    {0, -1200, 1200, true, true},          // 31 keynumToModEnvHold
    {0, -1200, 1200, true, true},          // 32 keynumToModEnvDecay
    {-12000, -12000, 5000, true, true},    // 33 delayVolEnv
    {-12000, -12000, 8000, true, true},    // 34 attackVolEnv
    {-12000, -12000, 5000, true, true},    // 35 holdVolEnv
    {-12000, -12000, 8000, true, true},    // 36 decayVolEnv
    {0, 0, 1440, true, true},              // 37 sustainVolEnv
    {-12000, -12000, 8000, true, true},    // 38 releaseVolEnv
    {0, -1200, 1200, true, true},          // 39 keynumToVolEnvHold
    {0, -1200, 1200, true, true},          // 40 keynumToVolEnvDecay
    {0, 0, kMax, true, true},              // 41 instrument (terminal)
    {0, 0, 0, false, false},               // 42 reserved1
    {0, 0, 0, true, true},                 // 43 keyRange (kept as lo/hi)
    {0, 0, 0, true, true},                 // 44 velRange (kept as lo/hi)
    {0, kMin, kMax, false, true},          // 45 startloopAddrsCoarseOffset
    {-1, -1, 127, false, true},            // 46 keynum
    {-1, -1, 127, false, true},            // 47 velocity
    {0, 0, 1440, true, true},              // 48 initialAttenuation
    {0, 0, 0, false, false},               // 49 reserved2
    {0, kMin, kMax, false, true},          // 50 endloopAddrsCoarseOffset
    {0, -120, 120, true, true},            // 51 coarseTune
    {0, -99, 99, true, true},              // 52 fineTune
    {0, 0, kMax, false, true},             // 53 sampleID (terminal)
    {0, 0, 3, false, true},                // 54 sampleModes
    {0, 0, 0, false, false},               // 55 reserved3
    {100, 0, 1200, true, true},            // 56 scaleTuning
    {0, 0, 127, false, true},              // 57 exclusiveClass
    {-1, -1, 127, false, true},            // 58 overridingRootKey
    {0, kMin, kMax, false, false},         // 59 pitch: internal modulator destination only
    {0, 0, 0, false, false},               // 60 endOper
};

// SF2 2.04 section 8.4. Source encodings: index | CC flag | direction | polarity | type << 10.
constexpr ModulatorDef kDefaults[kDefaultModulatorCount] = {
    // 8.4.1 MIDI note-on velocity to initial attenuation: negative unipolar concave, 960 cB
    {0x0502, 48, 960, 0, 0},
    // 8.4.2 MIDI note-on velocity to filter cutoff: negative unipolar linear, -2400 cents
    {0x0102, 8, -2400, 0, 0},
    // 8.4.3 MIDI channel pressure to vibrato LFO pitch depth: 50 cents
    {0x000D, 6, 50, 0, 0},
    // 8.4.4 MIDI CC 1 (modulation wheel) to vibrato LFO pitch depth: 50 cents
    {0x0081, 6, 50, 0, 0},
    // 8.4.5 MIDI CC 7 (volume) to initial attenuation: negative unipolar concave, 960 cB
    {0x0587, 48, 960, 0, 0},
    // 8.4.6 MIDI CC 10 (pan) to pan: bipolar linear, 1000 (0.1 %)
    {0x028A, 17, 1000, 0, 0},
    // 8.4.7 MIDI CC 11 (expression) to initial attenuation: negative unipolar concave, 960 cB
    {0x058B, 48, 960, 0, 0},
    // 8.4.8 MIDI CC 91 to reverb send: 200 (0.1 %)
    {0x00DB, 16, 200, 0, 0},
    // 8.4.9 MIDI CC 93 to chorus send: 200 (0.1 %)
    {0x00DD, 15, 200, 0, 0},
    // 8.4.10 pitch wheel to initial pitch, scaled by pitch wheel sensitivity: bipolar linear, 12700 cents
    {0x020E, 59, 12700, 0x0010, 0},
};

} // namespace

const GenInfo& GenInfoOf(int gen)
{
    static constexpr GenInfo kInvalid = {0, 0, 0, false, false};
    if (gen < 0 || gen >= kGenCount)
        return kInvalid;
    return kGenTable[gen];
}

bool IsValidModSource(uint16_t src)
{
    const uint16_t index = src & modsrc::kIndexMask;
    if (src & modsrc::kCcFlag)
        return !(index == 0 || index == 6 || (index >= 32 && index <= 63) || (index >= 98 && index <= 101) ||
                 index >= 120);
    switch (index)
    {
        case modsrc::kNone:
        case modsrc::kNoteOnVelocity:
        case modsrc::kNoteOnKey:
        case modsrc::kPolyPressure:
        case modsrc::kChannelPressure:
        case modsrc::kPitchWheel:
        case modsrc::kPitchWheelSensitivity:
        case modsrc::kLink:
            return true;
        default:
            return false;
    }
}

const ModulatorDef* DefaultModulators()
{
    return kDefaults;
}

} // namespace sam2695
