#pragma once

/// @file surfacehaptics.h
/// @brief What a control surface feels per context (control surfaces profiles.md §1, architecture.md §9): the
/// `haptics` section of a debug profile, keyed by layer (`debug`, `@ttd`, `@debug.*`), gives the steps per turn, whether
/// each step clicks, the pattern each timeline mark plays and the marks' rate limit. The game haptics of a play profile
/// (hapticprofile.h) are another form of the same section.
///
/// @code
///   haptics:
///     "@ttd":          { steps: 36, clicks: true,  marks: { second: tick, ten_seconds: double, edge: buzz } }
///     "@ttd.timecode": { steps: 36, clicks: false, marks: { second: tick, edge: buzz }, min_interval_ms: 40 }
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "hostbackend.h"
#include "profiles/json.h"

namespace controllers
{

/// "tick", "double", "buzz"
const char* HapticPatternName(HapticPattern pattern);
bool HapticPatternFromName(const std::string& name, HapticPattern& out);

/// The timeline marks a `ttd.seek_by` result names (core/src/debugger/ttd/timelinemarks.h): second, ten_seconds,
/// bookmark, edge, barrier
const std::vector<std::string>& MarkKinds();

/// What a device feels while one layer of its debug profile is the top one
struct LayerHaptics
{
    StepsOut steps;                               ///< `steps` (default 36), `clicks` (default true)
    std::map<std::string, HapticPattern> marks;   ///< mark kind -> pattern; a kind not named plays nothing
    uint32_t minIntervalMs = 40;                  ///< `min_interval_ms`: marks closer than this merge
};

struct SurfaceHaptics
{
    /// router layer name ("debug", "@ttd", "@debug.*") -> its feel
    std::map<std::string, LayerHaptics> layers;

    bool Empty() const
    {
        return layers.empty();
    }
    const LayerHaptics* Find(const std::string& layer) const
    {
        const auto it = layers.find(layer);
        return it == layers.end() ? nullptr : &it->second;
    }
};

/// The per-layer form; false with the reason, which names the place. Keys: `debug` or a context layer (`@ttd`)
bool ParseSurfaceHaptics(const JsonValue& section, SurfaceHaptics& out, std::string& error);

/// The pattern of the strongest mark the layer names a pattern for; @p kinds strongest first, as the action gives
/// them. False when none of them plays
bool PatternForMarks(const LayerHaptics& layer, const std::vector<std::string>& kinds, HapticPattern& pattern,
                     std::string& kind);

/// The marks' rate limit per device (architecture.md §9): a mark within the interval of the last one played is
/// dropped, except an `edge` or a `barrier`, which always plays
class MarkLimiter
{
public:
    bool Admit(const HostDeviceId& device, const std::string& kind, uint64_t nowMs, uint32_t minIntervalMs);
    void Forget(const HostDeviceId& device)
    {
        _last.erase(device);
    }

private:
    std::map<HostDeviceId, uint64_t> _last;
};

}  // namespace controllers
