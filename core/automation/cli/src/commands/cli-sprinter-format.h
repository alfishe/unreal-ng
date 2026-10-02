#pragma once

/// @file cli-sprinter-format.h
/// @brief CLI `state sprinter` (Sprinter tdd-integration §3): the arguments to a DeviceState
/// query and the text. Header-only so core-tests drive it without a CLI socket; the reports
/// are the core's (DeviceState::Sprinter / SprinterPortTable / SprinterPortLookup), the same
/// trees the WebAPI, Lua, Python and MCP render.
///
///   state sprinter                                   the machine state
///   state sprinter ports [map=0-3] [dos=0|1] [pn5=0|1] [rw=r|w|rw]
///   state sprinter port <hex> [rw=r|w|rw] [map=..] [dos=..] [pn5=..]
///   state sprinter text                              the screen text (80 x 32)
///
/// Worked example: `state sprinter port 21BC rw=w` on BIOS 3.04 after boot prints the index
/// #003C and code #2B, IdePrimary.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "emulator/state/devicestate.h"

namespace CliSprinter
{
/// key=value arguments from args[from..]; a bare word goes to `positional`. False on an unknown key
inline bool ParseOptions(const std::vector<std::string>& args, size_t from, std::string& map, std::string& dos,
                         std::string& pn5, std::string& rw, std::string& positional, std::string& error)
{
    for (size_t i = from; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos)
        {
            if (!positional.empty())
            {
                error = "unexpected argument '" + arg + "'";
                return false;
            }
            positional = arg;
            continue;
        }
        std::string key = arg.substr(0, eq);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string value = arg.substr(eq + 1);
        if (key == "map")
            map = value;
        else if (key == "dos")
            dos = value;
        else if (key == "pn5")
            pn5 = value;
        else if (key == "rw" || key == "dir")
            rw = value;
        else
        {
            error = "unknown option '" + key + "' (map, dos, pn5, rw)";
            return false;
        }
    }
    return true;
}

/// The port table as one line per row ("#10 r  xxxx xxxx 000x x111  #0007    8  FdcCommand"):
/// the full tree (ToText) would take seven lines a row
inline std::string PortTableText(const StateNode& table, const char* newline)
{
    auto text = [](const StateNode& node, const char* key) {
        const StateNode* member = node.find(key);
        if (!member)
            return std::string();
        if (member->kind == StateNode::Kind::String)
            return member->s;
        if (member->kind == StateNode::Kind::Bool)
            return std::string(member->b ? "on" : "off");
        return std::to_string(member->i);
    };
    const StateNode* available = table.find("available");
    if (available && available->kind == StateNode::Kind::Bool && !available->b)
        return "Error: " + text(table, "description") + newline;

    // "0x10" -> "#10"
    auto hash = [](const std::string& hex) { return hex.size() > 2 && hex[1] == 'x' ? "#" + hex.substr(2) : hex; };
    std::string out = "Sprinter port table: map " + text(table, "map") + ", DOS " + text(table, "dos") + ", PN5 " +
                      std::string(text(table, "pn5") == "on" ? "1" : "0") + ", " + text(table, "source") + newline;
    if (table.find("note"))
        out += "Note: " + text(table, "note") + newline;
    out += std::string("code dir  A15..A0 (x = any)    example  ports  name") + newline;
    if (const StateNode* rows = table.find("rows"))
    {
        for (const StateNode& row : rows->items)
        {
            char line[160];
            std::snprintf(line, sizeof(line), "%-4s %-3s  %-19s  %-7s  %5s  %s", hash(text(row, "code")).c_str(),
                          text(row, "direction").c_str(), text(row, "pattern").c_str(), hash(text(row, "example")).c_str(),
                          text(row, "addresses").c_str(), text(row, "name").c_str());
            out += line;
            out += newline;
        }
    }
    out += "Unmapped (code #00, reads #FF): " + text(table, "unmapped_combinations") + " of the address combinations" + newline;
    out += std::string("Z84C15 on-chip (never in the table): #10-#13 CTC, #18-#1B SIO, #1C-#1F PIO, #EE/#EF, #F0/#F1, #F4") + newline;
    return out;
}

/// The response text for `state sprinter ...`; args[0] is the subsystem word ("sprinter" / "sp")
inline std::string StateText(EmulatorContext* context, const std::vector<std::string>& args, const char* newline = "\n")
{
    std::string sub = args.size() > 1 ? args[1] : std::string();
    std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (sub.empty())
        return std::string("Sprinter") + newline + "========" + newline + DeviceState::ToText(DeviceState::Sprinter(context));

    if (sub == "text" || sub == "screen")
    {
        const StateNode report = DeviceState::SprinterText(context);
        const StateNode* lines = report.find("lines");
        if (!lines)
            return std::string("Error: not a Sprinter machine") + newline;
        std::string out;
        for (const StateNode& line : lines->items)
        {
            const StateNode* text = line.find("text");
            out += (text ? text->s : std::string()) + newline;
        }
        return out;
    }
    if (sub != "ports" && sub != "port" && sub != "lookup")
        return "Error: unknown subcommand '" + args[1] + "'. Available: ports, port <hex>, text" + newline;

    std::string map, dos, pn5, rw, positional, error;
    DeviceState::SprinterPortQuery query;
    if (!ParseOptions(args, 2, map, dos, pn5, rw, positional, error) ||
        !DeviceState::SprinterPortQueryFromStrings(map, dos, pn5, rw, query, error))
        return "Error: " + error + newline;

    if (sub == "ports")
    {
        if (!positional.empty())
            return "Error: unexpected argument '" + positional + "'" + newline;
        return PortTableText(DeviceState::SprinterPortTable(context, query), newline);
    }

    uint16_t port = 0;
    if (!DeviceState::SprinterPortFromString(positional, port))
        return std::string("Error: state sprinter port <hex> [rw=r|w] [map=0-3] [dos=0|1] [pn5=0|1]") + newline;
    return std::string("Sprinter port lookup") + newline + "====================" + newline +
           DeviceState::ToText(DeviceState::SprinterPortLookup(context, port, query));
}
}  // namespace CliSprinter
