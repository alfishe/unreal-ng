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
///   state next dma                                   the DMA as programmed and running (no side effect on its read sequence)
///   state next video                                 layer order, ULA / Layer 2 / tilemap / sprites switches, clip windows, raster
///   state next palette [palette=selected|0-7|name|all] [range=0-255]   the 9-bit palettes
///   state next ports [port=6B] [access=r|w]          the internal port enable word, and which device answers a port
///   state next nextreg [reg=07] [changed=true]       one NextREG with decoded bits, or the table (no side effect)
///   state next copper [from=0] [count=64] [raw=true] the copper list as a disassembly, control mode, write address, pc
///   state next sprites [from=0] [count=128] [all=true]  the sprite attributes decoded (visible ones), switches, flags, pattern memory
///   next nextreg <reg> <value> [nextreg|port|internal]   write a NextREG through the board's write choke point
///   (positional shortcuts: `state next palette sprites_1 0-15`, `state next ports 6B w`, `state next nextreg 07`)
///
/// Worked example: `state next journal on`, run the Browser to a snapshot, `state next journal regs=02` prints the
/// `NEXTREG 2,1` soft resets that no port trace shows.

#include <cstdio>
#include <string>
#include <vector>

#include "debugger/ports/nextregwrite.h"
#include "emulator/emulator.h"
#include "emulator/io/z80n/nextregjournal.h"
#include "emulator/io/z80n/nextreportquery.h"
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


/// `key=value` words and positional shortcuts: the words after the subcommand fill `keys` in order unless they carry a `name=`
inline bool ParseWords(const std::vector<std::string>& args, size_t from, const std::vector<std::string>& keys,
                       std::vector<std::string>& values, std::string& error)
{
    values.assign(keys.size(), std::string());
    size_t positional = 0;
    for (size_t i = from; i < args.size(); i++)
    {
        const size_t eq = args[i].find('=');
        size_t slot = keys.size();
        std::string value = args[i];
        if (eq != std::string::npos)
        {
            const std::string key = args[i].substr(0, eq);
            value = args[i].substr(eq + 1);
            for (size_t k = 0; k < keys.size(); k++)
                if (keys[k] == key)
                    slot = k;
            if (slot == keys.size())
            {
                error = "unknown option '" + key + "' (";
                for (size_t k = 0; k < keys.size(); k++)
                    error += (k ? ", " : "") + keys[k];
                error += ")";
                return false;
            }
        }
        else
        {
            while (positional < keys.size() && !values[positional].empty())
                positional++;
            if (positional >= keys.size())
            {
                error = "too many words: '" + args[i] + "'";
                return false;
            }
            slot = positional++;
        }
        values[slot] = value;
    }
    return true;
}

inline std::string PaletteText(const StateNode& report, const char* newline)
{
    std::string out = "Palette " + Field(*report.find("selected"), "name") + " selected, index " + Field(*report.find("selected"), "index") +
                      ", auto-increment " + Field(*report.find("selected"), "auto_increment") + newline;
    const StateNode& t = *report.find("transparent");
    out += "transparent: global " + Field(t, "global") + ", sprites " + Field(t, "sprites") + ", tilemap " + Field(t, "tilemap") + ", fallback " +
           Field(t, "fallback") + newline;
    for (const StateNode& palette : report.find("palettes")->items)
    {
        out += std::string(newline) + "[" + Field(palette, "palette") + "] " + Field(palette, "name") + newline;
        for (const StateNode& e : palette.find("entries")->items)
        {
            out += "  " + Field(e, "index") + "\t" + Field(e, "rgb9") + "  R" + Field(e, "red") + " G" + Field(e, "green") + " B" + Field(e, "blue");
            if (Field(e, "priority") == "on")
                out += "  priority";
            out += newline;
        }
    }
    return out;
}

