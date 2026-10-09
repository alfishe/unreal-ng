#pragma once

/// @file cli-next-format.h
/// @brief CLI `state next` (docs/inprogress/2026-10-07-zx-next/design-automation.md, design-nextreg-journal.md): the arguments to a
/// DeviceState query and the text. Header-only so core-tests drive it without a CLI socket; the reports are the core's
/// (DeviceState::Next / NextRegs / NextMmu / NextRegJournalReport), the same trees the WebAPI, Lua, Python and MCP render.
///
///   state next                                       the machine: type, CPU clock, DivMMC, interrupts, CTC, SPI
///   state next regs                                  the NextREG table with values and reset values
///   state next mmu                                   the eight 8K slots and the paging latches
///   state next journal [regs=07,02] [sources=nextreg,port,copper,internal] [since=N] [from=F] [to=F] [limit=N]
///                                                    who wrote which NextREG, when (frame, T, PC), through which door
///   state next journal on|off|clear                  switch / clear the NextREG write journal (off by default)
///
/// Worked example: `state next journal on`, run the Browser to a snapshot, `state next journal regs=02` prints the
/// `NEXTREG 2,1` soft resets that no port trace shows.

#include <string>
#include <vector>

#include "emulator/io/z80n/nextregjournal.h"
#include "emulator/state/devicestate.h"

namespace CliNext
{
inline std::string Field(const StateNode& node, const char* key)
{
    const StateNode* member = node.find(key);
    if (!member)
        return std::string();
    switch (member->kind)
    {
        case StateNode::Kind::String: return member->s;
        case StateNode::Kind::Bool: return member->b ? "on" : "off";
        case StateNode::Kind::Int: return std::to_string(member->i);
        default: return std::string();
    }
}

/// `state next [sub ...]`; args[0] is "next"
inline std::string StateText(EmulatorContext* context, const std::vector<std::string>& args, const char* newline)
{
    const std::string sub = args.size() > 1 ? args[1] : std::string();
    auto unavailable = [&](const StateNode& report) {
        const StateNode* available = report.find("available");
        return available && !available->b ? "Error: " + Field(report, "description") + newline : std::string();
    };
    if (sub.empty())
    {
        const StateNode report = DeviceState::Next(context);
        const std::string error = unavailable(report);
        return error.empty() ? "ZX Spectrum Next" + std::string(newline) + "================" + newline + DeviceState::ToText(report) : error;
    }
    if (sub == "regs")
    {
        const StateNode report = DeviceState::NextRegs(context);
        const std::string error = unavailable(report);
        return error.empty() ? DeviceState::ToText(report) : error;
    }
    if (sub == "mmu")
    {
        const StateNode report = DeviceState::NextMmu(context);
        const std::string error = unavailable(report);
        return error.empty() ? DeviceState::ToText(report) : error;
    }
    if (sub == "journal" || sub == "regjournal" || sub == "reg-journal")
    {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off" || args[2] == "clear"))
        {
            const StateNode r = DeviceState::NextRegJournalControl(context, args[2] == "clear" ? -1 : (args[2] == "on" ? 1 : 0),
                                                                   args[2] == "clear");
            const std::string error = unavailable(r);
            if (!error.empty())
                return error;
            return "NextREG journal " + Field(r, "enabled") + ", " + Field(r, "size") + " event(s) held, " + Field(r, "evicted") +
                   " evicted" + newline;
        }
        std::string regs, sources, since, from, to, limit;
        const char* keys = "regs, sources, since, from, to, limit";
        for (size_t i = 2; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            if (eq == std::string::npos)
                return "Error: expected key=value, got '" + args[i] + "' (" + keys + ")" + newline;
            const std::string key = args[i].substr(0, eq);
            const std::string value = args[i].substr(eq + 1);
            if (key == "regs" || key == "reg")
                regs = value;
            else if (key == "sources" || key == "source")
                sources = value;
            else if (key == "since")
                since = value;
            else if (key == "from")
                from = value;
            else if (key == "to")
                to = value;
            else if (key == "limit")
                limit = value;
            else
                return "Error: unknown option '" + key + "' (" + keys + ")" + newline;
        }
        NextRegJournalQuery query;
        std::string error;
        if (!NextRegJournalQueryFromStrings(regs, sources, since, from, to, limit, query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextRegJournalReport(context, query);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        const StateNode* events = report.find("events");
        std::string out = "NextREG journal " + Field(report, "enabled") + ": " + std::to_string(events ? events->items.size() : 0) +
                          " event(s) of " + Field(report, "size") + " held, " + Field(report, "evicted") + " evicted" + newline;
        if (Field(report, "enabled") == "off")
            out += "  (off: `state next journal on` first)" + std::string(newline);
        if (events)
            for (const StateNode& e : events->items)
            {
                out += "#" + Field(e, "seq") + " frame " + Field(e, "frame") + " T " + Field(e, "t") + " PC " + Field(e, "pc") + " " +
                       Field(e, "source") + " NR" + Field(e, "reg") + " " + Field(e, "previous") + " -> " + Field(e, "value");
                if (!Field(e, "name").empty())
                    out += "  " + Field(e, "name");
                if (!Field(e, "decoded").empty())
                    out += " (" + Field(e, "decoded") + ")";
                out += newline;
            }
        return out;
    }
    return "Error: unknown subcommand '" + args[1] + "'. Available: (none), regs, mmu, journal [regs=.. sources=.. since=N from=F to=F limit=N | on | off | clear]" +
           newline;
}
}  // namespace CliNext
