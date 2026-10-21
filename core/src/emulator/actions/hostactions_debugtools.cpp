#include "emulator/actions/hostactions_debugtools.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/analyzers/aylog/ayloganalyzer.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "debugger/analyzers/trdos/trdosanalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpoints/breakpointselection.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolcontrol.h"
#include "debugger/ports/portwrite.h"
#include "debugger/tracecontrol/tracecontrol.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/memory/memory.h"

namespace actions
{
namespace
{

/// region <Rules and results>

uint32_t RouteScope(const char* method, const std::string& subpath)
{
    return automation_gate::ChangesMachine(method, subpath) ? automation_gate::CompanionScopeOf(method, subpath) : 0;
}

ActionRules RouteRules(const char* method, const char* subpath)
{
    ActionRules rules;
    rules.scope = ActionRules::Fixed(RouteScope(method, subpath));
    return rules;
}

ActionResult BadRequest(std::string message)
{
    return ActionResult::Failure("Bad Request", std::move(message));
}

ActionResult Conflict(std::string message)
{
    return ActionResult::Failure("Conflict", std::move(message));
}

ActionResult NotFound(std::string message)
{
    return ActionResult::Failure("Not Found", std::move(message));
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A number as the debugger writes it: decimal, 0x.., #.., $.. or ..h; false for anything else or above `max`
bool ParseNumberText(const std::string& text, uint32_t max, uint32_t& out)
{
    std::string digits = text;
    int base = 10;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits = digits.substr(2), base = 16;
    else if (digits.size() > 1 && (digits[0] == '#' || digits[0] == '$'))
        digits = digits.substr(1), base = 16;
    else if (digits.size() > 1 && (digits.back() == 'h' || digits.back() == 'H'))
        digits.pop_back(), base = 16;
    if (digits.empty())
        return false;
    for (const char c : digits)
        if (!(base == 16 ? std::isxdigit(static_cast<unsigned char>(c)) : std::isdigit(static_cast<unsigned char>(c))))
            return false;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(digits.c_str(), &end, base);
    if (end == nullptr || *end != '\0' || value > max)
        return false;
    out = static_cast<uint32_t>(value);
    return true;
}

/// A numeric param in any of the debugger's forms; nullopt when absent, `error` when present and not a number <= max
std::optional<uint32_t> UIntParam(const ActionValue& value, const char* name, uint32_t max, std::string& error)
{
    const std::optional<std::string> text = TextParam(value, name);
    if (!text)
        return std::nullopt;
    uint32_t number = 0;
    if (!ParseNumberText(*text, max, number))
    {
        error = std::string(name) + ": a number 0.." + std::to_string(max) + " (decimal, 0x.., #.., $.. or ..h)";
        return std::nullopt;
    }
    return number;
}

BreakpointManager* Breakpoints(Emulator& emulator)
{
    return emulator.GetBreakpointManager();
}

LabelManager* Labels(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetLabelManager()
                                                                   : nullptr;
}

/// endregion </Rules and results>

/// region <Breakpoints>

/// The port presets of dbg.bp_add (use cases D4.2): the port and the mask the 128K / +3 decoders use
struct PortPreset
{
    const char* name;
    uint16_t port;
    uint16_t mask;
    const char* what;
};

constexpr PortPreset kPortPresets[] = {
    {"fe", 0x00FE, 0x00FF, "the ULA: border, beeper, keyboard (#xxFE)"},
    {"7ffd", 0x7FFD, 0x8002, "128K paging (A15 = 0, A1 = 0)"},
    {"1ffd", 0x1FFD, 0xF002, "+3 paging (A15..A12 = 0001, A1 = 0)"},
    {"fffd", 0xFFFD, 0xC002, "the AY register select (A15 = A14 = 1, A1 = 0)"},
    {"bffd", 0xBFFD, 0xC002, "the AY data (A15 = 1, A14 = 0, A1 = 0)"},
};

const PortPreset* FindPortPreset(std::string name)
{
    name = Lower(std::move(name));
    if (!name.empty() && (name[0] == '#' || name[0] == '$'))
        name = name.substr(1);
    else if (name.size() > 2 && name.compare(0, 2, "0x") == 0)
        name = name.substr(2);
    for (const PortPreset& preset : kPortPresets)
        if (name == preset.name)
            return &preset;
    return nullptr;
}

/// A memory address param: a number or a label's name (its CPU address)
std::optional<uint16_t> AddressParam(Emulator& emulator, const ActionValue& value, const char* name, bool labels,
                                     std::string& error)
{
    const std::optional<std::string> text = TextParam(value, name);
    if (!text)
        return std::nullopt;
    uint32_t number = 0;
    if (ParseNumberText(*text, 0xFFFF, number))
        return static_cast<uint16_t>(number);
    if (labels)
        if (LabelManager* manager = Labels(emulator))
            if (const auto label = manager->GetLabelByName(*text))
                return label->address;
    error = std::string(name) + ": an address 0..65535 (decimal, 0x.., #.., $..)" + (labels ? " or a label's name" : "");
    return std::nullopt;
}

/// dbg.bp_add: the spec from the params (the fields of POST /breakpoints)
bool BreakpointSpecFromParams(Emulator& emulator, const ActionValue& value, BreakpointSpec& spec, std::string& kind,
                              std::string& error)
{
    kind = Lower(TextParam(value, "kind").value_or("exec"));
    if (kind == "exec" || kind == "execution" || kind == "execute" || kind == "bp")
        spec.type = BRK_MEMORY, spec.access = BRK_MEM_EXECUTE;
    else if (kind == "read" || kind == "r")
        spec.type = BRK_MEMORY, spec.access = BRK_MEM_READ;
    else if (kind == "write" || kind == "w")
        spec.type = BRK_MEMORY, spec.access = BRK_MEM_WRITE;
    else if (kind == "rw" || kind == "access")
        spec.type = BRK_MEMORY, spec.access = BRK_MEM_READ | BRK_MEM_WRITE;
    else if (kind == "in" || kind == "port_in")
        spec.type = BRK_IO, spec.access = BRK_IO_IN;
    else if (kind == "out" || kind == "port_out")
        spec.type = BRK_IO, spec.access = BRK_IO_OUT;
    else if (kind == "io" || kind == "port")
        spec.type = BRK_IO, spec.access = BRK_IO_IN | BRK_IO_OUT;
    else if (kind == "nextreg_read")
        spec.type = BRK_NEXTREG, spec.access = BRK_IO_IN;
    else if (kind == "nextreg_write")
        spec.type = BRK_NEXTREG, spec.access = BRK_IO_OUT;
    else if (kind == "nextreg")
        spec.type = BRK_NEXTREG, spec.access = BRK_IO_IN | BRK_IO_OUT;
    else if (kind == "beam")
        spec.type = BRK_BEAM, spec.access = BRK_IO_OUT;
    else if (kind == "copper")
        spec.type = BRK_COPPER, spec.access = BRK_IO_OUT;
    else if (kind == "magic" || kind == "magic_ed_ff" || kind == "magic_dd_01")
        spec.type = BRK_MAGIC, spec.access = BRK_IO_OUT;
    else if (kind == "keyboard" || kind == "key")
        spec.type = BRK_KEYBOARD, spec.access = BRK_KEY_PRESS;
    else
    {
        error = "kind: exec, read, write, rw, in, out, io, nextreg_read, nextreg_write, nextreg, beam, copper, magic, "
                "keyboard";
        return false;
    }

    const bool memory = spec.type == BRK_MEMORY;
    if (spec.type == BRK_MAGIC)
    {
        std::string opcode = Lower(TextParam(value, "opcode").value_or(kind == "magic_dd_01" ? "dd01" : "edff"));
        opcode.erase(std::remove_if(opcode.begin(), opcode.end(), [](char c) { return c == ' ' || c == '_'; }),
                     opcode.end());
        if (opcode == "edff" || opcode == "0xedff")
            spec.address = BRK_MAGIC_ED_FF;
        else if (opcode == "dd01" || opcode == "0xdd01")
            spec.address = BRK_MAGIC_DD_01;
        else
        {
            error = "opcode: edff (ED FF) or dd01 (DD 01)";
            return false;
        }
    }
    else if (spec.type == BRK_KEYBOARD)
    {
        const std::string key = TextParam(value, "key").value_or("any");
        if (Lower(key) == "any")
            spec.address = BRK_KEY_ANY;
        else
        {
            const ZXKeysEnum code = DebugKeyboardManager::ResolveKeyName(key);
            if (code == ZXKEY_NONE)
            {
                error = "key: a key name (the WebAPI's GET /keyboard/keys lists them) or any";
                return false;
            }
            spec.address = static_cast<uint16_t>(code);
        }
        const std::string on = Lower(TextParam(value, "on").value_or(TextParam(value, "access").value_or("press")));
        spec.access = on == "release" ? BRK_KEY_RELEASE : on == "both" ? (BRK_KEY_PRESS | BRK_KEY_RELEASE) : BRK_KEY_PRESS;
        if (on != "press" && on != "release" && on != "both")
        {
            error = "on: press, release or both";
            return false;
        }
    }
    else
    {
        // A port: a preset (fe, 7ffd, 1ffd, fffd, bffd) sets the port and the mask
        const std::optional<std::string> presetName = TextParam(value, "preset");
        const PortPreset* preset = presetName ? FindPortPreset(*presetName) : nullptr;
        if (presetName && preset == nullptr)
        {
            error = "preset: fe, 7ffd, 1ffd, fffd or bffd";
            return false;
        }
        if (preset != nullptr && spec.type != BRK_IO)
        {
            error = "a preset is a port: kind in, out or io";
            return false;
        }
        if (preset != nullptr)
        {
            spec.address = preset->port;
            spec.portMask = preset->mask;
        }
        else
        {
            const char* field = value.params.find("address") != nullptr ? "address"
                                : value.params.find("port") != nullptr  ? "port"
                                                                        : "address";
            const std::optional<uint16_t> address = AddressParam(emulator, value, field, memory, error);
            if (!error.empty())
                return false;
            if (!address)
            {
                error = spec.type == BRK_IO      ? "address (or port, or preset): the port"
                        : spec.type == BRK_BEAM  ? "address: the line (0..511)"
                        : spec.type == BRK_COPPER ? "address: the copper list index (0..1023)"
                        : spec.type == BRK_NEXTREG ? "address: the register (0..255)"
                                                   : "address: a CPU address or a label";
                return false;
            }
            spec.address = *address;
        }
        if (const std::optional<uint16_t> end = AddressParam(emulator, value, "end", memory, error))
        {
            spec.hasEnd = true;
            spec.addressEnd = *end;
        }
        if (!error.empty())
            return false;
        if (const std::optional<uint32_t> mask = UIntParam(value, "mask", 0xFFFF, error))
            spec.portMask = static_cast<uint16_t>(*mask);
        if (const std::optional<uint32_t> hc = UIntParam(value, "hc", BRK_BEAM_MAX_HC, error))
            spec.beamHc = static_cast<uint16_t>(*hc);
        if (const std::optional<std::string> doors = TextParam(value, "doors"))
            if (!BreakpointManager::ParseNextRegDoors(*doors, spec.nrDoors, error))
                return false;
        if (const std::optional<uint32_t> byte = UIntParam(value, "value", 0xFF, error))
        {
            spec.nrValue = static_cast<uint8_t>(*byte);
            spec.nrMask = 0xFF;
        }
        if (const std::optional<uint32_t> mask = UIntParam(value, "value_mask", 0xFF, error))
            spec.nrMask = static_cast<uint8_t>(*mask);
        if (const std::optional<std::string> page = TextParam(value, "page"))
            if (!BreakpointManager::ParsePageInto(*page, spec, error))
                return false;
        if (const std::optional<bool> slotOnly = BoolParam(value, "slot_only", error))
            spec.slotOnly = *slotOnly;
        if (!error.empty())
            return false;
    }
    if (const std::optional<std::string> hits = TextParam(value, "hits"))
        if (!BreakpointManager::ParseHitSpec(*hits, spec.hitMode, spec.hitTarget, error))
            return false;
    spec.note = TextParam(value, "note").value_or("");
    spec.group = TextParam(value, "group").value_or("");
    return true;
}

/// The selection of dbg.bp_remove / bp_enable / bp_disable / bp_toggle: `select` (all, an id, a kind, group:<name>),
/// or `id`, `kind`, `group` on their own; none: all
bool SelectionFromParams(const ActionValue& value, BreakpointSelection& selection, std::string& error)
{
    if (const std::optional<std::string> select = TextParam(value, "select"))
        return BreakpointSelection::Parse(*select, selection, error);
    if (const std::optional<std::string> id = TextParam(value, "id"))
        return BreakpointSelection::Parse(*id, selection, error) &&
               (selection.GetKind() == BreakpointSelection::Kind::Id ||
                (error = "id: a breakpoint id", false));
    if (const std::optional<std::string> group = TextParam(value, "group"))
    {
        if (group->empty())
        {
            error = "group: a group name";
            return false;
        }
        selection = BreakpointSelection::Group(*group);
        return true;
    }
    if (const std::optional<std::string> kind = TextParam(value, "kind"))
        return BreakpointSelection::Parse(*kind, selection, error);
    selection = BreakpointSelection::All();
    return true;
}

StateNode OutcomeBody(const BreakpointSelection& selection, const BreakpointSelection::Outcome& outcome,
                      const BreakpointManager& manager)
{
    StateNode body = StateNode::Object();
    body["select"] = selection.Name();
    body["selected"] = static_cast<int64_t>(outcome.selected);
    body["changed"] = static_cast<int64_t>(outcome.changed);
    const std::optional<bool> active = selection.IsActive(manager);
    if (active)
        body["active"] = *active;
    else
        body["active"] = StateNode();
    return body;
}

/// The user's plain execution breakpoint at `address` (no range, no page), BRK_INVALID when none
uint16_t ExecBreakpointAt(const BreakpointManager& manager, uint16_t address)
{
    for (const auto& [id, bp] : manager.GetAllBreakpoints())
        if (bp && BreakpointManager::IsUserBreakpoint(*bp) && bp->type == BRK_MEMORY &&
            bp->memoryType == BRK_MEM_EXECUTE && !bp->isRange && bp->matchType == BRK_MATCH_ADDR &&
            bp->z80address == address)
            return id;
    return BRK_INVALID;
}

/// The address of dbg.breakpoint_toggle_pc: `address` (a number or a label) or the PC
std::optional<uint16_t> TogglePcAddress(Emulator& emulator, const ActionValue& value, std::string& error)
{
    if (const std::optional<uint16_t> address = AddressParam(emulator, value, "address", true, error))
        return address;
    if (!error.empty())
        return std::nullopt;
    const Z80State* z80 = emulator.GetZ80State();
    if (z80 == nullptr)
    {
        error = "CPU state not available";
        return std::nullopt;
    }
    return z80->pc;
}

void RegisterBreakpointActions(const std::function<void(const char*, const char*, const char*, ValueKind, const char*,
                                                        const char*, Executor, ActionRules)>& add)
{
    const char* debugger = "Debugger";
    {
        ActionRules rules = RouteRules("POST", "/breakpoints");
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            BreakpointManager* manager = Breakpoints(emulator);
            std::string error;
            const std::optional<uint16_t> address = TogglePcAddress(emulator, value, error);
            if (manager == nullptr || !address)
                return std::nullopt;
            const uint16_t id = ExecBreakpointAt(*manager, *address);
            if (id == BRK_INVALID)
                return false;
            const BreakpointSnapshot bp = manager->GetBreakpointById(id);
            return bp != nullptr && bp->active;
        };
        add("dbg.breakpoint_toggle_pc", "Toggle a breakpoint at PC", debugger, ValueKind::Press, "address? (else PC)",
            "Set an execution breakpoint at PC (or at address), or remove the one there.",
            [](const ActionCall& call) {
                BreakpointManager* manager = Breakpoints(*call.emulator);
                if (manager == nullptr)
                    return Conflict("Breakpoint manager not available");
                std::string error;
                const std::optional<uint16_t> address = TogglePcAddress(*call.emulator, call.value, error);
                if (!address)
                    return BadRequest(error);
                StateNode body = StateNode::Object();
                body["address"] = static_cast<int64_t>(*address);
                const uint16_t existing = ExecBreakpointAt(*manager, *address);
                if (existing != BRK_INVALID)
                {
                    manager->RemoveBreakpointByID(existing);
                    body["removed"] = static_cast<int64_t>(existing);
                    body["set"] = false;
                    return ActionResult::Success(std::move(body), "Breakpoint removed");
                }
                BreakpointSpec spec;
                spec.address = *address;
                const uint16_t id = manager->AddBreakpoint(spec, error);
                if (id == BRK_INVALID)
                    return BadRequest(error);
                body["id"] = static_cast<int64_t>(id);
                body["set"] = true;
                return ActionResult::Success(std::move(body), "Breakpoint set");
            },
            std::move(rules));
    }
    add("dbg.bp_add", "Add a breakpoint", debugger, ValueKind::Press,
        "kind (exec|read|write|rw|in|out|io|nextreg_read|nextreg_write|nextreg|beam|copper|magic|keyboard), address "
        "(or a label), end?, preset? (fe|7ffd|1ffd|fffd|bffd), mask?, page?, slot_only?, hc?, doors?, value?, "
        "value_mask?, opcode? (edff|dd01), key? (any), on? (press|release|both), hits?, note?, group?",
        "Add a breakpoint of any kind: memory, ports with a mask or a preset, ZX Next registers, the beam, the copper, "
        "the magic opcodes, a key.",
        [](const ActionCall& call) {
            BreakpointManager* manager = Breakpoints(*call.emulator);
            if (manager == nullptr)
                return Conflict("Breakpoint manager not available");
            BreakpointSpec spec;
            std::string kind;
            std::string error;
            if (!BreakpointSpecFromParams(*call.emulator, call.value, spec, kind, error))
                return BadRequest(error);
            const uint16_t id = manager->AddBreakpoint(spec, error);
            if (id == BRK_INVALID)
                return BadRequest(error);
            StateNode body = StateNode::Object();
            body["id"] = static_cast<int64_t>(id);
            body["kind"] = kind;
            body["address"] = static_cast<int64_t>(spec.address);
            std::string line = manager->FormatBreakpointInfo(id);
            const size_t first = line.find_first_not_of(' ');
            body["info"] = first == std::string::npos ? line : line.substr(first);
            return ActionResult::Success(std::move(body), "Breakpoint #" + std::to_string(id) + " added");
        },
        RouteRules("POST", "/breakpoints"));

    const char* selectParams = "select? (all | an id | a kind: exec, read, write, mem, port, in, out, nextreg, beam, "
                               "copper, magic, keyboard | group:<name>; or id, kind, group)";
    add("dbg.bp_remove", "Remove breakpoints", debugger, ValueKind::Press, selectParams,
        "Remove one breakpoint, all of them, a kind or a group.",
        [](const ActionCall& call) {
            BreakpointManager* manager = Breakpoints(*call.emulator);
            if (manager == nullptr)
                return Conflict("Breakpoint manager not available");
            BreakpointSelection selection;
            std::string error;
            if (!SelectionFromParams(call.value, selection, error))
                return BadRequest(error);
            const BreakpointSelection::Outcome outcome = selection.Remove(*manager);
            if (selection.GetKind() == BreakpointSelection::Kind::Id && outcome.changed == 0)
                return NotFound("No breakpoint " + selection.Name());
            return ActionResult::Success(OutcomeBody(selection, outcome, *manager),
                                         std::to_string(outcome.changed) + " breakpoint(s) removed");
        },
        RouteRules("POST", "/breakpoints/remove"));
    for (const bool enable : {true, false})
        add(enable ? "dbg.bp_enable" : "dbg.bp_disable", enable ? "Enable breakpoints" : "Disable breakpoints",
            debugger, ValueKind::Press, selectParams,
            enable ? "Switch on one breakpoint, all of them, a kind or a group."
                   : "Switch off one breakpoint, all of them, a kind or a group (they stay in the list).",
            [enable](const ActionCall& call) {
                BreakpointManager* manager = Breakpoints(*call.emulator);
                if (manager == nullptr)
                    return Conflict("Breakpoint manager not available");
                BreakpointSelection selection;
                std::string error;
                if (!SelectionFromParams(call.value, selection, error))
                    return BadRequest(error);
                const BreakpointSelection::Outcome outcome = selection.SetActive(*manager, enable);
                if (selection.GetKind() == BreakpointSelection::Kind::Id && outcome.selected == 0)
                    return NotFound("No breakpoint " + selection.Name());
                return ActionResult::Success(OutcomeBody(selection, outcome, *manager),
                                             std::to_string(outcome.selected) + " breakpoint(s) " +
                                                 (enable ? "enabled" : "disabled"));
            },
            RouteRules("PUT", "/breakpoints/active"));
    {
        ActionRules rules = RouteRules("PUT", "/breakpoints/active");
        rules.toggleStem = "bp";
        rules.toggleParam = "select";
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            BreakpointManager* manager = Breakpoints(emulator);
            BreakpointSelection selection;
            std::string error;
            if (manager == nullptr || !SelectionFromParams(value, selection, error))
                return std::nullopt;
            return selection.IsActive(*manager);
        };
        add("dbg.bp_toggle", "Toggle breakpoints", debugger, ValueKind::Press,
            "select? (as dbg.bp_enable; all by default), on? (else flips: on when any is off)",
            "Switch a breakpoint, all of them, a kind or a group on or off; the key shows whether all of them are on.",
            [](const ActionCall& call) {
                BreakpointManager* manager = Breakpoints(*call.emulator);
                if (manager == nullptr)
                    return Conflict("Breakpoint manager not available");
                BreakpointSelection selection;
                std::string error;
                if (!SelectionFromParams(call.value, selection, error))
                    return BadRequest(error);
                const std::optional<bool> on = BoolParam(call.value, "on", error);
                if (!error.empty())
                    return BadRequest(error);
                bool active = false;
                BreakpointSelection::Outcome outcome;
                if (on)
                {
                    active = *on;
                    outcome = selection.SetActive(*manager, active);
                }
                else
                    outcome = selection.Toggle(*manager, active);
                if (outcome.selected == 0)
                    return NotFound("No breakpoint in " + selection.Name());
                return ActionResult::Success(OutcomeBody(selection, outcome, *manager),
                                             std::to_string(outcome.selected) + " breakpoint(s) " +
                                                 (active ? "enabled" : "disabled"));
            },
            std::move(rules));
    }
    {
        ActionRules rules = RouteRules("PUT", "/breakpoints/active");
        rules.toggleStem = "bp_group";
        rules.toggleParam = "group";
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            BreakpointManager* manager = Breakpoints(emulator);
            const std::optional<std::string> group = TextParam(value, "group");
            if (manager == nullptr || !group || group->empty())
                return std::nullopt;
            return BreakpointSelection::Group(*group).IsActive(*manager);
        };
        add("dbg.bp_group_toggle", "Breakpoint group on / off", debugger, ValueKind::Press,
            "group, on? (else flips)",
            "Switch a breakpoint group on or off; the key shows whether the group is on.",
            [](const ActionCall& call) {
                BreakpointManager* manager = Breakpoints(*call.emulator);
                if (manager == nullptr)
                    return Conflict("Breakpoint manager not available");
                const std::string group = TextParam(call.value, "group").value_or("");
                if (group.empty())
                    return BadRequest("group: a breakpoint group (GET /breakpoints/groups lists them)");
                std::string error;
                const std::optional<bool> on = BoolParam(call.value, "on", error);
                if (!error.empty())
                    return BadRequest(error);
                const BreakpointSelection selection = BreakpointSelection::Group(group);
                bool active = false;
                BreakpointSelection::Outcome outcome;
                if (on)
                    outcome = selection.SetActive(*manager, active = *on);
                else
                    outcome = selection.Toggle(*manager, active);
                if (outcome.selected == 0)
                    return NotFound("No breakpoint in group '" + group + "'");
                StateNode body = OutcomeBody(selection, outcome, *manager);
                body["group"] = group;
                return ActionResult::Success(std::move(body),
                                             "Group '" + group + "' " + (active ? "on" : "off"));
            },
            std::move(rules));
    }
}

