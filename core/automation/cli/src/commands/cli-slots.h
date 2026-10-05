#pragma once

/// @file cli-slots.h
/// @brief CLI `slots` (ZX-bus slots SL-7, architecture.md §9): the arguments become a SlotControlRequest
/// (core/src/emulator/slots/slotcontrol.h), the reply becomes text - the same verbs, options and replies as the WebAPI,
/// MCP, Lua and Python. Header-only so core-tests drive it without a CLI socket.
///
///   slots [list]                                     buses, slots, fitted cards, built-ins
///   slots catalog                                    every card, its options and how it fits this machine
///   slots matrix [table]                             the compatibility tables (markdown)
///   slots plug <slot> <card> [opt=val ...]           a card into a slot (zxbus.next, ay-socket, auto)
///   slots remove <slot>                              the card out of a slot
///   slots set <slot> opt=val ...                     a slot card's options
///   slots gs <gs|gs-lw|neogs>                        the General Sound personality (a slot replace)
///   flags for plug / remove / set / gs: --replace (replaceIfIncompatible), --dry-run, --media save|discard,
///   --adapter <id> (plug); --json for every verb
///
/// Worked example: `slots plug zxbus.next multisound dip=ym,saa,gs,sd --dry-run` prints the plan without changing
/// anything; with `--replace` instead the machine restarts with the card and the reply names the new emulator id.

#include <string>
#include <vector>

#include "emulator/slots/slotcontrol.h"
#include "emulator/state/devicestate.h"

namespace CliSlots
{

/// The request of `slots <args>`; false with `error` (the CLI prints it with the usage)
inline bool ParseArgs(const std::vector<std::string>& args, SlotControlRequest& out, bool& json, std::string& error)
{
    out = SlotControlRequest{};
    json = false;
    std::vector<std::string> positional;
    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        if (arg == "--json")
            json = true;
        else if (arg == "--replace" || arg == "--replace-if-incompatible")
            out.replaceIfIncompatible = true;
        else if (arg == "--dry-run" || arg == "--plan")
            out.dryRun = true;
        else if ((arg == "--media" || arg == "--adapter") && i + 1 < args.size())
            (arg == "--media" ? out.media : out.adapter) = args[++i];
        else if (arg.rfind("--", 0) == 0)
        {
            error = "unknown option '" + arg + "'";
            return false;
        }
        else
            positional.push_back(arg);
    }
    out.verb = positional.empty() ? "list" : positional[0];
    size_t next = 1;
    auto take = [&](std::string& field) {
        if (next < positional.size())
            field = positional[next++];
    };
    if (out.verb == "matrix")
        take(out.table);
    else if (out.verb == "plug")
    {
        take(out.slot);
        take(out.card);
        if (out.card.empty())
        {
            error = "plug needs <slot> <card>";
            return false;
        }
        if (out.slot == "auto")
            out.slot.clear();
    }
    else if (out.verb == "remove" || out.verb == "set")
    {
        take(out.slot);
        if (out.slot.empty())
        {
            error = out.verb + " needs <slot>";
            return false;
        }
    }
    else if (out.verb == "gs")
    {
        take(out.card);
        if (out.card.empty())
        {
            error = "gs needs a personality: gs, gs-lw or neogs";
            return false;
        }
    }
    // The rest: card options name=value (plug, set)
    for (; next < positional.size(); next++)
    {
        const std::string& option = positional[next];
        if ((out.verb != "plug" && out.verb != "set") || option.find('=') == std::string::npos)
        {
            error = "unexpected argument '" + option + "'";
            return false;
        }
        out.options += (out.options.empty() ? "" : " ") + option;
    }
    return true;
}

/// The request of `network set key=value ... [--replace] [--dry-run] [--media save|discard] [--json]` (owner decision
/// Q11: SlotControl verb network; a ZX-bus card change restarts the machine). `args` start after "set"
inline bool ParseNetworkSet(const std::vector<std::string>& args, SlotControlRequest& out, bool& json, std::string& error)
{
    out = SlotControlRequest{};
    out.verb = "network";
    json = false;
    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        if (arg == "--json")
            json = true;
        else if (arg == "--replace" || arg == "--replace-if-incompatible")
            out.replaceIfIncompatible = true;
        else if (arg == "--dry-run" || arg == "--plan")
            out.dryRun = true;
        else if (arg == "--media" && i + 1 < args.size())
            out.media = args[++i];
        else if (arg.rfind("--", 0) == 0)
        {
            error = "unknown option '" + arg + "'";
            return false;
        }
        else
        {
            const size_t eq = arg.find('=');
            if (eq == std::string::npos)
            {
                error = "expected key=value, got '" + arg + "'";
                return false;
            }
            out.settings.emplace_back(arg.substr(0, eq), arg.substr(eq + 1));
        }
    }
    if (out.settings.empty())
    {
        error = "no settings given (key=value ...)";
        return false;
    }
    return true;
}

inline std::string Text(const StateNode* node)
{
    return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
}

