#pragma once

/// @file machinepreset.h
/// @brief The preset machine verb for every surface (docs/inprogress/2026-11-12-re-api-efficiency/analysis.md V3): one
/// call creates a reproducible machine for a reverse-engineering or verification run and answers when it is ready -
/// the cards in their slots, the features set, the sound muted, the machine started and paused at power-on,
/// run control taken. The WebAPI (and MCP through it), the CLI, Lua and Python turn their input into a StateNode
/// object and show the reply; arguments, defaults, refusals, the answer's fields and its text live here, so the
/// surfaces cannot drift apart (the SlotControl / analysis::Execute pattern).
///
/// Arguments (an object; all optional but `model`):
///   model         the hardware model's short name, as for a create ("48K", "PENTAGON", a machine variant)
///   ram           "zero" (default: every RAM page reads 0, a reproducible run) | "random"; alias ram_power_on
///   cards         cards to fit: a list of "edge.1=kempston" strings or {slot, card, options, adapter, replace}
///                 objects (one string may hold several, comma separated). Planned as a slot plug is
///                 (SlotManager::PlanChanges) against the model's own slot set, and the machine is created with the
///                 planned set: there is no restart, so nothing to wait for. `replace` allows the removals the plan
///                 lists (replaceIfIncompatible)
///   features      {name: bool}: FeatureManager features by id or alias ({"kempstonmouse": false})
///   muted         true (default): the mixer's master is muted
///   paused        true (default): the machine is started and left paused at power-on, before its first instruction; false: running
///   fast_tape     false (default): the tape accelerators are off (features fasttape: the ROM LD-BYTES trap, and
///                 turbotape: turbo while the tape plays), a tape loads in real machine time; true: both on. A name
///                 in `features` wins over it
///   take_control  true (default): the caller holds the new machine's run control (the surface's own session)
///   replace       false (default): the machine is created next to the existing ones, as a create does; true: every
///                 other instance is destroyed once the new one is built (refused while another client's explicit
///                 session holds one of them)
///   select        false (default): the selection is left alone (several machines run side by side; which one is
///                 active is the caller's choice); true: the new machine becomes the selected one (the one a GUI
///                 shows, the default of calls with no target)
///   client, lease_seconds   the session's name and idle lease, as for take_control
///
/// Everything that can be checked without a machine is checked first (argument names and types, ram, feature
/// names, card ids and options): a refusal names the valid values and nothing is created. The slot plan needs the
/// model's configuration, so it is made while the machine is being created; a refused plan, an unknown model and
/// any later failure remove the never-started machine: no instance is left behind, the existing ones are untouched.
///
/// @code
///   StateNode args = StateNode::Object();
///   args["model"] = "48K";
///   args["cards"].push("edge.1=kempston");
///   args["features"]["kempstonmouse"] = false;
///   MachinePresetReply reply = MachinePreset::Execute(args, caller);   // caller: the surface's session functions
///   // reply.text: "Preset machine <id> ready: 48K (48 KB), RAM zero, paused at frame 0 t 3. ..."
/// @endcode

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

/// What the calling surface brings: its name and its control sessions
struct MachinePresetCaller
{
    /// "webapi", "mcp", "cli", "lua", "python": named in the answer
    std::string surface = "internal";
    /// Opens the surface's run-control session on the new machine for the caller. True with the session id in
    /// `session` ("" when the surface has no id to show); false with the reason. Not set: the surface has no
    /// sessions, take_control is answered as not taken
    std::function<bool(Emulator& emulator, const std::string& client, uint32_t leaseSeconds, std::string& session,
                       std::string& reason)>
        takeControl;
    /// The caller's own session holds this machine (replace: such a machine may be destroyed). Not set: no
    std::function<bool(Emulator& emulator)> holdsControl;
};

struct MachinePresetReply
{
    /// "created", "bad-request" (400), "refused" (409: the slot plan, a feature, the control session, replace),
    /// "failed" (500)
    std::string status = "created";
    int httpStatus = 201;
    std::string message;                 ///< the refusal; "" when created
    std::string text;                    ///< the answer for a reader: the effective settings line by line
    StateNode body = StateNode::Object();
    std::shared_ptr<Emulator> emulator;  ///< created: the machine

    bool Ok() const
    {
        return status == "created";
    }

    /// The envelope every surface returns: ok, status, message (a refusal), text, then the body's fields
    StateNode ToValue() const;
};

class MachinePreset
{
public:
    static MachinePresetReply Execute(const StateNode& args, const MachinePresetCaller& caller);

    /// The argument names Execute takes (an unknown one is refused with this list)
    static const std::vector<std::string>& ArgumentNames();

    /// The CLI form "<model> [name=value ...]" as the arguments object: ram=zero, card=edge.1=kempston (repeatable,
    /// also cards=a=b,c=d), feature=kempstonmouse=off (repeatable), muted=off, paused=off, fast_tape=on,
    /// take_control=off, replace=on, select=on, client=<name>, lease_seconds=<n>. False with the reason
    static bool ArgumentsFromWords(const std::vector<std::string>& words, StateNode& args, std::string& error);
};
