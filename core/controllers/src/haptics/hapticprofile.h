#pragma once

/// @file hapticprofile.h
/// @brief The `haptics` section of a game profile (haptics.md §5-§7): named sources (the sound filters of §4 and the
/// frame probes of §5) and the rules that play an effect of the built-in library (§6.2) when a source fires.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/controllers/ihapticfeed.h"
#include "haptics/hapticmixer.h"
#include "profiles/json.h"

namespace controllers
{

enum class HapticSourceKind : uint8_t
{
    AyNoise,        ///< filter `ay-noise`: level, the loudest noise channel
    AyEnvelope,     ///< filter `ay-envelope`: event, an envelope restart with a decaying shape
    BeeperDensity,  ///< filter `beeper-density`: level, EAR edges per ms above `min_rate_khz`
    Write,          ///< probe `write`: event, the byte changed between two frame ends (`each_write`: in any write) and
                    ///< meets `when`
    Value,          ///< probe `value`: level, the byte times `scale` / 255
    Frame,          ///< probe `frame`: level 1 while `mem & mask == value` at the frame end
    Exec,           ///< probe `exec`: event, the PC reached the address this frame
    Read,           ///< probe `read`: event, a read of the address this frame
    PortIn,         ///< probe `port-in`: event, an IN from the port
    PortOut,        ///< probe `port-out`: event, an OUT to the port (with `value`: whose `value & mask` matches)
    Envelope,       ///< filter `envelope`: level, the stream's smoothed level times `scale`
    Lowband,        ///< filter `lowband`: level, the band's level times `scale`
    Onset,          ///< filter `onset`: event, the stream's level rose by `rise_db` within `within_ms`
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
    bool hasValue = false;  ///< `value` was given (port-out tests it only then)
    uint8_t mask = 0xFF;
    float scale = 1.0f;
    /// write: every write is seen (a breakpoint probe), so a change undone within one frame fires too
    bool eachWrite = false;
    /// value: `word: true` reads a little-endian 16-bit value at `address` and `address + 1`, scaled as a byte is
    /// (value * scale / 255, so a speed of 0..511 takes `scale: 0.5`)
    bool word = false;
    /// port-in / port-out: `port`, matched under `port_mask`
    uint16_t port = 0;
    uint16_t portMask = 0xFFFF;
    /// envelope / lowband / onset: the stream (`source`: mix, beeper, ay:N, covox, soundrive, gs, neogs, next-dac)
    HapticStream stream = HapticStream::Mix;
    float attackMs = 5.0f;
    float releaseMs = 80.0f;
    float lowHz = 20.0f;
    float highHz = 150.0f;
    float riseDb = 12.0f;
    float withinMs = 30.0f;
    float floor = 0.05f;  ///< onset: the level must reach it (0..1 of full scale)
    /// `if:` (the core condition language, docs/features/conditional-breakpoints.md): a breakpoint probe counts a hit
    /// only when it holds at the access (with its access operands); any other source fires only when it holds at the
    /// frame end. `when:` keeps its meaning beside it; an `each_write` probe's byte before is then the one of the
    /// last write the condition held for. Empty: none
    std::string condition;

    bool IsEvent() const
    {
        return kind == HapticSourceKind::AyEnvelope || kind == HapticSourceKind::Write ||
               kind == HapticSourceKind::Exec || kind == HapticSourceKind::Read || kind == HapticSourceKind::PortIn ||
               kind == HapticSourceKind::PortOut || kind == HapticSourceKind::Onset;
    }
    /// Read at the frame end (IHapticFeed::Watches)
    bool IsProbe() const
    {
        return (kind == HapticSourceKind::Write && !eachWrite) || kind == HapticSourceKind::Value ||
               kind == HapticSourceKind::Frame;
    }
    /// A breakpoint probe (IHapticFeed::Probes)
    bool IsBreakpointProbe() const
    {
        return (kind == HapticSourceKind::Write && eachWrite) || kind == HapticSourceKind::Exec ||
               kind == HapticSourceKind::Read || kind == HapticSourceKind::PortIn || kind == HapticSourceKind::PortOut;
    }
    /// Reads a sample stream (IHapticFeed::Streams)
    bool IsStream() const
    {
        return kind == HapticSourceKind::Envelope || kind == HapticSourceKind::Lowband ||
               kind == HapticSourceKind::Onset;
    }
    /// The breakpoint probe this source arms (IsBreakpointProbe)
    HapticProbe Probe() const;
    /// Where `if:` is checked, as the access operands it may use (BreakpointConditionTrigger: BRK_COND_EXEC for an
    /// exec probe, ..., 0 at the frame end)
    uint8_t ConditionTrigger() const;
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
    /// `hd: {frequency_hz, low_hz, high_hz}`: the HD frequency of each band over the effect's own (0: the effect's);
    /// `frequency_hz` sets both bands, `low_hz` / `high_hz` one each
    uint16_t lowHz = 0;
    uint16_t highHz = 0;

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

/// The `haptics` section -> its sources and rules; false with the reason, which names the place
bool ParseHapticProfile(const JsonValue& section, HapticProfile& out, std::string& error);

/// Checks an `if:` text for a source whose condition takes the access operands @p trigger (ConditionTrigger): false
/// with the reason. The core installs it (this library does not link the condition engine); none installed: every
/// text passes here and the machine checks it when the profile is armed
using HapticConditionCheck = bool (*)(const std::string& text, uint8_t trigger, std::string& error);
void SetHapticConditionCheck(HapticConditionCheck check);

/// The built-in effect library (haptics.md §6.2): peak levels on the handle motors, a length, an envelope; false for an
/// unknown id
bool BuiltinHapticEffect(const std::string& id, HapticEffect& out);
/// The ids of the library, for errors and the Haptics tab
const std::vector<std::string>& BuiltinHapticEffectIds();

/// The stream selector of a DSP filter's `source` ("mix", "beeper", "ay:1", "covox", "soundrive", "gs", "neogs",
/// "next-dac"); false for another
bool ParseHapticStream(const std::string& text, HapticStream& out);
const char* HapticStreamName(HapticStream stream);

/// "Add probe from debugger" (haptics.md §7): @p probe (a breakpoint's address or port) as a new source of the
/// `haptics` section @p haptics (made an object when null) with a rule that plays @p play; a write probe sees every
/// write (`each_write`, `when: changed`). The new source's name in @p name ("probe-1", "probe-2", ...); false with the
/// reason when the result does not parse
bool AddHapticProbeSource(JsonValue& haptics, const HapticProbe& probe, const std::string& play, std::string& name,
                          std::string& error);

/// Move @p levels to @p target: the loudest handle level goes to every target channel
RumbleLevels RetargetLevels(const RumbleLevels& levels, uint8_t target);

}  // namespace controllers
