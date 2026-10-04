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
///   state sprinter video [page=0|1] [all=1] [squares=1]  the mode table: one letter per square
///   state sprinter palette [0-7|all|used]            the palettes (R, G, B per pen)
///   state sprinter ring                              the Covox-Blaster sample ring
///   state sprinter zx [deep=0]                       the ZX (Spectrum) mode: launcher configuration, clock, ports
///   state sprinter journal [kinds=a,b] [since=N] [from=F] [to=F] [limit=N] [source=live|ttd]
///                                                    who changed the PLD setup, when (frame, T, PC)
///   state sprinter journal on|off|clear              switch / clear the PLD journal
///   state sprinter bios                              the BIOS images, which one runs, the start options
///   state sprinter bios <3.04|3.06|3.07|file|-> [fast_start=0|1] [accel_int_suspend=0|1] [reset=0|1]
///                                                    select (the image loads at the reset; reset=1 default)
///
/// Worked example: `state sprinter port 21BC rw=w` on BIOS 3.04 after boot prints the index
/// #003C and code #2B, IdePrimary.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "emulator/ports/models/sprinter/sprinterbios.h"
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

/// A scalar member as text ("" when absent; bools as on / off)
inline std::string Field(const StateNode& node, const char* key)
{
    const StateNode* member = node.find(key);
    if (!member)
        return std::string();
    if (member->kind == StateNode::Kind::String)
        return member->s;
    if (member->kind == StateNode::Kind::Bool)
        return member->b ? "on" : "off";
    return std::to_string(member->i);
}

/// `state sprinter video`: the registers on two lines, the counts, then the map (one letter a square)
inline std::string VideoMapText(const StateNode& report, const char* newline)
{
    const StateNode* map = report.find("map");
    if (!map)
        return "Error: " + Field(report, "description") + newline;
    std::string out = "Sprinter mode table: page " + Field(report, "mode_page") +
                      (Field(report, "displayed") == "on" ? " (displayed)" : " (not displayed)") + ", RGMOD " +
                      Field(report, "rgmod") + ", PORT_Y " + Field(report, "port_y") + ", border " +
                      Field(report, "border") + newline;
    if (const StateNode* hold = report.find("hold"))
        out += "HOLD " + Field(*hold, "value") + " (x " + Field(*hold, "x_pixels") + ", y " + Field(*hold, "y_lines") + ")";
    if (const StateNode* frame = report.find("frame"))
        out += ", frame " + Field(*frame, "lines") + " lines (" + Field(*frame, "lines_requested") + " requested)";
    out += newline;
    if (const StateNode* counts = report.find("counts"))
    {
        out += "Squares:";
        for (const auto& [key, value] : counts->members)
            out += " " + key + " " + std::to_string(value.i);
        out += newline;
    }
    out += Field(report, "legend") + newline;
    for (const StateNode& row : map->items)
        out += row.s + newline;
    return out;
}