inline std::string CopperText(const StateNode& report, const char* newline)
{
    const StateNode& control = *report.find("control");
    std::string out = "Copper " + Field(control, "name") + " (mode " + Field(control, "mode") + "), write address " + Field(report, "address") + " (word " +
                      Field(report, "word_index") + ", " + Field(report, "address_byte") + " next), pc " + Field(report, "pc") + ", line offset " +
                      Field(report, "line_offset") + ", " + Field(report, "list_length") + " word(s) in the list" + newline;
    auto lines = [&](const StateNode& list) {
        for (const StateNode& w : list.items)
            out += std::string(Field(w, "current") == "on" ? "> " : "  ") + Field(w, "index") + "\t" + Field(w, "word") + "  " + Field(w, "text") +
                   (Field(w, "reg_name").empty() ? "" : "  (" + Field(w, "reg_name") + ")") + newline;
    };
    lines(*report.find("instructions"));
    if (const StateNode* around = report.find("around_pc"))
    {
        out += std::string("around the pc:") + newline;
        lines(*around);
    }
    if (!Field(report, "raw").empty())
        out += "raw: " + Field(report, "raw") + newline;
    return out;
}

inline std::string SpritesText(const StateNode& report, const char* newline)
{
    std::string out = "Sprites " + Field(report, "enabled") + ", over border " + Field(report, "over_border") + ", " + Field(report, "visible_count") +
                      " visible; collision " + Field(*report.find("flags"), "collision") + ", too many " + Field(*report.find("flags"), "too_many") + newline;
    for (const StateNode& s : report.find("sprites")->items)
    {
        out += "#" + Field(s, "index") + " " + Field(s, "kind") + " x " + Field(s, "x") + " y " + Field(s, "y") + " pattern " + Field(s, "pattern") +
               (Field(s, "four_bit") == "on" ? " (4-bit)" : "") + " palette +" + Field(s, "palette_offset");
        if (Field(s, "x_mirror") == "on")
            out += " xmirror";
        if (Field(s, "y_mirror") == "on")
            out += " ymirror";
        if (Field(s, "rotate") == "on")
            out += " rotate";
        out += " scale " + Field(s, "scale_x") + "x" + Field(s, "scale_y");
        if (Field(s, "kind") == "relative")
            out += " anchor #" + Field(s, "anchor") + " offset " + Field(s, "offset_x") + "," + Field(s, "offset_y");
        if (Field(s, "visible") != "on")
            out += " (hidden)";
        out += "  [" + Field(s, "bytes") + "]" + newline;
    }
    const StateNode& memory = *report.find("pattern_memory");
    out += "pattern memory: " + Field(memory, "non_zero_bytes") + " non-zero byte(s) of " + Field(memory, "size") + ", " + Field(memory, "used_patterns_8bit") +
           " 8-bit pattern(s) in use" + newline;
    return out;
}

