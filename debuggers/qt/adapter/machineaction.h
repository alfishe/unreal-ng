// unreal-debugger: a core action (docs/emulator/design/control-surfaces/actions.md) run on the machine behind the
// debugger API, for the GUI control plane's composed commands (memory.find_last_write). Plain types only, as in
// unrealmachine.h: the Qt side never sees the core.

#pragma once

#include <functional>
#include <string>

struct MachineActionResult {
    bool ok = false;
    std::string error;    // the refusal ("not_now", "Not Found", ...), empty when ok
    std::string message;  // one sentence a user can act on
    std::string body;     // the action's result as JSON text
};

// Called on the GUI thread
using MachineActionDone = std::function<void(const MachineActionResult &)>;
