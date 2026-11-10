// gui [list | catalogue | state <target> | <method> [name=value ..] [target=<node>]] [--json]: the GUI control plane's
// methods the program registered (gui-control-plane design §4.10), the same as WebAPI /api/v1/gui, MCP gui_<interface>,
// Lua / Python gui_call(), `unreal-gui`. Waits for the GUI's answer (15 s at most)

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "../../../common/guicall.h"
#include "cli-processor.h"
#include "emulator/state/statenodejson.h"

namespace
{
const char* const kGuiUsage =
    "gui [list]                             - The GUI programs on this machine, their windows and views\n"
    "gui catalogue                          - The interfaces: their commands and parameters\n"
    "gui state <target>                     - A node's state, interfaces and machine (main/screen, memory)\n"
    "gui <method> [name=value ..] [target=<node>] - Run a command, e.g. gui memory.goto addr=0x6000,\n"
    "                                         gui screen.zoom factor=2 target=main/screen, gui window.raise target=debugger\n"
    "Flags: --json (the result every surface returns)\n";

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

/// The catalogue's interfaces and commands, one line each
std::string CatalogueText(const StateNode& catalogue, const char* nl)
{
    std::ostringstream out;
    if (const StateNode* interfaces = catalogue.find("interfaces"))
        for (const StateNode& spec : interfaces->items)
        {
            const StateNode* name = spec.find("name");
            out << (name ? name->s : "?") << ":" << nl;
            if (const StateNode* commands = spec.find("commands"))
                for (const StateNode& command : commands->items)
                {
                    std::string line = "  " + (command.find("method") ? command.find("method")->s : "?");
                    if (const StateNode* params = command.find("params"))
                        for (const StateNode& param : params->items)
                        {
                            const StateNode* optional = param.find("optional");
                            line += " " + (param.find("name") ? param.find("name")->s : "?") + "=" +
                                    (param.find("kind") ? param.find("kind")->s : "") +
                                    (optional && optional->b ? "?" : "");
                        }
                    line.resize(std::max<size_t>(line.size() + 1, 44), ' ');
                    out << line << "- " << (command.find("description") ? command.find("description")->s : "") << nl;
                }
        }
    return out.str();
}
}  // namespace

void CLIProcessor::HandleGui(const ClientSession& session, const std::vector<std::string>& args)
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
        session.SendResponse(Lines(kGuiUsage, NEWLINE));
        return;
    }
    if (!GuiPlane::Get())
    {
        session.SendResponse(std::string("Error: no_gui: this program has no GUI control plane") + NEWLINE);
        return;
    }
    if (verb == "catalogue")
    {
        StateNode catalogue;
        if (!StateNodeFromJsonText(GuiPlane::Catalogue(), catalogue))
        {
            session.SendResponse(std::string("Error: failed: the GUI gave no catalogue") + NEWLINE);
            return;
        }
        session.SendResponse(json ? StateNodeToJsonText(catalogue) + NEWLINE : CatalogueText(catalogue, NEWLINE));
        return;
    }

    std::string method = verb;
    std::string target;
    StateNode params = StateNode::Object();
    if (verb == "state")
    {
        if (words.size() < 2)
        {
            session.SendResponse(std::string("Usage: gui state <target>") + NEWLINE);
            return;
        }
        params["target"] = words[1];
    }
    else if (verb != "list")
    {
        std::string error;
        if (!gui_call::ParseWords(std::vector<std::string>(words.begin() + 1, words.end()), target, params, error))
        {
            session.SendResponse("Error: " + error + NEWLINE + Lines(kGuiUsage, NEWLINE));
            return;
        }
    }

    const StateNode result = gui_call::CallAndWait(method, target, params);
    if (json)
    {
        session.SendResponse(StateNodeToJsonText(result) + NEWLINE);
        return;
    }
    const StateNode* ok = result.find("ok");
    if (ok == nullptr || !ok->b)
    {
        const StateNode* error = result.find("error");
        const StateNode* message = result.find("message");
        std::string text = "Error: " + (error ? error->s : std::string("failed")) + ": " + (message ? message->s : "");
        if (const StateNode* body = result.find("body"))
            if (const StateNode* candidates = body->find("candidates"))
                text += NEWLINE + std::string("Candidates: ") + StateNodeToJsonText(*candidates);
        session.SendResponse(text + NEWLINE);
        return;
    }
    const StateNode* body = result.find("body");
    session.SendResponse((body && body->size() > 0 ? StateNodeToJsonText(*body) : std::string("OK")) + NEWLINE);
}
