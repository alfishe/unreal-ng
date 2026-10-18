#include <emulator/emulator.h>
#include <emulator/io/controllers/controllercontrol.h>
#include <emulator/state/statenodejson.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "cli-processor.h"

/// region <Controllers>

// controllers [verb] ... : a thin adapter over ControllerControl (core/src/emulator/io/controllers/controllercontrol.h),
// the same verbs as WebAPI /api/v1/emulator/{id}/controllers and MCP `controllers` (controller manager surfaces.md §2)

namespace
{
    const char* const kUsage =
        "controllers [list]                         - The machine's connectors and the devices they hold\n"
        "controllers get <connector>                - One connector, the controls it holds now\n"
        "controllers plug <connector|auto> <device> [opt=val ..] - e.g. plug auto kempston-joystick, plug auto "
        "sinclair-joystick port=2\n"
        "controllers unplug <connector>             - Free a connector\n"
        "controllers options <connector> opt=val .. - Change the options of the device it holds\n"
        "controllers targets <device>               - The connectors that take a device, the default first\n"
        "controllers auto [apply]                   - The port-read evidence and the joystick it points to\n"
        "Flags: --json (the reply every surface returns), async (do not wait for the frame boundary)\n";

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

    std::string Text(const StateNode* node)
    {
        if (node == nullptr || node->kind == StateNode::Kind::Null)
            return "-";
        if (node->kind == StateNode::Kind::String)
            return node->s;
        return StateNodeToJsonText(*node);
    }

    /// A connector line: id, kind, the device, the alias
    std::string ConnectorLine(const StateNode& connector)
    {
        std::string line = "  " + Text(connector.find("id"));
        line.resize(std::max<size_t>(line.size() + 1, 20), ' ');
        line += Text(connector.find("kind"));
        line.resize(std::max<size_t>(line.size() + 1, 40), ' ');
        line += Text(connector.find("device"));
        const StateNode* alias = connector.find("alias");
        if (alias != nullptr && alias->kind == StateNode::Kind::String)
            line += "  (" + alias->s + ")";
        return line + "\n";
    }
}  // namespace

void CLIProcessor::HandleControllers(const ClientSession& session, const std::vector<std::string>& args)
{
    using namespace controllers;
    if (!args.empty() && (args[0] == "help" || args[0] == "--help"))
    {
        session.SendResponse(Lines(kUsage, NEWLINE));
        return;
    }
    bool json = false;
    std::vector<std::string> words;
    std::map<std::string, std::string> options;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else if (arg.find('=') != std::string::npos)
            options[arg.substr(0, arg.find('='))] = arg.substr(arg.find('=') + 1);
        else if (arg == "async" || arg == "apply" || arg == "shift")
            options[arg] = "";
        else
            words.push_back(arg);
    }
    ControllerRequest request;
    request.verb = words.empty() ? "list" : words[0];
    const auto& verbs = ControllerControl::Verbs();
    if (std::find(verbs.begin(), verbs.end(), request.verb) == verbs.end())
    {
        session.SendResponse(Lines("Error: unknown verb '" + request.verb + "'\n" + kUsage, NEWLINE));
        return;
    }
    if (request.verb == "targets")
    {
        if (words.size() > 1)
            request.device = words[1];
    }
    else
    {
        if (words.size() > 1)
            request.selector = words[1];
        if (words.size() > 2)
            request.device = words[2];
    }
    request.options = options;

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }
    const ControllerReply reply = ControllerControl(emulator->GetContext()).Execute(request);
    if (json)
    {
        session.SendResponse(reply.ToJson() + NEWLINE);
        return;
    }
    if (!reply.result.Ok())
    {
        session.SendResponse("Error: " + reply.result.message + NEWLINE);
        return;
    }
    std::string text;
    const StateNode value = reply.ToValue();
    if (const StateNode* connectors = value.find("connectors"))
    {
        text += "Connectors (id, kind, device):\n";
        for (const StateNode& connector : connectors->items)
            text += ConnectorLine(connector);
    }
    else
    {
        text += "OK";
        if (!reply.connector.empty())
            text += " " + reply.connector;
        if (reply.pending)
            text += " (pending: applies at the frame boundary)";
        if (!reply.result.message.empty())
            text += ": " + reply.result.message;
        text += "\n";
        for (const auto& [key, field] : reply.body.members)
            text += "  " + key + ": " + Text(&field) + "\n";
    }
    session.SendResponse(Lines(text, NEWLINE));
}

/// endregion </Controllers>