/// `slots` as a table: buses, then one line per slot, then the built-ins
inline std::string ListText(const StateNode& report)
{
    if (const StateNode* available = report.find("available"); available != nullptr && !available->b)
        return "slots: " + Text(report.find("description")) + "\n";
    std::string out = Text(report.find("model")) + " (" + Text(report.find("board")) + "), cards from " +
                      Text(report.find("source")) + "\n";
    if (const StateNode* buses = report.find("buses"))
    {
        out += "buses:\n";
        for (const StateNode& bus : buses->items)
        {
            out += "  " + Text(bus.find("id")) + "  " + Text(bus.find("kind")) + ", " +
                   std::to_string(bus.find("physicalSlots")->i) + " slot(s), arbitration " + Text(bus.find("arbitration"));
            if (const StateNode* retrofit = bus.find("retrofit"); retrofit != nullptr && retrofit->b)
                out += " - " + Text(bus.find("retrofitNote"));
            out += "\n";
        }
    }
    if (const StateNode* slots = report.find("slots"))
    {
        out += "slots:\n";
        if (slots->size() == 0)
            out += "  (no cards)\n";
        for (const StateNode& slot : slots->items)
        {
            out += "  " + Text(slot.find("slot")) + " = " + Text(slot.find("card"));
            const std::string options = Text(slot.find("options"));
            if (!options.empty())
                out += " [" + options + "]";
            const std::string adapter = Text(slot.find("adapter"));
            if (!adapter.empty())
                out += " behind " + adapter;
            out += "  fit " + Text(slot.find("fit")) + ", " + Text(slot.find("state"));
            const std::string reason = Text(slot.find("reason"));
            if (!reason.empty())
                out += " (" + reason + ")";
            out += "\n";
        }
    }
    if (const StateNode* builtIns = report.find("builtIns"))
    {
        out += "built-in devices:\n";
        for (const StateNode& builtIn : builtIns->items)
            out += "  " + Text(builtIn.find("id")) + " (" + Text(builtIn.find("name")) + "): " + Text(builtIn.find("state")) + "\n";
    }
    if (const StateNode* log = report.find("log"); log != nullptr && log->size() > 0)
    {
        out += "plan log:\n";
        for (const StateNode& line : log->items)
            out += "  " + line.s + "\n";
    }
    return out;
}

/// `slots catalog`: one line per card with its fit here, then its options
inline std::string CatalogText(const StateNode& reply)
{
    std::string out = "cards for " + Text(reply.find("model")) + ":\n";
    const StateNode* cards = reply.find("cards");
    if (cards == nullptr)
        return out;
    for (const StateNode& card : cards->items)
    {
        const StateNode* here = card.find("thisMachine");
        out += "  " + Text(card.find("id")) + " - " + Text(card.find("name"));
        if (here != nullptr && here->find("outcome") != nullptr)
        {
            out += ": " + Text(here->find("outcome")) + " in " + Text(here->find("slot")) + ", fit " + Text(here->find("fit"));
            const std::string adapter = Text(here->find("adapter"));
            if (!adapter.empty())
                out += " behind " + adapter;
            const StateNode* removes = here->find("removes");
            if (removes != nullptr && removes->size() > 0)
            {
                std::string list;
                for (const StateNode& r : removes->items)
                    list += (list.empty() ? "" : ", ") + r.s;
                out += " (removes " + list + ")";
            }
            const std::string fittedIn = Text(here->find("fittedIn"));
            if (!fittedIn.empty())
                out += "; fitted in " + fittedIn;
        }
        if (const StateNode* emulated = card.find("emulated"); emulated != nullptr && !emulated->b)
            out += " [not emulated]";
        out += "\n";
        if (const StateNode* options = card.find("options"))
        {
            for (const StateNode& option : options->items)
            {
                std::string values;
                for (const StateNode& v : option.find("values")->items)
                    values += (values.empty() ? "" : "|") + Text(v.find("id"));
                out += "      " + Text(option.find("name")) + "=" + values + " (" + Text(option.find("kind")) +
                       ", default " + Text(option.find("default")) + ")\n";
            }
        }
    }
    return out;
}

/// The reply as the CLI prints it ("\n" line ends; the CLI turns them into its NEWLINE)
inline std::string Render(const SlotControlRequest& request, const SlotControlReply& reply, bool json)
{
    if (json)
        return reply.ToJson() + "\n";
    if (!reply.Ok() && reply.body.find("plan") == nullptr)
        return "Error [" + reply.status + "]: " + reply.message + "\n";
    if (request.verb == "list" || request.verb.empty())
        return ListText(reply.body);
    if (request.verb == "catalog")
        return CatalogText(reply.body);
    return reply.ToText();
}

inline const char* Usage()
{
    return "Usage: slots [list] | catalog | matrix [table] | plug <slot> <card> [opt=val ...] | remove <slot> |\n"
           "       set <slot> opt=val ... | gs <gs|gs-lw|neogs>\n"
           "       plug / remove / set / gs: [--replace] [--dry-run] [--media save|discard] [--adapter <id>]; any: [--json]\n"
           "A change is planned first: one that removes a card or fits it unrealistically needs --replace (the plan\n"
           "lists every card it removes), --dry-run shows the plan only. Applied, the machine restarts with the new\n"
           "slot set (a new emulator id; the media follow).\n";
}

} // namespace CliSlots
