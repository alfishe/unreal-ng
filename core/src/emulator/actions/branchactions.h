#pragma once

/// @file branchactions.h
/// @brief The TTD branch actions and readouts the surfaces share (TTD branches surfaces.md §2): Qt's shortcuts, the
/// Stream Deck and the controllers' profiles call the same ids, each a thin call into a TTDControl verb.
///
/// | Action | Does |
/// |:--|:--|
/// | `ttd.branch_prev` | the branch left last (branch-switch to=previous); repeated presses walk the trail back |
/// | `ttd.branch_next_sibling` / `ttd.branch_prev_sibling` | the next / previous branch at the nearest fork of the lineage, by creation |
/// | `ttd.branch_switch` | a branch by id, GUID or name (a branch menu's key) |
/// | `ttd.branch_menu` | the branch menu's list (the `ttd.branch_list` readout's value) |
/// | `ttd.branch_pin` | pin / unpin the active branch (a toggle: `toggle.ttd.branch_pin`) |
/// | `ttd.branch_delete` | delete a branch off the active lineage after a 1 s hold (`now`: at the press) |
///
/// Readouts, from the published branch table only (never a session operation):
/// `ttd.branch` {available, id, guid, name, count, depth, pinned, created_ago, age, viewed, previous, siblings, forks
/// (the lineage's fork frames, at most 64)} and
/// `ttd.branch_list` {count, total, active, items: [{id, name, age, pinned, active, on_lineage, length}]}.

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace actions
{
class ActionRegistry;
class RegistrationHandle;

/// Register the branch actions; the registry keeps the handles
std::vector<RegistrationHandle> RegisterBranchActions(ActionRegistry& registry);

/// The readout names of this file
const std::vector<std::string>& BranchReadoutNames();
/// A branch readout's value; Null for another name
StateNode ReadBranchReadout(Emulator& emulator, const std::string& name);

/// A creation time as an age: "12s", "5m", "3h", "2d" (coarse, so a readout changes rarely); "" for 0
std::string BranchAge(uint64_t createdHostMicros, uint64_t nowHostMicros);

}  // namespace actions