inline std::string PortsText(const StateNode& report, const char* newline)
{
    std::string out;
    if (const StateNode* d = report.find("describe"))
    {
        out += "Port " + Field(*d, "port") + " " + Field(*d, "access") + ": " + Field(*d, "device");
        if (!Field(*d, "channels").empty())
            out += " " + Field(*d, "channels");
        out += newline;
        out += "  decoded by: " + Field(*d, "decoded_by") + newline;
        out += "  enabled: " + Field(*d, "enabled");
        if (const StateNode* by = d->find("enabled_by"))
            out += " (NR " + Field(*by, "nr") + " bit " + Field(*by, "bit") + ", gated by the emulator: " + Field(*d, "enforced") + ")";
        out += newline;
        out += "  side effect: " + Field(*d, "side_effect") + newline + newline;
    }
    const StateNode& w = *report.find("enable_word");
    out += "Internal port enable word " + Field(w, "value") + " (NR #82-#85; restored by " + Field(w, "restored_by") + ")" + newline;
    for (const StateNode& b : w.find("bits")->items)
        out += "  bit " + Field(b, "bit") + (Field(b, "bit").size() < 2 ? " " : "") + " NR" + Field(b, "nr") + "  " + Field(b, "enabled") + "  " +
               Field(b, "ports") + (Field(b, "enforced") == "on" ? "" : "  (not gated)") + newline;
    return out;
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
    if (sub == "dma" || sub == "video")
    {
        const StateNode report = sub == "dma" ? DeviceState::NextDma(context) : DeviceState::NextVideo(context);
        const std::string error = unavailable(report);
        return error.empty() ? DeviceState::ToText(report) : error;
    }
    if (sub == "palette")
    {
        std::vector<std::string> v;
        std::string error;
        NextPaletteQuery query;
        if (!ParseWords(args, 2, {"palette", "range"}, v, error) || !NextPaletteQueryFromStrings(v[0], v[1], query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextPalette(context, query);
        const std::string unavailableText = unavailable(report);
        return unavailableText.empty() ? PaletteText(report, newline) : unavailableText;
    }
    if (sub == "ports")
    {
        std::vector<std::string> v;
        std::string error;
        NextPortsQuery query;
        if (!ParseWords(args, 2, {"port", "access"}, v, error) || !NextPortsQueryFromStrings(v[0], v[1], query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextPorts(context, query);
        const std::string unavailableText = unavailable(report);
        return unavailableText.empty() ? PortsText(report, newline) : unavailableText;
    }
    if (sub == "copper" || sub == "sprites")
    {
        std::vector<std::string> v;
        std::string error;
        if (sub == "copper")
        {
            NextCopperQuery query;
            if (!ParseWords(args, 2, {"from", "count", "raw"}, v, error) || !NextCopperQueryFromStrings(v[0], v[1], v[2], query, error))
                return "Error: " + error + newline;
            const StateNode report = DeviceState::NextCopper(context, query);
            const std::string unavailableText = unavailable(report);
            return unavailableText.empty() ? CopperText(report, newline) : unavailableText;
        }
        NextSpritesQuery query;
        if (!ParseWords(args, 2, {"from", "count", "all"}, v, error) || !NextSpritesQueryFromStrings(v[0], v[1], v[2], query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextSprites(context, query);
        const std::string unavailableText = unavailable(report);
        return unavailableText.empty() ? SpritesText(report, newline) : unavailableText;
    }
    if (sub == "nextreg")
    {
        std::vector<std::string> v;
        std::string error;
        NextRegReadQuery query;
        if (!ParseWords(args, 2, {"reg", "changed"}, v, error) || !NextRegReadQueryFromStrings(v[0], v[1], query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextRegRead(context, query);
        const std::string unavailableText = unavailable(report);
        return unavailableText.empty() ? DeviceState::ToText(report) : unavailableText;
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
    return "Error: unknown subcommand '" + args[1] + "'. Available: (none), regs, mmu, dma, video, palette [palette= range=], ports [port= access=], nextreg [reg= changed=], copper [from= count= raw=], sprites [from= count= all=], journal [regs=.. sources=.. since=N from=F to=F limit=N | on | off | clear]" +
           newline;
}

/// The CLI command `next ...` (args without the command word): `nextreg <reg> <value> [nextreg|port|internal]` writes a NextREG; any
/// other word is a `state next` subcommand (`next dma`)
inline std::string NextCommand(Emulator* emulator, const std::vector<std::string>& args, const char* newline)
{
    if (!emulator)
        return std::string("No emulator selected.") + newline;
    const bool write = !args.empty() && args[0] == "nextreg" && args.size() >= 3 && args.size() <= 4 && args[1].find('=') == std::string::npos;
    if (!write)
    {
        if (!args.empty() && args[0] == "nextreg" && args.size() == 1)
            return std::string("Usage: next nextreg <reg> <value> [nextreg|port|internal]   (hex; `state next nextreg <reg>` reads)") + newline;
        std::vector<std::string> state = {"next"};
        state.insert(state.end(), args.begin(), args.end());
        return StateText(emulator->GetContext(), state, newline);
    }
    uint8_t reg = 0, value = 0;
    std::string error;
    NextRegWriteControl::Door door = NextRegWriteControl::Door::NextReg;
    if (!NextRegWriteControl::Parse(args[1], args[2], reg, value, error))
        return "Error: " + error + newline;
    if (args.size() == 4 && !NextRegWriteControl::ParseDoor(args[3], door))
        return std::string("Error: door must be nextreg, port or internal") + newline;
    const NextRegWriteControl::Result result = NextRegWriteControl::Write(emulator, reg, value, door, "cli");
    if (!result.ok)
        return "Error: " + result.error + newline;
    char text[160];
    std::snprintf(text, sizeof text, "NR #%02X <- #%02X (was #%02X, reads #%02X) through %s (%s)", result.reg, result.value, result.previous,
                  result.after, NextRegWriteControl::DoorName(result.door), result.moment.c_str());
    return std::string(text) + newline;
}
}  // namespace CliNext
