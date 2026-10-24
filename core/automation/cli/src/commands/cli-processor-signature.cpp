// CLI: signature <file> | media | compare <known> <candidate> - media signatures through SignatureControl
// (core/swsignature/src/signaturecontrol.h: the same fields as the WebAPI, MCP and Python)

#include <sstream>

#include "cli-processor.h"
#include <emulator/emulator.h>
#include <emulator/state/statenodejson.h>
#ifdef ENABLE_SWSIGNATURE
#include "signaturecontrol.h"
#endif

namespace
{
#ifdef ENABLE_SWSIGNATURE
std::string Text(const StateNode* node)
{
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return "-";
    if (node->kind == StateNode::Kind::String)
        return node->s;
    return StateNodeToJsonText(*node);
}

/// One signature tree as lines: format, size, the exact hash, the content signature, the file set
void SignatureLines(std::stringstream& ss, const StateNode& signature, const std::string& indent, const char* newline)
{
    ss << indent << Text(signature.find("name")) << "  " << Text(signature.find("kind")) << " "
       << Text(signature.find("format")) << ", " << Text(signature.find("size")) << " bytes" << newline;
    ss << indent << "  sha256   " << Text(signature.find("sha256")) << newline;
    for (const char* key : {"sha1", "md5", "machine_hint"})
        if (const StateNode* value = signature.find(key))
            ss << indent << "  " << key << (std::string(key).size() < 8 ? std::string(8 - std::string(key).size(), ' ') : "")
               << " " << Text(value) << newline;
    const StateNode* content = signature.find("content");
    ss << indent << "  content  " << (content != nullptr && !content->s.empty() ? content->s : std::string("none"))
       << newline;
    if (const StateNode* set = signature.find("file_set"); set != nullptr && !set->items.empty())
    {
        ss << indent << "  file set";
        for (const StateNode& token : set->items)
            ss << " " << token.s;
        ss << newline;
    }
    if (const StateNode* files = signature.find("files"))
        for (const StateNode& file : files->items)
            ss << indent << "    " << Text(file.find("name")) << "." << Text(file.find("type")) << "  start "
               << Text(file.find("start")) << "  length " << Text(file.find("length")) << "  "
               << Text(file.find("token")) << newline;
    if (const StateNode* blocks = signature.find("blocks"))
        for (const StateNode& block : blocks->items)
            ss << indent << "    #" << Text(block.find("index")) << "  flag " << Text(block.find("flag")) << "  "
               << Text(block.find("length")) << " bytes  " << Text(block.find("sha256")) << newline;
    if (const StateNode* warnings = signature.find("warnings"))
        for (const StateNode& warning : warnings->items)
            ss << indent << "  warning: " << warning.s << newline;
}
#endif
}  // namespace

void CLIProcessor::HandleSignature(const ClientSession& session, const std::vector<std::string>& args)
{
#ifndef ENABLE_SWSIGNATURE
    (void)args;
    session.SendResponse(std::string("Error: built without the signature library (ENABLE_SWSIGNATURE)") + NEWLINE);
#else
    using namespace unrealng::swsignature;
    if (args.empty() || args[0] == "help" || args[0] == "--help")
    {
        std::stringstream ss;
        ss << "Usage:" << NEWLINE;
        ss << "  signature <file> [--details] [--external]  - exact SHA-256, tape content / TR-DOS file set / memory" << NEWLINE;
        ss << "  signature media [--details]                - the media in the machine's tape and floppy slots" << NEWLINE;
        ss << "  signature compare <known> <candidate>      - exact, content, fuzzy (file sets), memory or none" << NEWLINE;
        ss << "  (--json: the reply every surface returns; the game profile a signature picks: controllers match)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }
    SignatureRequest request;
    std::vector<std::string> positional;
    bool json = false;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else if (arg.size() > 2 && arg.compare(0, 2, "--") == 0)
            request.options[arg.substr(2)] = "true";
        else
            positional.push_back(arg);
    }
    EmulatorContext* context = nullptr;
    if (positional[0] == "media")
    {
        request.verb = "media";
        auto emulator = GetSelectedEmulator(session);
        context = emulator ? emulator->GetContext() : nullptr;
    }
    else if (positional[0] == "compare")
    {
        request.verb = "compare";
        if (positional.size() > 1)
            request.options["path"] = positional[1];
        if (positional.size() > 2)
            request.options["with"] = positional[2];
    }
    else
    {
        request.verb = "file";
        request.options["path"] = positional[0] == "file" && positional.size() > 1 ? positional[1] : positional[0];
    }
    const SignatureReply reply = SignatureControl(context).Execute(request);
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
    std::stringstream ss;
    if (request.verb == "file")
        SignatureLines(ss, *reply.body.find("signature"), "", NEWLINE);
    else if (request.verb == "media")
    {
        const StateNode* media = reply.body.find("media");
        if (media == nullptr || media->items.empty())
            ss << "No tape or disk loaded" << NEWLINE;
        else
            for (const StateNode& item : media->items)
            {
                ss << Text(item.find("slot")) << ": " << Text(item.find("source"))
                   << (item.find("dirty") != nullptr && item.find("dirty")->b ? " (written)" : "") << NEWLINE;
                if (const StateNode* signature = item.find("signature"))
                    SignatureLines(ss, *signature, "  ", NEWLINE);
                else
                    ss << "  " << Text(item.find("error")) << NEWLINE;
            }
    }
    else
    {
        const StateNode& match = *reply.body.find("match");
        ss << Text(match.find("level")) << " (score " << Text(match.find("score")) << "): " << Text(match.find("reason"))
           << NEWLINE;
    }
    session.SendResponse(ss.str());
#endif
}