/// endregion </Breakpoints>

/// region <Machine state>

void RegisterMachineStateActions(const std::function<void(const char*, const char*, const char*, ValueKind, const char*,
                                                          const char*, Executor, ActionRules)>& add)
{
    const char* group = "Machine state";
    add("dbg.register_set", "Set a register", group, ValueKind::Press,
        "name (a, f, bc, hl, ix, sp, pc, i, r, im, iff1, ... and the alternates af', bc'), value",
        "Write a CPU register (as PUT /registers/{name}); refused in the recorded past of a time travel history.",
        [](const ActionCall& call) {
            const std::string name = TextParam(call.value, "name").value_or("");
            const Z80::RegisterInfo* info = Z80::FindRegister(name);
            if (info == nullptr)
                return BadRequest(name.empty() ? std::string("name: a register (GET /registers lists them)")
                                               : "Unknown register: " + name);
            std::string error;
            const std::optional<uint32_t> value = UIntParam(call.value, "value", 0xFFFF, error);
            if (!value)
                return BadRequest(error.empty() ? std::string("value: 0..65535") : error);
            std::string refused;
            if (!call.emulator->SetRegisterFromTool("action register", name, static_cast<uint16_t>(*value), &refused))
            {
                if (!refused.empty())
                    return Conflict(refused);
                return BadRequest(std::string(info->name) + " cannot hold " + std::to_string(*value) + " (at most " +
                                  std::to_string(info->maxValue) + ")");
            }
            StateNode body = StateNode::Object();
            body["name"] = std::string(info->name);
            body["value"] = static_cast<int64_t>(*value);
            return ActionResult::Success(std::move(body), std::string(info->name) + " set");
        },
        RouteRules("PUT", "/registers/pc"));
    add("mem.write", "Write memory", group, ValueKind::Press,
        "address (or a label), bytes (hex: \"3E01C9\" or \"3E 01 C9\"), or value (one byte) with count? (1: a fill)",
        "Write bytes into the CPU's memory view, or fill a range with one byte (address, value, count).",
        [](const ActionCall& call) {
            Memory* memory = call.emulator->GetMemory();
            if (memory == nullptr)
                return Conflict("Memory not available");
            std::string error;
            const std::optional<uint16_t> address = AddressParam(*call.emulator, call.value, "address", true, error);
            if (!address)
                return BadRequest(error.empty() ? std::string("address: where to write") : error);
            std::vector<uint8_t> bytes;
            if (const std::optional<std::string> hex = TextParam(call.value, "bytes"))
            {
                std::string digits;
                for (const char c : *hex)
                    if (c != ' ' && c != ',' && c != ':')
                        digits += c;
                if (digits.empty() || digits.size() % 2 != 0)
                    return BadRequest("bytes: an even number of hex digits");
                for (size_t i = 0; i < digits.size(); i += 2)
                {
                    uint32_t byte = 0;
                    if (!ParseNumberText("0x" + digits.substr(i, 2), 0xFF, byte))
                        return BadRequest("bytes: hex digits only");
                    bytes.push_back(static_cast<uint8_t>(byte));
                }
            }
            else
            {
                const std::optional<uint32_t> byte = UIntParam(call.value, "value", 0xFF, error);
                const std::optional<uint32_t> count = UIntParam(call.value, "count", 0x10000, error);
                if (!error.empty())
                    return BadRequest(error);
                if (!byte)
                    return BadRequest("bytes (hex) or value (a byte, with count for a fill)");
                bytes.assign(count.value_or(1), static_cast<uint8_t>(*byte));
            }
            if (bytes.empty() || *address + bytes.size() > 0x10000)
                return BadRequest("the write runs past #FFFF");
            const std::string refused = call.emulator->EditMemoryFromTool("action memory", [&] {
                for (size_t i = 0; i < bytes.size(); i++)
                    memory->DirectWriteToZ80Memory(static_cast<uint16_t>(*address + i), bytes[i]);
            });
            if (!refused.empty())
                return Conflict(refused);
            StateNode body = StateNode::Object();
            body["address"] = static_cast<int64_t>(*address);
            body["count"] = static_cast<int64_t>(bytes.size());
            return ActionResult::Success(std::move(body), std::to_string(bytes.size()) + " byte(s) written");
        },
        RouteRules("POST", "/memory/write"));
    add("port.out", "Port OUT", group, ValueKind::Press,
        "port (0..#FFFF, or a preset: fe, 7ffd, 1ffd, fffd, bffd), value (0..#FF)",
        "Write a value to a port through the machine's decoder, as a CPU OUT would (paging, border, AY ...).",
        [](const ActionCall& call) {
            const std::string portText = TextParam(call.value, "port").value_or("");
            const std::string valueText = TextParam(call.value, "value").value_or("");
            if (portText.empty() || valueText.empty())
                return BadRequest("port and value");
            uint16_t port = 0;
            uint8_t value = 0;
            std::string error;
            const PortPreset* preset = FindPortPreset(portText);
            if (!PortWrite::Parse(preset != nullptr ? std::to_string(preset->port) : portText, valueText, port, value,
                                  error))
                return BadRequest(error);
            const PortWrite::Result result = PortWrite::Write(call.emulator.get(), port, value, "action");
            if (!result.ok)
                return ActionResult::Failure(result.busy ? "not_now" : "Conflict", result.error);
            StateNode body = StateNode::Object();
            body["port"] = static_cast<int64_t>(port);
            body["value"] = static_cast<int64_t>(value);
            body["moment"] = result.moment;
            return ActionResult::Success(std::move(body), "OUT written");
        },
        RouteRules("POST", "/ports/out"));
}

