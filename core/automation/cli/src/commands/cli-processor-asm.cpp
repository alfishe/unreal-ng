// CLI: asm <verb> - assembler sources through AsmControl (the same checks and fields as the WebAPI, MCP, Lua, Python and
// the Qt disk browser); any other first word is the assembler (asm = assemble, as before)

#include <sstream>

#include "cli-processor.h"
#include <debugger/asm/asmcontrol.h>
#include <emulator/emulator.h>
#include <emulator/state/statenodejson.h>

void CLIProcessor::HandleAsm(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // asm sync <status|probe|extract> ...: the verbs sync-status, sync-probe, sync-extract
    std::vector<std::string> args = rawArgs;
    if (!args.empty() && args[0] == "sync")
    {
        const std::string action = args.size() > 1 && args[1].compare(0, 2, "--") != 0 ? args[1] : "status";
        args.erase(args.begin(), args.begin() + (args.size() > 1 && args[1] == action ? 2 : 1));
        args.insert(args.begin(), "sync-" + action);
    }
    const auto& verbs = AsmControl::Verbs();
    if (args.empty() || std::find(verbs.begin(), verbs.end(), args[0]) == verbs.end())
    {
        if (args.empty())
        {
            std::stringstream ss;
            ss << "Usage:" << NEWLINE;
            ss << "  asm <addr> <code...> [--write]        - assemble Z80 text (as 'assemble')" << NEWLINE;
            ss << "  asm formats | dialects                - source formats; dialects convert reads and writes" << NEWLINE;
            ss << "  asm files [A-D]                       - the files on a disk, each with its format" << NEWLINE;
            ss << "  asm detect <path>                     - which format a source is" << NEWLINE;
            ss << "  asm decode <path> [--codec c] [--version v] [--output file]" << NEWLINE;
            ss << "  asm encode <text-file> --codec c [--version v] [--output file|disk:A/NAME.T]" << NEWLINE;
            ss << "  asm convert <path> --to dialect [--codec c] [--from d] [--output file]" << NEWLINE;
            ss << "  asm sync [status] [--assembler a]       - the assembler running in the machine and its text" << NEWLINE;
            ss << "  asm sync probe                        - every assembler that identifies in RAM" << NEWLINE;
            ss << "  asm sync extract [--as text|file|dialect] [--to d] [--output file|disk:A/NAME.T]" << NEWLINE;
            ss << "  (a path is a host file or disk:A/NAME.T; --file NAME.T picks a file in a .trd / .tap; --json)" << NEWLINE;
            session.SendResponse(ss.str());
            return;
        }
        HandleAssemble(session, args);
        return;
    }
    auto emulator = GetSelectedEmulator(session);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;

    const std::string& verb = args[0];
    AsmRequest request{verb, {}};
    bool json = false;
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); i++)
    {
        if (args[i] == "--json")
            json = true;
        else if (args[i].size() > 2 && args[i].compare(0, 2, "--") == 0)
        {
            const std::string name = args[i].substr(2);
            request.options[name] = i + 1 < args.size() ? args[++i] : std::string();
        }
        else
            positional.push_back(args[i]);
    }
    if (!positional.empty())
    {
        if (verb == "files")
            request.options["drive"] = positional[0];
        else if (verb == "encode")
            request.options["input"] = positional[0];
        else if (verb == "detect" || verb == "decode" || verb == "convert")
            request.options["path"] = positional[0];
    }

    const AsmReply reply = AsmControl(context).Execute(request);
    if (json)
    {
        session.SendResponse(StateNodeToJsonText(reply.ToValue()) + NEWLINE);
        return;
    }
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode& body = reply.body;
    const auto text = [&](const char* key) {
        const StateNode* n = body.find(key);
        return n && n->kind == StateNode::Kind::String ? n->s : std::string("-");
    };
    const auto number = [&](const char* key) {
        const StateNode* n = body.find(key);
        return n ? std::to_string(n->i) : std::string("0");
    };
    std::stringstream ss;
    if (verb == "formats")
        for (const StateNode& f : body.find("formats")->items)
        {
            std::string versions;
            for (const StateNode& v : f.find("versions")->items)
                versions += (versions.empty() ? "" : " ") + v.find("id")->s;
            ss << f.find("id")->s << "  " << f.find("title")->s << (versions.empty() ? "" : "  [" + versions + "]") << NEWLINE;
        }
    else if (verb == "dialects")
    {
        ss << "read:";
        for (const StateNode& d : body.find("read")->items)
            ss << " " << d.s;
        ss << NEWLINE << "write:";
        for (const StateNode& d : body.find("write")->items)
            ss << " " << d.s;
        ss << NEWLINE;
    }
    else if (verb == "files")
        for (const StateNode& f : body.find("files")->items)
            ss << f.find("name")->s << "." << f.find("type")->s << "  start " << f.find("start")->i << "  length " << f.find("length")->i << "  "
               << (f.find("format")->kind == StateNode::Kind::String ? f.find("format")->s : std::string("-")) << NEWLINE;
    else if (verb == "detect")
    {
        ss << "Format: " << text("format") << NEWLINE;
        for (const StateNode& c : body.find("candidates")->items)
            ss << "  " << c.find("format")->s << "  " << c.find("score")->i << NEWLINE;
    }
    else if (verb == "decode" || verb == "convert")
    {
        if (body.find("output"))
            ss << number("lines") << " lines (" << text("format") << " " << text("version") << ") written to " << text("output") << NEWLINE;
        else
            ss << text("text");
    }
    else if (verb == "sync-probe")
    {
        if (body.find("candidates")->items.empty())
            ss << "No known assembler in RAM" << NEWLINE;
        for (const StateNode& c : body.find("candidates")->items)
            ss << c.find("assembler")->s << "  " << c.find("score")->i << "  " << c.find("reason")->s << NEWLINE;
    }
    else if (verb == "sync-status" || (verb == "sync-extract" && !body.find("text")))
    {
        ss << text("title") << " (" << text("assembler") << "): " << text("state") << ", text '" << text("name") << "', " << number("bytes")
           << " bytes";
        if (body.find("page") && body.find("page")->i >= 0)
            ss << " in page " << number("page");
        if (body.find("editor") && body.find("editor")->b)
            ss << ", in the editor at line " << body.find("current_line")->i + 1 << " of " << number("lines");
        if (body.find("typing") && body.find("typing")->b)
            ss << ", a line being typed (not in the text yet)";
        if (body.find("changed") && body.find("changed")->b)
            ss << ", changed since saved";
        ss << NEWLINE;
        if (body.find("output"))
            ss << "Written to " << text("output") << NEWLINE;
        else if (body.find("data"))
            ss << "The file as base64 (give --output to write it): " << text("data").size() << " characters" << NEWLINE;
    }
    else if (verb == "sync-extract")
        ss << text("text");
    else if (verb == "encode")
        ss << number("bytes") << " bytes as " << text("format") << " " << text("version")
           << (body.find("output") ? " written to " + text("output") : std::string(" (give --output to write them)")) << NEWLINE;
    if (const StateNode* diagnostics = body.find("diagnostics"))
        for (const StateNode& d : diagnostics->items)
            ss << "  " << d.find("severity")->s << " line " << d.find("line")->i << ": " << d.find("message")->s << NEWLINE;
    session.SendResponse(ss.str());
}
