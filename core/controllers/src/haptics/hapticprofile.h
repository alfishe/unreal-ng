#pragma once

/// @file hapticprofile.h
/// @brief The `haptics` section of a game profile (haptics.md §5-§7): named sources (the sound filters of §4 and the
/// frame probes of §5) and the rules that play an effect of the built-in library (§6.2) when a source fires.

#include <cstdint>
#include <string>
#include <vector>

#include "haptics/hapticmixer.h"
#include "profiles/json.h"

namespace controllers
{

enum class HapticSourceKind : uint8_t
{
    AyNoise,        ///< filter `ay-noise`: level, the loudest noise channel
    AyEnvelope,     ///< filter `ay-envelope`: event, an envelope restart with a decaying shape
    BeeperDensity,  ///< filter `beeper-density`: level, EAR edges per ms above `min_rate_khz`
    Write,          ///< probe `write`: event, the byte changed between two frame ends and meets `when`
    Value,          ///< probe `value`: level, the byte times `scale` / 255
    Frame,          ///< probe `frame`: level 1 while `mem & mask == value` at the frame end
};

/// How a `write` probe's new byte must relate to the old one or to `value`
enum class HapticWhen : uint8_t
{
    Changed,
    Decreased,
    Increased,
    Equal,
    NotEqual,
    Less,
    Greater,
};

struct HapticSource
{
    std::string name;
    HapticSourceKind kind = HapticSourceKind::AyNoise;
    unsigned chip = 0;  ///< `source: "ay:N"`; filters read every chip when absent
    bool anyChip = true;
    float minRateKhz = 2.0f;
    /// probes: a CPU address, or `page:<n>:<addr>` (RAM page n, the offset `addr & #3FFF`)
    uint16_t address = 0;
    uint16_t page = 0xFFFF;
    HapticWhen when = HapticWhen::Changed;
    uint8_t value = 0;
    uint8_t mask = 0xFF;
    float scale = 1.0f;

    bool IsEvent() const
    {
        return kind == HapticSourceKind::AyEnvelope || kind == HapticSourceKind::Write;
    }
    bool IsProbe() const
    {
        return kind == HapticSourceKind::Write || kind == HapticSourceKind::Value || kind == HapticSourceKind::Frame;
    }
};

/// Where an effect plays: the abstract targets of §6.1, ORed
enum HapticTarget : uint8_t
{
    kHandleLow = kRumbleLow,
    kHandleHigh = kRumbleHigh,
    kTriggerLeft = kRumbleLeftTrigger,
    kTriggerRight = kRumbleRightTrigger,
    kTargetAll = 15,
};

struct HapticRule
{
    std::string on;      ///< a source name
    std::string play;    ///< a built-in effect id
    float scale = 1.0f;  ///< times the effect's strength
    uint8_t target = 0;  ///< 0: the effect's own; else the first of `trigger.right|handle.high`
    std::vector<uint8_t> fallbacks;  ///< the rest, in order: the first a device has plays (Haptics_Fallback)
    uint8_t priority = 0;            ///< `priority`: over the effect's own when set

    /// The target for a device with @p actuators: the first alternative it has all of, else the last one
    uint8_t TargetFor(uint8_t actuators) const;
};

struct HapticProfile
{
    std::vector<HapticSource> sources;
    std::vector<HapticRule> rules;

    bool Empty() const
    {
        return rules.empty();
    }
    const HapticSource* Find(const std::string& name) const;
};

/// The `haptics` section -> its sources and rules; false with the reason, which names the place. The probes `exec`,
/// `read`, `port-out` and `port-in` are refused: they need the non-breaking breakpoint action (TODO.md H3)
bool ParseHapticProfile(const JsonValue& section, HapticProfile& out, std::string& error);

/// The built-in effect library (haptics.md §6.2): peak levels on the handle motors, a length, an envelope; false for an
/// unknown id
bool BuiltinHapticEffect(const std::string& id, HapticEffect& out);
/// The ids of the library, for errors and the Haptics tab
const std::vector<std::string>& BuiltinHapticEffectIds();

/// Move @p levels to @p target: the loudest handle level goes to every target channel
RumbleLevels RetargetLevels(const RumbleLevels& levels, uint8_t target);

}  // namespace controllers