/// endregion </Machine state>

/// region <Symbols>

ActionResult FromSymbols(SymbolReply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(SymbolControlErrorPhrase(reply.error), std::move(reply.message), std::move(reply.body));
}

/// A SymbolControl verb with the params as its options
Executor SymbolVerb(const char* verb)
{
    return [verb](const ActionCall& call) {
        return FromSymbols(SymbolControl(call.emulator->GetContext()).Execute({verb, ParamOptions(call.value.params)}));
    };
}

void RegisterSymbolActions(const std::function<void(const char*, const char*, const char*, ValueKind, const char*,
                                                    const char*, Executor, ActionRules)>& add)
{
    const char* group = "Symbols";
    add("sym.import", "Load symbols", group, ValueKind::Press,
        "path, format?, set?, space?, base?, policy? (both|keep|replace|fail)",
        "Load a symbol file (.map, .sym, .sld, .lbl, ... in any format the symbols formats list) into its own set.",
        SymbolVerb("import"), RouteRules("POST", "/symbols/import"));
    add("sym.export", "Save symbols", group, ValueKind::Press, "path, format?, sets?, space?, from?, to?, kinds?, name?",
        "Save the symbols (every enabled set, or the ones named) to a file in a format.", SymbolVerb("export"),
        RouteRules("POST", "/symbols/export"));
    add("sym.import_source", "Import labels from a source", group, ValueKind::Press,
        "path (a source, an image, disk:A/NAME.T), main?, set?, policy?, generated?",
        "Assemble a source on the host and import the labels it defines.", SymbolVerb("import-source"),
        RouteRules("POST", "/symbols/import/source"));
    add("sym.import_live", "Import an assembler's labels from RAM", group, ValueKind::Press,
        "scanner? (alasm-table|xas-table), page?, offset?, set?, policy?",
        "Read the label table of an assembler running in the machine (ALASM, XAS) into a set.",
        SymbolVerb("import-live"), RouteRules("POST", "/symbols/import/live"));
    add("sym.drop", "Drop a symbol set", group, ValueKind::Press, "id (GET /symbols/sets lists them)",
        "Remove a symbol set and its labels.", SymbolVerb("drop"), RouteRules("DELETE", "/symbols/sets"));
    {
        ActionRules rules = RouteRules("PUT", "/symbols/sets");
        rules.toggleStem = "sym_set";
        rules.toggleParam = "id";
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            LabelManager* labels = Labels(emulator);
            const std::optional<std::string> id = TextParam(value, "id");
            if (labels == nullptr || !id)
                return std::nullopt;
            return labels->IsSymbolSetEnabled(*id);
        };
        add("sym.set_toggle", "Symbol set on / off", group, ValueKind::Press, "id, on? (else flips)",
            "Show or hide the labels of a symbol set; the key shows whether the set is on.",
            [](const ActionCall& call) {
                LabelManager* labels = Labels(*call.emulator);
                if (labels == nullptr)
                    return Conflict("Label manager not available");
                const std::string id = TextParam(call.value, "id").value_or("");
                const std::optional<bool> current = labels->IsSymbolSetEnabled(id);
                if (!current)
                    return NotFound(id.empty() ? std::string("id: a symbol set (GET /symbols/sets lists them)")
                                               : "No symbol set '" + id + "'");
                std::string error;
                const bool on = BoolParam(call.value, "on", error).value_or(!*current);
                if (!error.empty())
                    return BadRequest(error);
                std::map<std::string, std::string> options = {{"id", id}, {"enabled", on ? "true" : "false"}};
                return FromSymbols(SymbolControl(call.emulator->GetContext()).Execute({"set", options}));
            },
            std::move(rules));
    }
}

