// surface <verb> [args] [name=value ..] [--json]: the control surfaces' targets (actions::TargetState; Stream Deck
// integration targeting.md), the readouts and the activity feed - the verbs of actions::surfaces::Call, the same as
// WebAPI /api/v1/surfaces/..., MCP `actions`, Lua / Python surfaces()

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cli-processor.h"
#include "emulator/actions/surfacesapi.h"
#include "emulator/state/statenodejson.h"

namespace
{
const char* const kSurfaceUsage =
    "surface views                          - The views (windows) in focus order and the followed instance\n"
    "surface target [session]               - The followed instance (what \"no id\" on the WebAPI acts on), or a session's\n"
    "surface sessions                       - Every surface session (a deck, a REST surface) and its target\n"
    "surface pin <session> <target>         - Pin a session: follow | view:<id> | instance:<id|index>\n"
    "surface readouts [names]               - Readout values (run.state,media.*; default all) of the followed instance\n"
    "                                         (instance=<id|index> for another)\n"
    "surface activity [since]               - The activity feed: who ran which action (limit=N, instance=)\n"
    "surface view_add <name> [active=<id>]  - A view of this plane (tests, out-of-process windows); view_update <view>\n"
    "                                         [active=<id>] [activate=true], view_remove <view>\n"
    "surface session_open <name> [target=]  - A surface session of this plane; session <id>, session_close <id>\n"
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

/// The positional args of each verb, in order
const std::map<std::string, std::vector<const char*>>& Positional()
{
    static const std::map<std::string, std::vector<const char*>> table = {
        {"target", {"session"}},       {"pin", {"session", "target"}}, {"session", {"session"}},
        {"session_close", {"session"}}, {"session_open", {"name"}},    {"view_add", {"name"}},
        {"view_update", {"view"}},     {"view_remove", {"view"}},      {"readouts", {"names"}},
        {"activity", {"since"}}};
    return table;
}

std::string Str(const StateNode* node)
{
    if (node == nullptr)
        return "-";
    if (node->kind == StateNode::Kind::String)
        return node->s;
    if (node->kind == StateNode::Kind::Int)
        return std::to_string(node->i);
    if (node->kind == StateNode::Kind::Null)
        return "-";
    return StateNodeToJsonText(*node);
}

/// "#1 PENTAGON 128K (<id>) via v1 Main window, follow"
std::string TargetLine(const StateNode& target)
{
    const StateNode* instance = target.find("instance");
    if (instance == nullptr || instance->kind == StateNode::Kind::Null)
        return "no machine (" + Str(target.find("pin")) + ")";
    std::string line = "#" + Str(target.find("index")) + " " + Str(target.find("model")) + " " +
                       Str(target.find("ram_kb")) + "K (" + instance->s + ")";
    const StateNode* view = target.find("view");
    if (view != nullptr && view->kind == StateNode::Kind::Object)
        line += " in " + Str(view->find("id")) + " " + Str(view->find("name"));
    line += ", " + Str(target.find("pin")) + " [" + Str(target.find("source")) + "]";
    return line;
}

std::string Text(const std::string& verb, const StateNode& body, const char* nl)
{
    std::ostringstream out;
    if (verb == "views")
    {
        out << "Followed: " << TargetLine(*body.find("followed")) << nl;
        const StateNode* views = body.find("views");
        if (views == nullptr || views->items.empty())
            out << "No views (headless: the selected instance, else the sole one)" << nl;
        else
            for (const StateNode& view : views->items)
                out << "  " << Str(view.find("id")) << " " << Str(view.find("name")) << " [" << Str(view.find("owner"))
                    << "] active " << Str(view.find("active")) << nl;
        return out.str();
    }
    if (verb == "sessions")
    {
        const StateNode* sessions = body.find("sessions");
        if (sessions == nullptr || sessions->items.empty())
            out << "No surface sessions" << nl;
        else
            for (const StateNode& session : sessions->items)
                out << "  " << Str(session.find("id")) << " " << Str(session.find("name")) << " ["
                    << Str(session.find("kind")) << "] -> " << TargetLine(*session.find("target")) << nl;
        return out.str();
    }
    if (verb == "target")
        return "Target: " + TargetLine(*body.find("target")) + nl;
    if (verb == "pin" || verb == "session" || verb == "session_open")
    {
        const StateNode& session = *body.find("session");
        out << "OK: " << Str(session.find("id")) << " " << Str(session.find("name")) << " -> "
            << TargetLine(*session.find("target")) << nl;
        return out.str();
    }
    StateNode copy = body;
    return StateNodeToJsonText(copy) + nl;
}
}  // namespace

void CLIProcessor::HandleSurface(const ClientSession& session, const std::vector<std::string>& args)
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
    const std::string verb = words.empty() ? "views" : words[0];
    if (verb == "help" || verb == "-h" || verb == "--help")
    {
        session.SendResponse(Lines(kSurfaceUsage, NEWLINE));
        return;
    }
    StateNode request = StateNode::Object();
    std::vector<const char*> positional;
    if (auto it = Positional().find(verb); it != Positional().end())
        positional = it->second;
    size_t next = 0;
    for (size_t i = 1; i < words.size(); i++)
    {
        const size_t equals = words[i].find('=');
        if (equals != std::string::npos && equals > 0 && words[i].compare(0, 5, "view:") != 0 &&
            words[i].compare(0, 9, "instance:") != 0)
            request[words[i].substr(0, equals)] = words[i].substr(equals + 1);
        else if (next < positional.size())
            request[positional[next++]] = words[i];
        else
        {
            session.SendResponse("Error: Bad Request - unexpected '" + words[i] + "'" + NEWLINE +
                                 Lines(kSurfaceUsage, NEWLINE));
            return;
        }
    }
    const actions::surfaces::Reply reply = actions::surfaces::Call(verb, request);
    if (json)
    {
        session.SendResponse(StateNodeToJsonText(reply.body) + NEWLINE);
        return;
    }
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + Str(reply.body.find("error")) + " - " + Str(reply.body.find("message")) +
                             NEWLINE);
        return;
    }
    session.SendResponse(Text(verb, reply.body, NEWLINE));
}
