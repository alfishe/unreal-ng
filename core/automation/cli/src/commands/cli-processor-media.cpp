#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/media/mediacontrol.h>

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

#include "cli-processor.h"

/// region <Media Commands>

// media <verb> [slot] [path] [--option value] [--flag] [--json]
// A thin adapter over MediaControl (core/src/emulator/media/mediacontrol.h):
// the same verbs, options, selectors and error codes as the WebAPI, MCP, Lua
// and Python. Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md

namespace
{
    /// Options that take a value; every other option is a flag
    const std::set<std::string> kValueOptions = {"access", "fs",       "codepage", "free", "format", "kind",
                                                 "export", "cylinders", "sides",   "size", "on",     "retarget"};

    /// Verbs that take a path after the slot
    bool TakesPath(const std::string& verb)
    {
        return verb == "insert" || verb == "swap" || verb == "save" || verb == "export";
    }

    std::string MediumText(const StateNode* medium)
    {
        if (!medium || medium->kind == StateNode::Kind::Null)
            return "(empty)";
        std::string text = medium->find("source")->s + "  " + medium->find("format")->s + "  " +
                           medium->find("access")->s;
        if (medium->find("dirty")->b)
            text += "  unsaved " + medium->find("changes")->s;
        return text;
    }

    std::string Join(const StateNode* items)
    {
        std::string text;
        if (items)
        {
            for (const StateNode& item : items->items)
                text += (text.empty() ? "" : " ") + item.s;
        }
        return text;
    }

    std::string SlotLine(const StateNode& slot)
    {
        std::ostringstream line;
        line << "  " << std::left << std::setw(12) << slot.find("id")->s;
        const StateNode* aliases = slot.find("aliases");
        line << std::setw(6) << (aliases ? Join(aliases) : "");
        line << std::setw(9) << slot.find("state")->s;
        line << MediumText(slot.find("medium"));
        return line.str();
    }
}  // namespace

void CLIProcessor::HandleMedia(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    std::string verb = args.empty() ? "list" : args[0];
    std::transform(verb.begin(), verb.end(), verb.begin(), ::tolower);
    if (verb == "help")
    {
        ShowMediaHelp(session);
        return;
    }

    // Positional arguments, then --options
    MediaRequest request;
    request.verb = verb;
    bool json = false;
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0)
        {
            positional.push_back(arg);
            continue;
        }
        std::string name = arg.substr(2);
        std::string value;
        const size_t equals = name.find('=');
        if (equals != std::string::npos)
        {
            value = name.substr(equals + 1);
            name = name.substr(0, equals);
        }
        std::replace(name.begin(), name.end(), '-', '_');  // --end-recording
        if (name == "json")
        {
            json = true;
            continue;
        }
        if (equals == std::string::npos && kValueOptions.count(name) && i + 1 < args.size())
            value = args[++i];
        request.options[name] = value;
    }

    const bool slotless = verb == "list" || verb == "formats";
    size_t next = 0;
    if (!slotless && next < positional.size())
        request.selector = positional[next++];
    if (TakesPath(verb) && next < positional.size())
        request.path = positional[next++];
    if (next < positional.size())
    {
        session.SendResponse("Error: unexpected argument '" + positional[next] + "'" + NEWLINE +
                             "Use 'media help' for the syntax." + NEWLINE);
        return;
    }

    const MediaReply reply = MediaControl(emulator->GetContext()).Execute(request);
    if (json)
    {
        session.SendResponse(reply.ToJson() + NEWLINE);
        return;
    }

    std::ostringstream out;
    if (!reply.result.Ok())
    {
        out << "Error [" << MediaErrorCode(reply.result.error) << "]: " << reply.result.message << NEWLINE;
        session.SendResponse(out.str());
        return;
    }

    const StateNode body = reply.ToValue();
    if (verb == "list")
    {
        out << "  " << std::left << std::setw(12) << "Slot" << std::setw(6) << "Alias" << std::setw(9) << "State"
            << "Medium" << NEWLINE;
        for (const StateNode& slot : body.find("slots")->items)
            out << SlotLine(slot) << NEWLINE;
        const StateNode* detached = body.find("detached");
        if (detached && !detached->items.empty())
        {
            out << "Detached (the slot went away; save, export or discard by slot id):" << NEWLINE;
            for (const StateNode& slot : detached->items)
                out << SlotLine(slot) << NEWLINE;
        }
    }
    else if (verb == "info")
    {
        const StateNode* slot = body.find("info");
        out << SlotLine(*slot) << NEWLINE;
        out << "  kind: " << slot->find("kind")->s << "  tags: " << Join(slot->find("tags")) << NEWLINE;
        if (const StateNode* guest = slot->find("guestName"))
            out << "  guest: " << guest->s << NEWLINE;
    }
    else if (verb == "formats")
    {
        for (const auto& [kind, extensions] : body.find("formats")->members)
            out << "  " << std::left << std::setw(9) << kind << Join(&extensions) << NEWLINE;
    }
    else
    {
        out << "ok: " << reply.slot;
        if (reply.pending)
            out << " (pending: applied at the next frame boundary)";
        if (const StateNode* saved = body.find("savedPath"))
            out << " -> " << saved->s;
        out << NEWLINE;
    }
    for (const std::string& line : reply.result.report)
        out << "  note: " << line << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::ShowMediaHelp(const ClientSession& session)
{
    std::ostringstream out;
    out << "Usage: media <verb> [slot] [path] [--options] [--json]" << NEWLINE << NEWLINE;
    out << "Verbs:" << NEWLINE;
    out << "  list                         - every slot and the detached media" << NEWLINE;
    out << "  info <slot>                  - one slot, its tags and medium" << NEWLINE;
    out << "  formats [--kind floppy]      - accepted formats per kind" << NEWLINE;
    out << "  insert <slot|auto> <path>    - a file or a folder; auto picks the slot" << NEWLINE;
    out << "  swap <slot> <path>           - eject + insert in one step" << NEWLINE;
    out << "  eject <slot>                 - take the medium out" << NEWLINE;
    out << "  save <slot> [path]           - floppies: write back (or to path)" << NEWLINE;
    out << "  export <slot> <path>         - a copy of the medium as it is now" << NEWLINE;
    out << "  discard <slot>               - drop the unsaved writes" << NEWLINE;
    out << "  rescan <slot>                - rebuild a folder medium" << NEWLINE;
    out << "  create <slot> [--size bytes] - a blank floppy or card" << NEWLINE;
    out << "  protect <slot> --on true|false - the write-protect switch" << NEWLINE << NEWLINE;
    out << "Slot: id (fdd.b), alias (B, b:, sd, hd), kind:index (floppy:1), tag:a+b" << NEWLINE;
    out << "A dirty medium leaves only with --save, --export <path> or --discard" << NEWLINE;
    out << "Options per verb:" << NEWLINE;
    for (const std::string& v : MediaControl::Verbs())
    {
        std::string options;
        for (const std::string& o : MediaControl::OptionsFor(v))
            options += (options.empty() ? "" : " ") + std::string("--") + o;
        out << "  " << std::left << std::setw(9) << v << (options.empty() ? "-" : options) << NEWLINE;
    }
    out << "Sync by default: the reply comes when the medium is in or out; --async returns at once" << NEWLINE;
    session.SendResponse(out.str());
}

/// endregion </Media Commands>
