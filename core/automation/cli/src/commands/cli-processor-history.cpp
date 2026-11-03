#include <debugger/history/historycontrol.h>
#include <emulator/emulator.h>
#include <emulator/state/devicestate.h>

#include <string>

#include "cli-processor.h"

/// region <Device histories>

// history [device [last N] | <device> mode <off|ring|capture> [limit=N] [stream=path] [stream_limit=N] | <device>
// clear]: a thin adapter over HistoryControl (core/src/debugger/history/historycontrol.h)

namespace
{
    const char* kHistoryUsage =
        "history                          - every device history: mode, limit, counts, stream\n"
        "history <device> [N]             - the device's history, the newest N records (all by default)\n"
        "history <device> mode <off|ring|capture> [limit=N] [stream=<file>] [stream_limit=N]\n"
        "                                 - off: zero cost; ring: the last N; capture: up to N (0: no limit);\n"
        "                                   stream: every record to a JSON Lines file as well (stream= stops it)\n"
        "history <device> clear           - drop the records and counts\n"
        "history <device> pause | resume  - hold the history (nothing recorded, the records kept) / go on\n"
        "<device> is the registry id: beta128, zxbus.1, ... (see slots)";

    std::string Lines(const std::string& text, const char* newline)
    {
        std::string out;
        for (char c : text)
        {
            if (c == '\n')
                out += newline;
            else
                out += c;
        }
        return out;
    }

    bool ParseNumber(const std::string& text, uint64_t& value)
    {
        try
        {
            size_t used = 0;
            value = std::stoull(text, &used, 0);
            return used == text.size();
        }
        catch (...)
        {
            return false;
        }
    }
}  // namespace

void CLIProcessor::HandleHistory(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!args.empty() && (args[0] == "help" || args[0] == "--help"))
    {
        session.SendResponse(Lines(kHistoryUsage, NEWLINE) + NEWLINE);
        return;
    }
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    HistoryRequest request;
    std::string error;
    if (args.empty())
    {
        request.verb = "list";
    }
    else
    {
        request.device = args[0];
        request.verb = "read";
        if (args.size() >= 2 && (args[1] == "clear" || args[1] == "pause" || args[1] == "resume"))
        {
            request.verb = args[1];
        }
        else if (args.size() >= 2 && args[1] == "mode")
        {
            request.verb = "config";
            if (args.size() < 3)
                error = "mode needs off, ring or capture";
            else
                request.mode = args[2];
            for (size_t i = 3; i < args.size() && error.empty(); i++)
            {
                const size_t eq = args[i].find('=');
                const std::string key = args[i].substr(0, eq);
                const std::string value = eq == std::string::npos ? "" : args[i].substr(eq + 1);
                uint64_t number = 0;
                if (key == "stream")
                    request.stream = value;
                else if ((key == "limit" || key == "stream_limit") && ParseNumber(value, number))
                    (key == "limit" ? request.limit : request.streamLimit) = number;
                else
                    error = "unknown or bad option '" + args[i] + "'";
            }
        }
        else if (args.size() >= 2)
        {
            uint64_t last = 0;
            if (!ParseNumber(args[1], last))
                error = "'" + args[1] + "' is not a record count";
            request.last = static_cast<size_t>(last);
        }
    }
    if (!error.empty())
    {
        session.SendResponse(Lines("Error: " + error + "\n" + kHistoryUsage, NEWLINE) + NEWLINE);
        return;
    }

    const HistoryReply reply = HistoryControl::Execute(emulator->GetContext(), request);
    if (const StateNode* failure = reply.body.find("error"))
    {
        session.SendResponse("Error: " + failure->s + NEWLINE);
        return;
    }
    session.SendResponse(Lines(DeviceState::ToText(reply.body), NEWLINE));
}

/// endregion </Device histories>