/// endregion </Symbols>

/// region <Profilers, traces and analyzers>

AnalyzerManager* Analyzers(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetAnalyzerManager()
                                                                   : nullptr;
}

ActionResult FromTrace(tracecontrol::Reply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(std::move(reply.error), std::move(reply.message), std::move(reply.body));
}

/// A tracecontrol verb on params.name with the other params as its options
Executor TraceVerb(const char* op)
{
    return [op](const ActionCall& call) {
        return FromTrace(tracecontrol::Execute(*call.emulator, TextParam(call.value, "name").value_or(""), op,
                                               ParamOptions(call.value.params, {"name"})));
    };
}

/// Every one an observer: no companion scope (the /profiler routes change nothing)
ActionRules TracerRules()
{
    return RouteRules("POST", "/profiler/start");
}

void RegisterProfilerActions(const std::function<void(const char*, const char*, const char*, ValueKind, const char*,
                                                      const char*, Executor, ActionRules)>& add)
{
    const char* group = "Profilers";
    {
        ActionRules rules = TracerRules();
        rules.toggleStem = "profiler";
        rules.toggleParam = "name";
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            const std::optional<ProfilerSessionState> state =
                tracecontrol::State(emulator, TextParam(value, "name").value_or(""));
            if (!state)
                return std::nullopt;
            return *state != ProfilerSessionState::Stopped;
        };
        add("prof.toggle", "Profiler / trace on / off", group, ValueKind::Press,
            "name (opcode|memory|calltrace|coverage|porttrace|gsporttrace|pchist|aylog|audio|all), on? (else flips), "
            "keep? (coverage), capacity? (aylog), seconds? + source? (audio)",
            "Start or stop a profiler, a trace or a capture (opcode, memory, call trace, coverage, port trace, GS port "
            "trace, PC history, AY log, audio, or the three profilers at once); the key shows whether it runs.",
            TraceVerb("toggle"), std::move(rules));
    }
    {
        ActionRules rules = TracerRules();
        rules.toggleStem = "profiler_paused";
        rules.toggleParam = "name";
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            const std::string name = TextParam(value, "name").value_or("");
            const std::optional<ProfilerSessionState> state = tracecontrol::State(emulator, name);
            if (!state || !tracecontrol::CanPause(name))
                return std::nullopt;
            return *state == ProfilerSessionState::Paused;
        };
        add("prof.pause_toggle", "Profiler / trace pause", group, ValueKind::Press,
            "name (opcode|memory|calltrace|porttrace|gsporttrace|all), on? (true: pause, false: resume; else flips)",
            "Pause a running profiler or trace, or resume a paused one (the data stays); the key shows the pause.",
            TraceVerb("pause_toggle"), std::move(rules));
    }
    add("prof.clear", "Profiler / trace clear", group, ValueKind::Press,
        "name (opcode|memory|calltrace|coverage|porttrace|gsporttrace|aylog|audio|all)",
        "Drop what a profiler, a trace or a capture collected (it goes on running if it runs).", TraceVerb("clear"),
        TracerRules());

    // The analyzers by their id (GET /analyzers): trdos, editor-input, and the ones the tracers above use
    {
        ActionRules rules = RouteRules("PUT", "/analyzer/trdos");
        rules.toggleStem = "analyzer";
        rules.toggleParam = "name";
        rules.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            AnalyzerManager* analyzers = Analyzers(emulator);
            const std::optional<std::string> name = TextParam(value, "name");
            if (analyzers == nullptr || !name || !analyzers->hasAnalyzer(*name))
                return std::nullopt;
            return analyzers->isActive(*name);
        };
        add("analyzer.toggle", "Analyzer on / off", group, ValueKind::Press,
            "name (trdos, editor-input, coverage, aylog, audiocapture: GET /analyzers), on? (else flips)",
            "Switch an analyzer on or off (TR-DOS, BASIC editor input, ...); the key shows whether it is on.",
            [](const ActionCall& call) {
                AnalyzerManager* analyzers = Analyzers(*call.emulator);
                if (analyzers == nullptr)
                    return Conflict("Analyzer manager not available");
                const std::string name = TextParam(call.value, "name").value_or("");
                if (name.empty() || !analyzers->hasAnalyzer(name))
                {
                    StateNode body = StateNode::Object();
                    StateNode list = StateNode::Array();
                    for (const std::string& id : analyzers->getRegisteredAnalyzers())
                        list.push(StateNode(id));
                    body["available"] = std::move(list);
                    return ActionResult::Failure("Not Found", name.empty() ? "name: an analyzer" : "Unknown analyzer: " + name,
                                                 std::move(body));
                }
                std::string error;
                const bool on = BoolParam(call.value, "on", error).value_or(!analyzers->isActive(name));
                if (!error.empty())
                    return BadRequest(error);
                const bool done = on ? analyzers->activate(name) : analyzers->deactivate(name);
                if (!done && analyzers->isActive(name) != on)
                    return Conflict("The analyzer '" + name + "' did not switch " + (on ? "on" : "off"));
                StateNode body = StateNode::Object();
                body["name"] = name;
                body["enabled"] = analyzers->isActive(name);
                return ActionResult::Success(std::move(body), "Analyzer '" + name + "' " + (on ? "on" : "off"));
            },
            std::move(rules));
    }
    add("analyzer.clear", "Analyzer clear", group, ValueKind::Press, "name (trdos, coverage, aylog, audiocapture)",
        "Drop the events an analyzer collected.",
        [](const ActionCall& call) {
            AnalyzerManager* analyzers = Analyzers(*call.emulator);
            if (analyzers == nullptr)
                return Conflict("Analyzer manager not available");
            const std::string name = TextParam(call.value, "name").value_or("");
            if (TRDOSAnalyzer* trdos = name == "trdos" ? analyzers->getAnalyzer<TRDOSAnalyzer>(name) : nullptr)
                trdos->clear();
            else if (CoverageAnalyzer* coverage = name == "coverage" ? analyzers->getAnalyzer<CoverageAnalyzer>(name) : nullptr)
                coverage->clear();
            else if (AYLogAnalyzer* log = name == "aylog" ? analyzers->getAnalyzer<AYLogAnalyzer>(name) : nullptr)
                log->clear();
            else if (AudioCaptureAnalyzer* capture =
                         name == "audiocapture" ? analyzers->getAnalyzer<AudioCaptureAnalyzer>(name) : nullptr)
                capture->clearCapture();
            else
                return NotFound(name.empty() ? std::string("name: trdos, coverage, aylog or audiocapture")
                                             : "The analyzer '" + name + "' has nothing to clear");
            StateNode body = StateNode::Object();
            body["name"] = name;
            return ActionResult::Success(std::move(body), "Analyzer '" + name + "' cleared");
        },
        RouteRules("DELETE", "/analyzer/trdos/events"));
}

/// endregion </Profilers, traces and analyzers>

}  // namespace

std::vector<RegistrationHandle> RegisterDebugToolsActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, const char* group, ValueKind kind, const char* params,
                   const char* description, Executor run, ActionRules rules) {
        handles.push_back(registry.Register({id, label, group, kind, false, false, params, description, false},
                                            std::move(run), std::move(rules)));
    };
    RegisterBreakpointActions(add);
    RegisterMachineStateActions(add);
    RegisterSymbolActions(add);
    RegisterProfilerActions(add);
    return handles;
}

}  // namespace actions
