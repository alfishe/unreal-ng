#pragma once

/// @file companionactions.h
/// @brief What a companion client may do now (docs/inprogress/2026-10-16-companion-client/design.md §3.1): the
/// catalogue of its actions - the buttons of a panel - each with whether it is available in the machine's current
/// state and, when not, why. A companion greys out what it cannot do instead of trying and being refused.
///
/// An action is unavailable when: the machine is stopped; a session holds the machine (take-back excepted); the user
/// did not grant its scope; the state does not fit (resume while running, pause while paused, stepping while running,
/// rewind without recorded history, take-back with no session, allow-again with no cooldown).

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace companion
{
struct CompanionInfo;

/// One action of the catalogue: its name, the scope it needs (0: free for everybody) and what it is
struct ActionInfo
{
    const char* name;
    uint32_t scope;
    const char* what;
};

/// The catalogue, in a stable order (the names are the protocol)
const std::vector<ActionInfo>& Actions();

/// The machine's state the availability depends on
struct MachineView
{
    bool running = false;       ///< executing
    bool paused = false;        ///< paused (a breakpoint stop included)
    bool claimed = false;       ///< a session holds it
    std::string holder;         ///< who, for the reason
    bool history = false;       ///< time travel has recorded history
    bool cooldowns = false;     ///< clients wait in a re-take cooldown
    std::string speedLock;      ///< why the host speed may not change ("" = it may): TTD keeps a 1x history
};

MachineView ViewOf(Emulator& emulator);

/// {action: {available, scope, reason?}} for this companion and machine state
StateNode ActionsReport(const CompanionInfo& companion, const MachineView& view);
}  // namespace companion
