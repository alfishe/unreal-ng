#pragma once

/// @file callbackcontrol.h
/// @brief The remote planes' view of the script callbacks (docs/features/script-callbacks.md): REST, WebSocket, CLI
/// and MCP cannot register a function (it lives in a script), but they list and manage the callbacks with the same
/// semantics as Lua's callbacks(), callback_resume, callback_remove and callback_policy. One report (a StateNode with
/// the field names of the Lua table), one text line per callback and one outcome per verb, shared by every plane.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/callbacks/scriptcallbacks.h"
#include "emulator/state/statenode.h"

namespace callback_control
{
/// One callback: id, on, owner, label, target, breakpoint, event?, policy, state, reason?, budget_us, ring, pending,
/// hits, dropped, calls, errors, overruns, total_us, max_us, p99_us, last_overrun_frame? / last_overrun_us? (after an
/// overrun), last_error? (after an error). The optional fields are absent as in the Lua table
StateNode InfoNode(const CallbackInfo& info);
/// {count, callbacks: [InfoNode...]}
StateNode ListNode(const std::vector<CallbackInfo>& callbacks);

/// "#3 tap write #5C78 if MDT == 0 (lua "lives") active, policy suspend, hits 6, calls 6, ..., max 60 us": one line
/// from an InfoNode (or the same JSON parsed back), so the CLI and the MCP text say the same
std::string FormatLine(const StateNode& callback);
/// "Script callbacks: N" and a line per callback
std::string FormatList(const StateNode& list);

/// A callback id: 1 .. 2^32-1, decimal
bool ParseId(const std::string& text, uint32_t& id);

/// What a verb did: an HTTP status (200, 400 bad id / policy, 404 unknown callback, 409 a disabled one cannot be
/// resumed, 503 no callback manager), a one-line message and the body (success: {status, message, callback} or, for
/// remove, {status, message, id, removed}; failure: {error, message})
struct Outcome
{
    int status = 200;
    std::string message;
    StateNode body;

    bool ok() const
    {
        return status >= 200 && status < 300;
    }
};

/// Active again with fresh statistics (a suspended one; an active one restarts its statistics)
Outcome Resume(ScriptCallbackManager* manager, const std::string& id);
/// Removes the callback and its tap. The script that registered it is not told: its function is never called again
Outcome Remove(ScriptCallbackManager* manager, const std::string& id);
/// `policy`: suspend, disable or ignore
Outcome SetPolicy(ScriptCallbackManager* manager, const std::string& id, const std::string& policy);
}  // namespace callback_control
