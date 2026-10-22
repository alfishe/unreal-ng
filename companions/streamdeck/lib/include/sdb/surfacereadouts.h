// The core's readouts as a deck uses them (docs/inprogress/2026-10-19-streamdeck-integration/protocol.md §3,
// scenarios.md §4): a face's `state` path names a readout and maybe a field of it ("run.state.paused" = the field
// `paused` of `run.state`, derived from its `state`; "toggle.mach.turbo_toggle" = the readout itself, {on}); the
// strip's status draws whole readouts (`ttd` = ttd.position, `run` = run.state, `tape` = media.tape). The surface
// client subscribes to what a deck's scenario names and hands each value to the runtime.
#pragma once
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sdb/scenario.h"

namespace sdb::scn
{

// ttd.position run.state machine media.tape media.disk audio.mute debug.mode (toggle.<id> besides)
[[nodiscard]] std::span<const std::string_view> BaseReadouts();

struct ReadoutRef
{
    std::string name;   // the readout ("run.state", "toggle.mach.turbo_toggle")
    std::string field;  // "" = the whole value
};
// "run.state.paused" -> {run.state, paused}; "toggle.mach.turbo_toggle" -> {toggle.mach.turbo_toggle, ""} (".on" at
// the end is the field `on`); an unknown group: its first two parts are the name ("media.cart.loaded" -> media.cart)
[[nodiscard]] ReadoutRef SplitReadoutPath(std::string_view path);
// The value a face reads from a readout's value: the field (run.state's paused / running / stopped come from its
// `state`), or the whole value; null when it has no such field
[[nodiscard]] json ReadoutField(const ReadoutRef& ref, const json& value);

// What a scenario needs from the core
struct SurfaceNeeds
{
    std::vector<std::string> readouts;  // readout names, sorted, once each
    std::vector<std::string> paths;     // the faces' state paths, sorted, once each
    json target = "follow";             // settings.target
    bool timeline = false;              // a strip shows the TTD timeline (strip.status ttd, a dial scrubbing time)
};
[[nodiscard]] SurfaceNeeds NeedsOf(const Scenario& s);

}  // namespace sdb::scn
