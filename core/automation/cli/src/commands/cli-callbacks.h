#pragma once

/// @file cli-callbacks.h
/// @brief CLI `callback list | resume <id> | remove <id> | policy <id> <suspend|disable|ignore|unpaced>`: the script
/// callbacks of the selected emulator (docs/features/script-callbacks.md). The report and the verbs are core callback_control's,
/// shared with REST, WebSocket and MCP; this only reads the arguments.

#include <string>
#include <vector>

#include "debugger/callbacks/callbackcontrol.h"

namespace CliCallbacks
{
inline constexpr const char* kUsage =
    "Usage: callback list | resume <id> | remove <id> | policy <id> <suspend|disable|ignore|unpaced>\n"
    "  The script callbacks (Lua on_tap / on_breakpoint / on_game_event, Python emu.on_*): list them with their "
    "statistics, resume a suspended one, remove one, change the over-budget policy";

/// The answer to `callback <args>`; `changed` is set when a callback changed
inline std::string Run(ScriptCallbackManager* manager, const std::vector<std::string>& args, bool* changed = nullptr)
{
    if (changed != nullptr)
        *changed = false;
    const std::string verb = args.empty() ? std::string("list") : args[0];
    if (verb == "list" || verb == "ls")
    {
        if (manager == nullptr)
            return "Script callbacks are not available on this emulator";
        const CallbackHookStatus hooks = manager->HookStatus();
        return callback_control::FormatList(callback_control::ListNode(manager->List(), &hooks));
    }
    callback_control::Outcome outcome;
    if ((verb == "resume" || verb == "remove" || verb == "rm") && args.size() == 2)
        outcome = verb == "resume" ? callback_control::Resume(manager, args[1]) : callback_control::Remove(manager, args[1]);
    else if (verb == "policy" && args.size() == 3)
        outcome = callback_control::SetPolicy(manager, args[1], args[2]);
    else
        return kUsage;
    if (!outcome.ok())
        return "Error: " + outcome.message;
    if (changed != nullptr)
        *changed = true;
    const StateNode* callback = outcome.body.find("callback");
    return callback != nullptr ? outcome.message + ": " + callback_control::FormatLine(*callback) : outcome.message;
}
}  // namespace CliCallbacks
