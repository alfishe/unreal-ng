#pragma once

/// @file breakpointselection.h
/// @brief One way for every surface (the catalogue actions, REST, MCP, CLI, Lua, Python) to name a set of the user's
/// breakpoints and switch, toggle or remove it: one breakpoint by id, all of them, a kind, or a group.
///
/// The text form a surface takes (`select`):
/// - `all`
/// - an id: `5`, `0x05`, `#05`
/// - a kind: `exec` (execution), `read`, `write`, `mem` (memory: any of the three), `port` (io: in or out), `in`,
///   `out`, `nextreg`, `nextreg_read`, `nextreg_write`, `beam`, `copper`, `magic`, `keyboard`
/// - a group: `group:<name>` (or the group given on its own, BreakpointSelection::Group)
///
/// Only the user's breakpoints are selected (BreakpointManager::IsUserBreakpoint): the analyzers', the debugger
/// adapters' and the step traps stay with their owners, as with ClearUserBreakpoints.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/state/statenode.h"

class BreakpointSelection
{
public:
    enum class Kind : uint8_t
    {
        All,
        Id,
        Execute,
        Read,
        Write,
        Memory,
        Port,
        PortIn,
        PortOut,
        NextReg,
        NextRegRead,
        NextRegWrite,
        Beam,
        Copper,
        Magic,
        Keyboard,
        Group
    };

    BreakpointSelection() = default;
    static BreakpointSelection All();
    static BreakpointSelection Id(uint16_t id);
    static BreakpointSelection Group(std::string name);
    static BreakpointSelection OfKind(Kind kind);

    /// The text form (see the file comment); false with the reason in `error`
    static bool Parse(const std::string& text, BreakpointSelection& selection, std::string& error);
    /// The kind names Parse takes, for the surfaces' help ("exec, read, write, ...")
    static const char* KindNames();
    /// The text form of this selection ("all", "5", "exec", "group:loop")
    std::string Name() const;

    Kind GetKind() const { return _kind; }
    uint16_t GetId() const { return _id; }
    const std::string& GetGroup() const { return _group; }

    /// Does the breakpoint belong to the selection (the user's only)
    bool Matches(const BreakpointDescriptor& breakpoint) const;
    /// The ids selected now, in id order
    std::vector<uint16_t> Select(const BreakpointManager& manager) const;

    struct Outcome
    {
        size_t selected = 0;  ///< breakpoints in the selection
        size_t changed = 0;   ///< of them, the ones the operation changed
    };
    /// Switch the selection on or off (one repaint for all of them)
    Outcome SetActive(BreakpointManager& manager, bool active) const;
    /// On when any of the selection is off, else off (a toggle key: all on, or all off); `active` says which
    Outcome Toggle(BreakpointManager& manager, bool& active) const;
    /// Remove the selection
    Outcome Remove(BreakpointManager& manager) const;
    /// On when every selected breakpoint is active, off when one is not; nullopt when nothing is selected
    std::optional<bool> IsActive(const BreakpointManager& manager) const;

    /// The scripts' verb (Lua, Python bp_select): `op` enable | disable | toggle | remove on the selection `select`;
    /// `out` {select, selected, changed, active}. False with the reason in `error`
    static bool Run(BreakpointManager& manager, const std::string& select, const std::string& op, StateNode& out,
                    std::string& error);
    /// The groups of the user's breakpoints: [{name, count, enabled, active}] (GET /breakpoints/groups)
    static StateNode Groups(const BreakpointManager& manager);

private:
    Kind _kind = Kind::All;
    uint16_t _id = BRK_INVALID;
    std::string _group;
};
