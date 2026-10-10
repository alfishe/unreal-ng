#pragma once

/// @file machinepolicy.h
/// @brief The rules: what an operation does in each machine mode (architecture §4). One table, one row per
/// operation kind, one cell per mode; the coordinator, WouldRefuse and every surface read the same cells.

#include <string>

#include "machineop.h"

namespace coordinator
{
struct PolicyDecision
{
    bool refused = false;
    std::string reason;                          ///< refused: the sentence every surface shows
    SessionEffect effect = SessionEffect::None;  ///< allowed: what the TTD session sees at the commit point
    bool hold = false;                           ///< the machine is parked for the operation
    bool machineResetAfter = false;              ///< the session hears MachineReset after a successful commit
};

/// The cell for @p op in @p mode. Pure: no state, no side effects
PolicyDecision Decide(const MachineOp& op, MachineMode mode);
}  // namespace coordinator
