#pragma once

/// @file slotcontrol.h
/// @brief The one place that implements the slot verbs for every surface (ZX-bus slots SL-7,
/// docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §9): the WebAPI (and MCP through it), the CLI, Lua, Python
/// and the Qt slot window turn their input into a SlotControlRequest and the SlotControlReply into their output.
/// Replies are StateNode trees (the tree every surface already converts to JSON, sol::table, py::dict, text), built
/// from the core types 1:1 (SlotManager::Result through DeviceState::Slots, slots::SlotPlan, SlotChangeResult,
/// SlotManager::CarryReport), so the surfaces cannot drift apart.
///
/// Verbs:
///   list      the slot report (DeviceState::Slots): buses, slots, fitted cards, built-ins
///   catalog   every card of the reference data with its options, functions, ports, media and how it would fit this
///             machine (suggested slot, fit real / adapter / unrealistic, outcome fits / needs-replace / refused)
///   matrix    the compatibility tables (compatibility-matrix.md §1-§4) as markdown, one per table
///   plug      a card into a slot (SlotChange::Run): plan, refusal, dry run or the restart
///   remove    the card out of a slot
///   set       a slot's card options
///   gs        the General Sound personality (owner decision Q10): a plug of gs / gs-lw / neogs into the GS slot,
///             replacing the card there, applied by a restart like every slot change
///
/// Example (what a surface does):
/// @code
///   SlotControlRequest request;
///   request.verb = "plug";  request.emulatorId = id;  request.slot = "zxbus.next";  request.card = "multisound";
///   request.options = "dip=ym,saa gsRam=2m";  request.replaceIfIncompatible = true;
///   SlotControlReply reply = SlotControl::Execute(request);
///   http.status = reply.httpStatus;  http.body = reply.ToJson();   // reply.emulator: the restarted machine
/// @endcode

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "emulator/slots/slotchange.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
struct CONFIG;

struct SlotControlRequest
{
    std::string verb;               ///< list, catalog, matrix, plug, remove, set, gs
    std::string emulatorId;         ///< the instance; "" = the selected one
    std::string slot;               ///< plug: "zxbus.2", "zxbus.next", "ay-socket" ("" = the planner's choice)
    std::string card;               ///< plug: card id; gs: the personality (gs, gs-lw, neogs or the GS names z80, lw, ngs)
    /// plug / set: "dip=ym,saa gsRam=2m" (space-separated name=value, comma list for a set option; "none" = empty).
    /// set merges them over the slot's current options
    std::string options;
    std::string adapter;            ///< plug: an adapter in the slot (id)
    bool replaceIfIncompatible = false;   ///< allow the removals / an unrealistic fit the plan lists (Q1, Q5)
    bool dryRun = false;            ///< the plan only
    std::string media;              ///< what happens to unsaved media of removed cards: "" (refuse), save, discard
    std::string table;              ///< matrix: one table ("functions", "cards", "card-x-card", ...); "" = all
    /// Start the restarted machine when the old one ran (every automation surface); the Qt window starts it itself
    bool startWhenRunning = true;
    /// Called with the old machine stopped, before it is destroyed (a GUI unbinds its views here)
    std::function<void(Emulator& old)> beforeRelease;
};

struct SlotControlReply
{
    /// "ok" (list, catalog, matrix), "applied", "dry-run", "refused", "recording", "no-machine", "failed",
    /// "bad-request"
    std::string status = "ok";
    std::string message;
    int httpStatus = 200;
    /// Verb-specific fields, merged into the envelope: the report (list), cards (catalog), tables (matrix), plan /
    /// restart / media (changes)
    StateNode body = StateNode::Object();
    /// Applied: the restarted machine (created; started when the old one ran and startWhenRunning)
    std::shared_ptr<Emulator> emulator;

    bool Ok() const
    {
        return status == "ok" || status == "applied" || status == "dry-run";
    }

    /// The envelope every surface returns: ok, status, message, then the body's fields
    StateNode ToValue() const;
    std::string ToJson() const;
    /// For people (the CLI): the plan, the refusal, the restart and the media, line by line
    std::string ToText() const;
};

class SlotControl
{
public:
    static SlotControlReply Execute(const SlotControlRequest& request);

    static const std::vector<std::string>& Verbs();

    /// The machine's card catalog (verb catalog) for a slot set; the card nodes the catalog lists
    static StateNode Catalog(const SlotManager::Result& current);
    /// A change plan as the surfaces show it (also a refused one): the plan engine's plan, the refusal, the new
    /// [SLOTS] lines
    static StateNode PlanValue(const SlotManager::ChangePlan& plan);
    /// What a model switch did with the cards (ModelSwitchResult::slotCarry), for every surface's model-switch reply
    static StateNode CarryValue(const SlotManager::CarryReport& carry);
    /// The text lines of a plan for people (the CLI, the Qt preview)
    static std::vector<std::string> PlanLines(const slots::SlotPlan& plan);

    /// A create-time slot set (the general `"slots": {...}` form of a create request, architecture.md §9): the
    /// [SLOTS] key / value pairs ("zxbus.1" = "multisound", "zxbus.1.dip" = "ym,gs") become the new machine's slot set,
    /// replacing the INI's. False with the malformed lines in `error`
    static bool CreateOverride(const std::vector<std::pair<std::string, std::string>>& keyValues,
                               std::function<void(CONFIG&)>& out, std::string& error);

    /// "save" / "discard" / "" -> the disposition; false for anything else
    static bool ParseMediaDisposition(const std::string& text, slots::MediaDisposition& out);
};
