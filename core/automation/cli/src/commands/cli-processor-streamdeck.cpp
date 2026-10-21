// streamdeck <verb> [args] [name=value ..] [--json]: the Stream Deck daemon's supervision by unreal-qt, its settings,
// pairing and deck brightness - the verbs of companion::streamdeck::Call, the same as WebAPI /api/v1/streamdeck/...,
// MCP `streamdeck`, Lua / Python streamdeck() (Stream Deck integration daemon.md §7)

#include <sstream>
#include <string>
#include <vector>

#include "cli-processor.h"
#include "emulator/companions/streamdeckapi.h"
#include "emulator/state/statenodejson.h"

namespace
{
const char* const kStreamDeckUsage =
    "streamdeck [status]                    - The daemon and its supervisor: state, pid, decks, viewers, pairing\n"
    "streamdeck start | stop | restart      - Start it (or adopt the one running), stop it, both\n"
    "streamdeck settings [name=value ..]    - mode=off|deck|always activity=off|flash|line api_port= viewers_port=\n"
    "                                         binary= (read without values)\n"
    "streamdeck pairing                     - The daemon's pairing state (code while it waits for the approval)\n"
    "streamdeck pair | pair_cancel          - The daemon asks for a pairing (approve it at the GUI), or cancels it\n"
    "streamdeck revoke [id]                 - Revoke the daemon's companion (default its own)\n"
    "streamdeck brightness [deck] [value]   - A deck's brightness 0-100 (deck: its serial, default first)\n"
    "streamdeck udev                        - The Linux udev rule for Stream Decks\n"
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

std::string Str(const StateNode* node)
{
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return "-";
    if (node->kind == StateNode::Kind::String)
        return node->s.empty() ? "-" : node->s;
    if (node->kind == StateNode::Kind::Int)
        return std::to_string(node->i);
    if (node->kind == StateNode::Kind::Bool)
        return node->b ? "yes" : "no";
    return StateNodeToJsonText(*node);
}

std::string Text(const std::string& verb, const StateNode& body, const char* nl)
{
    std::ostringstream out;
    const StateNode* status = body.find("status");
    if (status != nullptr && (verb == "status" || verb == "start" || verb == "stop" || verb == "restart"))
    {
        if (verb != "status")
            out << "Requested: " << verb << nl;
        out << "Stream Deck daemon: " << Str(status->find("state")) << " (mode " << Str(status->find("mode"))
            << ", activity " << Str(status->find("activity")) << ")" << nl;
        out << "  pid " << Str(status->find("pid")) << (status->find("own") && status->find("own")->b ? ", ours" : "")
            << ", automation 127.0.0.1:" << Str(status->find("api_port")) << ", viewers "
            << Str(status->find("viewers_port")) << nl;
        out << "  decks on USB " << Str(status->find("deck_attached")) << ", restarts " << Str(status->find("restarts"))
            << nl;
        const StateNode* daemon = status->find("daemon");
        if (daemon != nullptr && daemon->isObject())
        {
            out << "  version " << Str(daemon->find("version")) << ", decks " << Str(daemon->find("decks"))
                << ", viewers " << Str(daemon->find("viewers")) << ", surface " << Str(daemon->find("surface"))
                << nl;
            if (const StateNode* pairing = daemon->find("pairing"))
                out << "  pairing " << Str(pairing->find("state")) << ", paired " << Str(pairing->find("paired"))
                    << ", companion " << Str(pairing->find("companion_id")) << nl;
        }
        if (const StateNode* decks = status->find("decks"))
            for (const StateNode& deck : decks->items)
                out << "  deck " << Str(deck.find("serial")) << " " << Str(deck.find("model")) << " brightness "
                    << Str(deck.find("brightness")) << nl;
        const std::string error = Str(status->find("error"));
        if (error != "-")
            out << "  error: " << error << nl;
        return out.str();
    }
    if (verb == "udev")
        return Lines(Str(body.find("rule")), nl);
    StateNode copy = body;
    return StateNodeToJsonText(copy) + nl;
}
} // namespace

void CLIProcessor::HandleStreamDeck(const ClientSession& session, const std::vector<std::string>& args)
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
    const std::string verb = words.empty() ? "status" : words[0];
    if (verb == "help" || verb == "-h" || verb == "--help")
    {
        session.SendResponse(Lines(kStreamDeckUsage, NEWLINE));
        return;
    }
    StateNode request = StateNode::Object();
    std::vector<const char*> positional;
    if (verb == "revoke")
        positional = {"id"};
    else if (verb == "brightness")
    {
        // "brightness 40": the first deck; "brightness A1B2C3 40": that deck
        const bool valueOnly = words.size() == 2 && !words[1].empty() &&
                               words[1].find_first_not_of("0123456789") == std::string::npos;
        positional = valueOnly ? std::vector<const char*>{"value"} : std::vector<const char*>{"deck", "value"};
    }
    size_t next = 0;
    for (size_t i = 1; i < words.size(); i++)
    {
        const size_t equals = words[i].find('=');
        if (equals != std::string::npos && equals > 0)
            request[words[i].substr(0, equals)] = words[i].substr(equals + 1);
        else if (next < positional.size())
            request[positional[next++]] = words[i];
        else
        {
            session.SendResponse("Error: Bad Request - unexpected '" + words[i] + "'" + NEWLINE +
                                 Lines(kStreamDeckUsage, NEWLINE));
            return;
        }
    }
    const companion::streamdeck::Reply reply = companion::streamdeck::Call(verb, request);
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