/// `state sprinter palette`: one block per palette, 16 pens a line as RRGGBB
inline std::string PaletteText(const StateNode& report, const char* newline)
{
    const StateNode* palettes = report.find("palettes");
    if (!palettes)
        return "Error: " + Field(report, "description") + newline;
    std::string out = "Sprinter palettes (" + Field(report, "selection") + "): R, G, B per pen as video RAM holds them" +
                      newline;
    for (const StateNode& p : palettes->items)
    {
        out += "Palette " + Field(p, "k") + " (" + Field(p, "role") + ", column " + Field(p, "vram_column") +
               (Field(p, "used_by_picture") == "on" ? ", used" : "") + ")" + newline;
        const std::string row = Field(p, "rgb_row");
        for (size_t pen = 0; pen < 256; pen += 16)
        {
            char head[8];
            std::snprintf(head, sizeof head, "  %02X:", static_cast<unsigned>(pen));
            out += head;
            out += " " + row.substr(pen * 7, 16 * 7 - 1) + newline;
        }
    }
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
    if (sub == "video" || sub == "modes")
    {
        std::string page, all, squares;
        for (size_t i = 2; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            const std::string key = eq == std::string::npos ? args[i] : args[i].substr(0, eq);
            const std::string value = eq == std::string::npos ? std::string("1") : args[i].substr(eq + 1);
            if (key == "page")
                page = value;
            else if (key == "all")
                all = value;
            else if (key == "squares" || key == "detail")
                squares = value;
            else
                return "Error: unknown option '" + key + "' (page, all, squares)" + newline;
        }
        DeviceState::SprinterVideoQuery query;
        std::string error;
        if (!DeviceState::SprinterVideoQueryFromStrings(page, all, squares.empty() ? "0" : squares, query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::SprinterVideo(context, query);
        if (query.squares)
            return std::string("Sprinter video") + newline + "==============" + newline + DeviceState::ToText(report);
        return VideoMapText(report, newline);
    }
    if (sub == "palette" || sub == "palettes")
    {
        int palette = DeviceState::kSprinterPalettesUsed;
        std::string error;
        if (!DeviceState::SprinterPaletteFromString(args.size() > 2 ? args[2] : std::string(), palette, error))
            return "Error: " + error + newline;
        return PaletteText(DeviceState::SprinterPalette(context, palette), newline);
    }
    if (sub == "zx" || sub == "zx-mode" || sub == "zxmode")
    {
        bool deep = true;
        for (size_t i = 2; i < args.size(); i++)
        {
            if (args[i] == "deep=0" || args[i] == "deep=off")
                deep = false;
            else if (args[i] != "deep=1" && args[i] != "deep=on")
                return "Error: unknown option '" + args[i] + "' (deep=0|1)" + newline;
        }
        const StateNode report = DeviceState::SprinterZxMode(context, deep);
        const StateNode* available = report.find("available");
        if (available && !available->b)
            return "Error: " + Field(report, "description") + newline;
        return Field(report, "summary") + newline + std::string("Sprinter ZX mode") + newline + "================" + newline +
               DeviceState::ToText(report);
    }
    if (sub == "journal" || sub == "pld-journal" || sub == "pld")
    {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off" || args[2] == "clear"))
        {
            const StateNode r = DeviceState::SprinterJournalControl(context, args[2] == "clear" ? -1 : (args[2] == "on" ? 1 : 0),
                                                                       args[2] == "clear");
            const StateNode* available = r.find("available");
            if (available && !available->b)
                return "Error: " + Field(r, "description") + newline;
            return "PLD journal " + std::string(Field(r, "enabled") == "on" ? "on" : "off") + ", " + Field(r, "held") +
                   " event(s) held" + newline;
        }
        std::string kinds, since, from, to, limit, source;
        for (size_t i = 2; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            if (eq == std::string::npos)
                return "Error: expected key=value, got '" + args[i] + "' (kinds, since, from, to, limit, source)" + newline;
            const std::string key = args[i].substr(0, eq);
            const std::string value = args[i].substr(eq + 1);
            if (key == "kinds" || key == "kind")
                kinds = value;
            else if (key == "since")
                since = value;
            else if (key == "from")
                from = value;
            else if (key == "to")
                to = value;
            else if (key == "limit")
                limit = value;
            else if (key == "source")
                source = value;
            else
                return "Error: unknown option '" + key + "' (kinds, since, from, to, limit, source)" + newline;
        }
        DeviceState::SprinterJournalQuery query;
        std::string error;
        if (!DeviceState::SprinterJournalQueryFromStrings(kinds, since, from, to, limit, source, query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::SprinterJournal(context, query);
        const StateNode* events = report.find("events");
        if (!events)
            return "Error: " + Field(report, "description") + newline;
        std::string out = "PLD journal (" + Field(report, "source") + "): " + std::to_string(events->items.size()) + " event(s)";
        if (report.find("error"))
            out += ", " + Field(report, "error");
        out += newline;
        for (const StateNode& e : events->items)
        {
            out += "frame " + Field(e, "frame") + " T " + Field(e, "t") + " (line " + Field(e, "line") + " T " + Field(e, "t_in_line") +
                   ") PC " + Field(e, "pc") + " " + Field(e, "kind") + ": " + Field(e, "text") + newline;
            if (const StateNode* details = e.find("details"))
                for (const StateNode& d : details->items)
                    out += "    " + d.s + newline;
        }
        if (const StateNode* queries = report.find("ttd_queries"))
        {
            out += "TTD port-events queries (the table now):" + std::string(newline);
            for (const StateNode& q : queries->items)
            {
                std::string ports;
                if (const StateNode* list = q.find("ports"))
                    for (const StateNode& p : list->items)
                        ports += " " + Field(p, "port") + "/" + Field(p, "port_mask");
                out += "  " + Field(q, "kind") + ":" + (ports.empty() ? std::string(" (no port reaches it)") : ports) + newline;
            }
        }
        return out;
    }
    if (sub == "bios")
    {
        if (args.size() <= 2)
            return std::string("Sprinter BIOS") + newline + "=============" + newline +
                   DeviceState::ToText(DeviceState::SprinterBios(context));
        std::string bios = args[2] == "-" ? std::string() : args[2];
        std::string fastStart, intSuspend, reset;
        for (size_t i = 3; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            const std::string key = eq == std::string::npos ? args[i] : args[i].substr(0, eq);
            const std::string value = eq == std::string::npos ? std::string("1") : args[i].substr(eq + 1);
            if (key == "fast_start")
                fastStart = value;
            else if (key == "accel_int_suspend" || key == "int_suspend")
                intSuspend = value;
            else if (key == "reset")
                reset = value;
            else
                return "Error: unknown option '" + key + "' (fast_start, accel_int_suspend, reset)" + newline;
        }
        SprinterBios::Options options;
        std::string error;
        if (!SprinterBios::OptionsFromStrings(bios, fastStart, intSuspend, reset, options, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::SprinterBiosSelect(context, options);
        const StateNode* available = report.find("available");
        if (available && !available->b)
            return "Error: " + Field(report, "description") + newline;
        return std::string("Selected: ") + Field(report, "rom_file") +
               (Field(report, "reset_done") == "on" ? " (reset done)" : " (loads at the next reset)") + newline +
               "Loaded: " + Field(report, "loaded") + newline;
    }
    if (sub == "ring" || sub == "cbl")
    {
        const StateNode report = DeviceState::SprinterSoundRing(context);
        const StateNode* rows = report.find("rows");
        if (!rows)
            return std::string("Error: not a Sprinter machine") + newline;
        std::string out = "Covox-Blaster ring: " + Field(report, "mode") + ", play " + Field(report, "play_index") +
                          ", write " + Field(report, "write_index") + " ([ ] playing, < > next write)" + newline;
        for (const StateNode& row : rows->items)
            out += row.s + newline;
        return out;
    }
    if (sub != "ports" && sub != "port" && sub != "lookup")
        return "Error: unknown subcommand '" + args[1] + "'. Available: ports, port <hex>, text, video, palette, ring, bios, zx, journal" +
               newline;

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
