// libsam2695 - SF2 2.04 generator metadata (section 8.1.2 / 8.1.3) and the default modulators (8.4).
#pragma once

#include "sam2695/soundbank.h"

#include <cstdint>

namespace sam2695
{

struct GenInfo
{
    int16_t defaultValue;
    int16_t minValue;
    int16_t maxValue;
    bool presetAllowed; // false: an instrument-only generator, ignored at preset level (8.1.2)
    bool valid;         // false: unused / reserved numbers, ignored everywhere
};

const GenInfo& GenInfoOf(int gen);

inline int16_t GenDefault(Gen g) { return GenInfoOf(static_cast<int>(g)).defaultValue; }

// SF2 modulator source / transform encoding (8.2, 8.3)
namespace modsrc
{
constexpr uint16_t kIndexMask = 0x007F;
constexpr uint16_t kCcFlag = 0x0080;
constexpr uint16_t kDirection = 0x0100;  // 1 = max to min
constexpr uint16_t kPolarity = 0x0200;   // 1 = bipolar
constexpr int kTypeShift = 10;           // 0 linear, 1 concave, 2 convex, 3 switch
// General controller indices (CC flag clear)
constexpr uint16_t kNone = 0;
constexpr uint16_t kNoteOnVelocity = 2;
constexpr uint16_t kNoteOnKey = 3;
constexpr uint16_t kPolyPressure = 10;
constexpr uint16_t kChannelPressure = 13;
constexpr uint16_t kPitchWheel = 14;
constexpr uint16_t kPitchWheelSensitivity = 16;
constexpr uint16_t kLink = 127;
} // namespace modsrc

// True when the source operator is one SF2 2.04 allows (8.2.1): the general controllers above,
// or a MIDI CC other than 0, 6, 32-63, 98-101, 120-127.
bool IsValidModSource(uint16_t src);

// The ten SF2 2.04 default modulators (8.4.1 - 8.4.10)
constexpr int kDefaultModulatorCount = 10;
const ModulatorDef* DefaultModulators();

// Two modulators are the same modulator when source, destination and amount source match (9.5.1).
inline bool SameModulator(const ModulatorDef& a, const ModulatorDef& b)
{
    return a.src == b.src && a.dest == b.dest && a.amtSrc == b.amtSrc;
}

} // namespace sam2695
