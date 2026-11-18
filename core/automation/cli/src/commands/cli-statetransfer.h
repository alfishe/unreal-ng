#pragma once

/// @file cli-statetransfer.h
/// @brief CLI `snapshot transfer` (model what-if W0b): the arguments become a StateTransferRequest
/// (core/src/loaders/snapshot/statetransfercontrol.h), the reply becomes text or the WebAPI's JSON - the same options,
/// refusals and report as the WebAPI, MCP, Lua, Python and the Qt menu. Header-only so core-tests drive it without a
/// CLI socket.
///
///   snapshot transfer <id> [--check] [--no-keep-frame] [--json]
///       the selected instance's state into the existing instance <id> (also --to <id>); --check: only say whether it
///       is possible and what would move, nothing changes
///   snapshot transfer --model <name> [--ram <KB>] [--id <symbolic>] [--start | --no-start] [--no-keep-frame] [--json]
///       into a new instance of that model (it gets the source's sound cards); started when the source runs, unless
///       --start / --no-start say otherwise
///
/// Worked example: `snapshot transfer --model PENTAGON --ram 512` prints "Transferred <src> -> <new> (new instance,
/// PENTAGON 512K)", the mode, the counts and one line per copied / dropped / refused item.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "emulator/state/statenodejson.h"
#include "loaders/snapshot/statetransfercontrol.h"

namespace CliStateTransfer
{

inline std::string Usage()
{
    return "Usage: snapshot transfer <id> [--check] [--no-keep-frame] [--json]\n"
           "       snapshot transfer --model <name> [--ram <KB>] [--id <symbolic>] [--start | --no-start]"
           " [--no-keep-frame] [--json]\n"
           "  Moves the selected instance's running state into another instance, in memory: an existing one (<id>,\n"
           "  --check only reports what would move) or a new one of a model. The report lists what was copied,\n"
           "  dropped or refused; a refused transfer changes nothing.\n";
}

/// The request of `snapshot transfer <args>` (`args` after "transfer"); false with `error` (printed with the usage)
/// when the arguments do not parse
inline bool ParseArgs(const std::vector<std::string>& args, StateTransferRequest& out, bool& json, std::string& error)
{
    out = StateTransferRequest{};
    json = false;
    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        const bool hasValue = i + 1 < args.size();
        if (arg == "--json")
            json = true;
        else if (arg == "--check")
            out.check = true;
        else if (arg == "--keep-frame")
            out.keepFramePosition = true;
        else if (arg == "--no-keep-frame")
            out.keepFramePosition = false;
        else if (arg == "--start")
            out.start = true;
        else if (arg == "--no-start")
            out.start = false;
        else if ((arg == "--to" || arg == "--model" || arg == "--id") && hasValue)
            (arg == "--to" ? out.to : arg == "--model" ? out.model : out.symbolicId) = args[++i];
        else if (arg == "--ram" && hasValue)
        {
            const std::string& value = args[++i];
            char* end = nullptr;
            const unsigned long kb = std::strtoul(value.c_str(), &end, 10);
            if (value.empty() || end == nullptr || *end != '\0' || kb == 0 || kb > 65536)
            {
                error = "--ram: expected a RAM size in KB, got '" + value + "'";
                return false;
            }
            out.ramSizeKB = static_cast<uint32_t>(kb);
        }
        else if (arg.rfind("--", 0) == 0)
        {
            error = "unknown option '" + arg + "'" + (hasValue ? "" : " (or its value is missing)");
            return false;
        }
        else if (out.to.empty())
            out.to = arg;
        else
        {
            error = "unexpected argument '" + arg + "'";
            return false;
        }
    }
    // Which target, and which options go with it, the core decides (StateTransferControl), as for every surface
    return true;
}

/// The reply as the CLI prints it: the WebAPI's JSON with --json, else the text report
inline std::string Render(const StateTransferReply& reply, bool json)
{
    if (json)
        return StateNodeToJsonText(reply.body) + "\n";
    return reply.ToText();
}

}  // namespace CliStateTransfer
