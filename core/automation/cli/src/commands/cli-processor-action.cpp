// action [list | run <id> [value] [param=value ..] | help]: the host actions of the core ActionRegistry
// (core/src/emulator/actions/actionregistry.h; control surfaces actions.md §6), the same ids, values, gates and errors
// as WebAPI /api/v1/controllers/actions, MCP `actions`, Lua / Python action(). The registry applies this connection's
// gate itself: a companion connection is checked against its scopes, an ordinary one gets the implicit session of the
// automation gate (kept for the connection, as for any other changing command)

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "cli-processor.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/execution/automationgate.h"
#include "emulator/state/statenodejson.h"
#include "platform-sockets.h"

namespace
{
const char* const kActionUsage =
    "action [list]                          - Every host action, its kind, whether it can run now (* = yes) or why not\n"
    "action run <id> [value] [param=val ..] - Run one on the selected instance and wait for it, e.g.\n"
    "                                         action run run.pause_toggle, action run ttd.jog_frames -10,\n"
    "                                         action run run.turbo_hold release, action run run.n_scanlines count=8\n"
    "Values by kind: press - press (default) | release; delta - signed steps; level - 0..1; text - the text.\n"
    "Flags: --json (the reply every surface returns)\n";

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

/// "count=8" -> params.count = 8 (a number when it reads as one, else the text)
StateNode ParamValue(const std::string& text)
{
    char* end = nullptr;
    const long long integer = std::strtoll(text.c_str(), &end, 10);
    if (!text.empty() && end != nullptr && *end == '\0')
        return StateNode(static_cast<int64_t>(integer));
    const double number = std::strtod(text.c_str(), &end);
    if (!text.empty() && end != nullptr && *end == '\0')
        return StateNode(number);
    if (text == "true" || text == "false")
        return StateNode(text == "true");
    return StateNode(text);
}
}  // namespace

void CLIProcessor::HandleAction(const ClientSession& session, const std::vector<std::string>& args)
{
    bool json = false;
    std::vector<std::string> words;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else
            words.push_back(arg);
    }
    const std::string verb = words.empty() ? "list" : words[0];
    if (verb == "help" || verb == "-h" || verb == "--help")
    {
        session.SendResponse(Lines(kActionUsage, NEWLINE));
        return;
    }

    std::shared_ptr<Emulator> emulator = GetSelectedEmulator(session);
    const std::string instance = emulator ? emulator->GetId() : std::string();

    actions::ActionOrigin origin;
    const std::string peer = PeerAddressOf(session.GetSocket());
    if (!_companionId.empty())
    {
        origin.origin = execution::Origin::Companion;
        origin.identity = _companionId;
    }
    else
    {
        origin.origin = execution::Origin::Cli;
        origin.identity = "cli|" + peer;  // the identity of this connection's implicit session (PassAutomationGate)
        origin.client = "cli";
        origin.peer = peer;
        if (!_controlSession.isNil())
            origin.session = _controlSession.toString();
    }
    actions::ActionRegistry& registry = actions::ActionRegistry::Instance();

    if (verb == "list")
    {
        std::ostringstream out;
        if (!emulator)
            out << "No instance selected: every action is listed as unavailable" << NEWLINE;
        std::string group;
        for (const actions::ActionStatus& status : registry.Statuses(instance, origin))
        {
            if (json)
                continue;
            if (status.info.group != group)
            {
                group = status.info.group;
                out << group << ":" << NEWLINE;
            }
            std::string line = std::string(status.available ? "* " : "  ") + status.info.id;
            line.resize(std::max<size_t>(line.size() + 1, 32), ' ');
            line += std::string("[") + actions::ValueKindName(status.info.kind) + (status.info.held ? ", held" : "") +
                    (status.info.gui ? ", gui" : "") + "] " + status.info.label;
            if (!status.available)
                line += " - " + status.reason;
            out << line << NEWLINE;
        }
        if (json)
        {
            StateNode body = StateNode::Object();
            body["instance"] = instance;
            StateNode& rows = body["actions"];
            rows = StateNode::Array();
            for (const actions::ActionStatus& status : registry.Statuses(instance, origin))
            {
                StateNode row = StateNode::Object();
                row["id"] = status.info.id;
                row["label"] = status.info.label;
                row["group"] = status.info.group;
                row["kind"] = actions::ValueKindName(status.info.kind);
                row["held"] = status.info.held;
                row["gui"] = status.info.gui;
                row["scope"] = automation_gate::CompanionScopeName(status.scope);
                row["needs_pause"] = status.needsPause;
                row["available"] = status.available;
                if (!status.available)
                {
                    row["error"] = status.error;
                    row["reason"] = status.reason;
                }
                rows.push(std::move(row));
            }
            session.SendResponse(StateNodeToJsonText(body) + NEWLINE);
            return;
        }
        session.SendResponse(out.str());
        return;
    }

    if (verb != "run" || words.size() < 2)
    {
        session.SendResponse(std::string("Error: usage - ") + Lines(kActionUsage, NEWLINE));
        return;
    }
    if (!emulator)
    {
        session.SendResponse(std::string("Error: no emulator selected") + NEWLINE);
        return;
    }
    const std::string id = words[1];
    const std::optional<actions::ActionInfo> info = registry.Find(id);
    if (!info)
    {
        session.SendResponse("Error: Not Found - unknown action '" + id + "' (action list shows them)" + NEWLINE);
        return;
    }
    std::string valueText;
    StateNode params = StateNode::Object();
    for (size_t i = 2; i < words.size(); i++)
    {
        const size_t equals = words[i].find('=');
        if (equals != std::string::npos && equals > 0)
            params[words[i].substr(0, equals)] = ParamValue(words[i].substr(equals + 1));
        else
            valueText = words[i];
    }
    actions::ActionValue value;
    std::string error;
    if (!actions::ValueFromText(info->kind, valueText, value, error))
    {
        session.SendResponse("Error: Bad Request - " + error + NEWLINE);
        return;
    }
    value.params = std::move(params);

    const actions::ActionResult result = registry.Run(id, std::move(value), instance, origin);
    if (!result.implicitSession.empty() && _companionId.empty())
    {
        // The gate opened (or went on in) this connection's implicit session: it ends with the connection
        const unreal::UUID owner(result.implicitSession);
        if (result.implicitOpened || _controlSession.isNil())
        {
            _controlSession = owner;
            _controlEmulator = emulator;
        }
        if (result.implicitOpened)
            session.SendResponse(std::string("Note: ") + automation_gate::kAdvice + NEWLINE);
    }
    if (json)
    {
        StateNode body = StateNode::Object();
        body["ok"] = result.ok;
        body["action"] = id;
        body["instance"] = instance;
        if (!result.ok)
            body["error"] = result.error;
        body["message"] = result.message;
        body["result"] = result.body;
        session.SendResponse(StateNodeToJsonText(body) + NEWLINE);
        return;
    }
    if (!result.ok)
    {
        session.SendResponse("Error: " + result.error + " - " + result.message + NEWLINE);
        return;
    }
    std::string text = "OK: " + id + (result.message.empty() ? std::string() : " - " + result.message);
    if (result.body.size() > 0)
        text += std::string(NEWLINE) + StateNodeToJsonText(result.body);
    session.SendResponse(text + NEWLINE);
}
