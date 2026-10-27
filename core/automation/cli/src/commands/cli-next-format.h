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
///   state next uart                                  the two UARTs (ESP, Pi): baud, frame, FIFOs, status, interrupts, what is on the line
///   state next nextreg [reg=07] [changed=true]       one NextREG with decoded bits, or the table (no side effect)
///   state next copper [from=0] [count=64] [raw=true] the copper list as a disassembly, control mode, write address, pc
///   state next sprites [from=0] [count=128] [all=true]  the sprite attributes decoded (visible ones), switches, flags, pattern memory
///   state next pixel <x> <y> [sx]                    the pixel inspector: which layer won the pixel of the last captured frame, why each layer lost or did
///                                                    not paint, where its byte came from (x 0-319, y 0-255, sx the sub-pixel 0|1)
///   state next layer-capture [on|off]                the per-layer record of the screen: switch it, or show enabled / frames / last frame / bytes
///   (the images: `capture screen --layer=ula|layer2|tilemap|sprites|ids [file]`)
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
#include "emulator/io/z80n/nextdivmmcjournal.h"
#include "emulator/io/z80n/nextintjournal.h"
#include "emulator/io/z80n/nextsdjournal.h"
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

inline std::string PixelText(const StateNode& report, const char* newline)
{
    const StateNode& pixel = *report.find("pixel");
    const StateNode& winner = *report.find("winner");
    const StateNode& colour = *report.find("colour");
    std::string out = "Pixel (" + Field(pixel, "x") + "," + Field(pixel, "y") + ") sub-pixel " + Field(pixel, "subpixel") + " of Next frame #" +
                      Field(*report.find("frame"), "sequence") + " (emulator frame " + Field(*report.find("frame"), "frame_number") + "), line segment " +
                      Field(pixel, "segment_from_x") + "-" + Field(pixel, "segment_to_x") + newline;
    out += "winner: " + Field(winner, "layer") + "  colour " + Field(colour, "rgb9") + " " + Field(colour, "rgb") + "  layer order " + Field(report, "layer_order") +
           newline;
    for (const char* name : {"ula", "layer2", "tilemap", "sprites"})
    {
        const StateNode& layer = *report.find("layers")->find(name);
        out += std::string("  ") + name + (std::string(name).size() < 7 ? std::string(7 - std::string(name).size(), ' ') : "") + ": " +
               (Field(layer, "opaque") == "on" ? "painted " + Field(layer, "rgb9") + " " + Field(layer, "rgb") : std::string("not painted")) + "  - " +
               Field(layer, "why") + newline;
        if (const StateNode* source = layer.find("source"))
        {
            std::string line;
            for (const auto& member : source->members)
            {
                const StateNode& v = member.second;
                const std::string text = v.kind == StateNode::Kind::String ? v.s : (v.kind == StateNode::Kind::Int ? std::to_string(v.i) : (v.kind == StateNode::Kind::Bool ? (v.b ? "yes" : "no") : std::string()));
                if (!text.empty())
                    line += (line.empty() ? "" : ", ") + member.first + " " + text;
            }
            if (!line.empty())
                out += "           " + line + newline;
        }
    }
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
    if (sub == "pixel")
    {
        std::vector<std::string> v;
        std::string error;
        NextPixelQuery query;
        if (!ParseWords(args, 2, {"x", "y", "sx"}, v, error) || !NextPixelQueryFromStrings(v[0], v[1], v[2], query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextPixel(context, query);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        if (const StateNode* ok = report.find("ok"); ok && !ok->b)
            return "Error: " + Field(report, "message") + newline;
        return PixelText(report, newline);
    }
    if (sub == "layer-capture" || sub == "layercapture" || sub == "layers")
    {
        int enable = -1;
        if (args.size() >= 3)
        {
            if (args[2] == "on" || args[2] == "true" || args[2] == "1")
                enable = 1;
            else if (args[2] == "off" || args[2] == "false" || args[2] == "0")
                enable = 0;
            else if (args[2] != "status")
                return "Error: expected on, off or status, got '" + args[2] + "'" + newline;
        }
        const StateNode r = DeviceState::NextLayerCaptureControl(context, enable);
        const std::string unavailableText = unavailable(r);
        if (!unavailableText.empty())
            return unavailableText;
        return "Next layer capture " + Field(r, "enabled") + ": " + Field(r, "frames") + " frame(s) recorded, last emulator frame " +
               Field(r, "last_frame") + ", " + Field(r, "bytes") + " bytes held" +
               (enable >= 0 ? " (the switch applies from the next frame start)" : "") + newline;
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
                       (Field(e, "beam_line").empty() ? std::string() : "beam " + Field(e, "beam_line") + ":" + Field(e, "beam_hc") + " ") +
                       Field(e, "source") + " NR" + Field(e, "reg") + " " + Field(e, "previous") + " -> " + Field(e, "value");
                if (!Field(e, "name").empty())
                    out += "  " + Field(e, "name");
                if (!Field(e, "decoded").empty())
                    out += " (" + Field(e, "decoded") + ")";
                out += newline;
            }
        return out;
    }
    if (sub == "sd" || sub == "sd-journal" || sub == "sdjournal")
    {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off" || args[2] == "clear"))
        {
            const StateNode r = DeviceState::NextSdJournalControl(context, args[2] == "clear" ? -1 : (args[2] == "on" ? 1 : 0),
                                                                  args[2] == "clear");
            const std::string error = unavailable(r);
            if (!error.empty())
                return error;
            return "SD journal " + Field(r, "enabled") + ", " + Field(r, "size") + " event(s) held, " + Field(r, "evicted") + " evicted" +
                   newline;
        }
        std::string kinds, card, since, from, to, limit;
        const char* keys = "kinds, card, since, from, to, limit";
        for (size_t i = 2; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            if (eq == std::string::npos)
                return "Error: expected key=value, got '" + args[i] + "' (" + keys + ")" + newline;
            const std::string key = args[i].substr(0, eq);
            const std::string value = args[i].substr(eq + 1);
            if (key == "kinds" || key == "kind")
                kinds = value;
            else if (key == "card")
                card = value;
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
        NextSdJournalQuery query;
        std::string error;
        if (!NextSdJournalQueryFromStrings(kinds, card, since, from, to, limit, query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextSdReport(context, query);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out;
        if (const StateNode* cards = report.find("cards"))
            for (const StateNode& c : cards->items)
            {
                out += "card " + Field(c, "card") + ": ";
                if (Field(c, "present") == "on")
                    out += Field(c, "type") + ", " + Field(c, "state") + ", " + Field(c, "size_bytes") + " bytes, read " +
                           Field(c, "blocks_read") + " / written " + Field(c, "blocks_written") + " block(s)";
                else
                    out += "none";
                out += newline;
            }
        if (const StateNode* spi = report.find("spi"))
            out += "SPI: selected " + Field(*spi, "selected") + ", byte in flight " + Field(*spi, "byte_in_flight") + ", last received " +
                   Field(*spi, "last_received") + ", too fast " + Field(*spi, "too_fast_accesses") + newline;
        const StateNode* events = report.find("events");
        out += "SD journal " + Field(report, "enabled") + ": " + std::to_string(events ? events->items.size() : 0) + " event(s) of " +
               Field(report, "size") + " held, " + Field(report, "evicted") + " evicted" + newline;
        if (Field(report, "enabled") == "off")
            out += "  (off: `state next sd on` first)" + std::string(newline);
        if (events)
            for (const StateNode& e : events->items)
            {
                out += "#" + Field(e, "seq") + " frame " + Field(e, "frame") + " T " + Field(e, "t") + " PC " + Field(e, "pc") + " card " +
                       Field(e, "card") + " ";
                if (Field(e, "kind") == "command")
                    out += Field(e, "command") + " " + Field(e, "argument") + " -> R1 " + Field(e, "r1") +
                           (Field(e, "crc_valid") == "off" ? " (bad CRC)" : "");
                else
                    out += Field(e, "kind") + " block " + Field(e, "block") + (Field(e, "ok") == "off" ? " (refused)" : "");
                out += newline;
            }
        return out;
    }
    if (sub == "divmmc" || sub == "divmmc-journal")
    {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off" || args[2] == "clear"))
        {
            const StateNode r = DeviceState::NextDivMmcJournalControl(context, args[2] == "clear" ? -1 : (args[2] == "on" ? 1 : 0),
                                                                      args[2] == "clear");
            const std::string error = unavailable(r);
            if (!error.empty())
                return error;
            return "DivMMC journal " + Field(r, "enabled") + ", " + Field(r, "size") + " event(s) held, " + Field(r, "evicted") + " evicted" +
                   newline;
        }
        std::string kinds, since, from, to, limit;
        const char* keys = "kinds, since, from, to, limit";
        for (size_t i = 2; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            if (eq == std::string::npos)
                return "Error: expected key=value, got '" + args[i] + "' (" + keys + ")" + newline;
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
            else
                return "Error: unknown option '" + key + "' (" + keys + ")" + newline;
        }
        NextDivMmcJournalQuery query;
        std::string error;
        if (!NextDivMmcJournalQueryFromStrings(kinds, since, from, to, limit, query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextDivMmcReport(context, query);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out;
        if (const StateNode* st = report.find("state"))
            out += std::string("DivMMC: ") + (Field(*st, "mapped") == "on" ? "in" : "out") + ", #E3 " + Field(*st, "control") + " (conmem " +
                   Field(*st, "conmem") + ", mapram " + Field(*st, "mapram") + ", bank " + Field(*st, "bank") + "), automap " +
                   (Field(*st, "automap_armed") == "on" ? "armed" : "off") + ", latch " + Field(*st, "automap_latch") + ", button NMI pending " +
                   Field(*st, "button_pending") + newline;
        if (const StateNode* entries = report.find("entries"))
            for (const StateNode& e : entries->items)
                if (Field(e, "enabled") == "on")
                    out += "  entry " + Field(e, "address") + " " + Field(e, "when") + ", " + Field(e, "map") +
                           (Field(e, "what").empty() ? std::string() : "  (" + Field(e, "what") + ")") + newline;
        const StateNode* events = report.find("events");
        out += "DivMMC journal " + Field(report, "enabled") + ": " + std::to_string(events ? events->items.size() : 0) + " event(s) of " +
               Field(report, "size") + " held, " + Field(report, "evicted") + " evicted" + newline;
        if (Field(report, "enabled") == "off")
            out += "  (off: `state next divmmc on` first)" + std::string(newline);
        if (events)
            for (const StateNode& e : events->items)
            {
                out += "#" + Field(e, "seq") + " frame " + Field(e, "frame") + " T " + Field(e, "t") + " PC " + Field(e, "pc") + " " +
                       Field(e, "kind");
                if (!Field(e, "address").empty())
                    out += " " + Field(e, "address");
                if (Field(e, "kind") == "map_in")
                    out += " (" + Field(e, "entry") + ", " + Field(e, "map") + ")";
                if (Field(e, "kind") == "esx_call")
                {
                    out += " " + (Field(e, "name").empty() ? Field(e, "call") : Field(e, "name")) + " return " + Field(e, "return_address") +
                           " AF " + Field(e, "af") + " BC " + Field(e, "bc") + " DE " + Field(e, "de") + " HL " + Field(e, "hl");
                    if (Field(e, "basic_rom") == "on")
                        out += " (BASIC ROM: an error report, not an esxDOS call)";
                }
                out += newline;
            }
        return out;
    }
    if (sub == "audio")
    {
        const StateNode report = DeviceState::NextAudioReport(context);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out;
        if (const StateNode* cfg = report.find("config"))
            out += "Sound: " + Field(*cfg, "chip_model") + " " + Field(*cfg, "stereo_mode") + ", turbosound " + Field(*cfg, "turbosound") + ", DAC " +
                   Field(*cfg, "dac") + ", beeper excluded " + Field(*cfg, "beeper_excluded") + " (NR #06 " + Field(*cfg, "nr_06") + ", #08 " +
                   Field(*cfg, "nr_08") + ", #09 " + Field(*cfg, "nr_09") + "), selected chip " + Field(report, "selected_chip") + newline;
        if (const StateNode* chips = report.find("chips"))
            for (const StateNode& chip : chips->items)
            {
                out += "chip " + Field(chip, "index") + (Field(chip, "selected") == "on" ? "*" : " ") + " pan " + Field(chip, "pan") +
                       (Field(chip, "mono") == "on" ? " mono" : "") + ", tones";
                if (const StateNode* tones = chip.find("tone_periods"))
                    for (const StateNode& tone : tones->items)
                        out += " " + std::to_string(tone.i);
                out += ", noise " + Field(chip, "noise_period") + ", mixer " + Field(chip, "mixer") + ", volumes";
                if (const StateNode* volumes = chip.find("volumes"))
                    for (const StateNode& volume : volumes->items)
                        out += " " + std::to_string(volume.i);
                out += ", envelope " + Field(chip, "envelope_period") + " shape " + Field(chip, "envelope_shape") + newline;
            }
        if (const StateNode* dac = report.find("dac"))
            out += "DAC " + Field(*dac, "enabled") + ": A " + Field(*dac, "a") + " B " + Field(*dac, "b") + " C " + Field(*dac, "c") + " D " +
                   Field(*dac, "d") + " (left " + Field(*dac, "left_sum") + ", right " + Field(*dac, "right_sum") + ")" + newline;
        return out;
    }
    if (sub == "uart")
    {
        const StateNode report = DeviceState::NextUartReport(context);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out;
        const StateNode* ports = report.find("ports");
        out += "UART select " + Field(report, "selected_name") + " (#153B); ports " + (ports && Field(*ports, "enabled") == "on" ? "on" : "off (NR #83 bit 4)") + newline;
        if (const StateNode* channels = report.find("channels"))
            for (const StateNode& c : channels->items)
            {
                const StateNode* frame = c.find("frame");
                const StateNode* rx = c.find("rx");
                const StateNode* tx = c.find("tx");
                const StateNode* status = c.find("status");
                const StateNode* bytes = c.find("bytes");
                const StateNode* irq = c.find("interrupts");
                const StateNode* peer = c.find("peer");
                out += Field(c, "name") + ": " + Field(c, "baud") + " baud, " + (frame ? Field(*frame, "data_bits") + Field(*frame, "parity") + Field(*frame, "stop_bits") : std::string()) +
                       (frame && Field(*frame, "hardware_flow") == "on" ? ", flow control" : "") + ", RTS " + (Field(c, "rts") == "on" ? "asserted" : "dropped") + newline;
                if (rx && tx && status)
                    out += "  status " + Field(*status, "value") + "  rx " + Field(*rx, "count") + "/" + Field(*rx, "capacity") + (Field(*rx, "near_full") == "on" ? " (near full)" : "") +
                           "  tx " + Field(*tx, "count") + "/" + Field(*tx, "capacity") + (Field(*tx, "busy") == "on" ? " (shifting)" : "") + newline;
                if (rx && !Field(*rx, "preview_hex").empty())
                    out += "  rx bytes " + Field(*rx, "preview_hex") + newline;
                if (bytes)
                    out += "  sent " + Field(*bytes, "sent") + ", received " + Field(*bytes, "received") + ", lost " + Field(*bytes, "lost") + newline;
                if (irq)
                    out += "  NR #C6 bits " + Field(*irq, "nr_c6_bits") + ": rx " + (Field(*irq, "rx_enabled") == "on" ? "enabled" : "off") + ", tx " +
                           (Field(*irq, "tx_enabled") == "on" ? "enabled" : "off") + newline;
                if (peer)
                    out += "  line: " + (Field(*peer, "attached") == "on" ? Field(*peer, "kind") + " " + Field(*peer, "target") : std::string("nothing")) + newline;
            }
        return out;
    }
    if (sub == "input")
    {
        const StateNode report = DeviceState::NextInputReport(context);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out;
        if (const StateNode* modes = report.find("joystick_modes"))
        {
            const StateNode* l = modes->find("left");
            const StateNode* r = modes->find("right");
            if (l && r)
                out += "Joystick (NR #05 " + Field(*modes, "nr_05") + "): left " + Field(*l, "name") + " [" + Field(*l, "port") + "], right " +
                       Field(*r, "name") + " [" + Field(*r, "port") + "]" + newline;
        }
        if (const StateNode* io = report.find("io_mode"))
            out += "I/O mode (NR #0B " + Field(*io, "nr_0b") + "): " + (Field(*io, "enabled") == "on" ? Field(*io, "mode") : std::string("off")) + newline;
        if (const StateNode* k = report.find("kempston_1f"))
        {
            out += "Kempston #1F: " + Field(*k, "value") + (Field(*k, "selected_by_nr_05") == "on" ? "" : "  (NR #05 does not select it; this emulator still answers)") +
                   newline;
        }
        out += "Port #1F " + std::string(Field(*report.find("kempston_1f"), "answers_here") == "on" ? "answers" : "idle (#FF)") + ", port #37 " +
               (Field(*report.find("kempston_37"), "answers_here") == "on" ? "answers" : "idle (#FF)") + newline;
        if (const StateNode* keys = report.find("extended_keys"))
        {
            std::string held;
            if (const StateNode* pressed = keys->find("pressed"))
                for (const StateNode& k : pressed->items)
                    held += (held.empty() ? "" : ",") + k.s;
            out += "Extended keys (NR #B0 " + Field(*keys, "nr_b0") + ", #B1 " + Field(*keys, "nr_b1") + "): " + (held.empty() ? std::string("none") : held) + newline;
        }
        if (const StateNode* pad = report.find("md_pad_extra"))
            out += "Pad extras (NR #B2 " + Field(*pad, "nr_b2") + "): host pad keys " + Field(*pad, "host_keys") + newline;
        return out;
    }
    if (sub == "magicport" || sub == "magic-port")
    {
        // magicport on <port> [hex|dec|ascii|line] | off | clear | [since=N] [limit=N]
        if (args.size() >= 3 && (args[2] == "on" || args[2] == "off" || args[2] == "clear"))
        {
            int port = -1;
            std::string mode;
            if (args[2] == "on")
            {
                if (args.size() < 4)
                    return std::string("Error: magicport on <port> [hex|dec|ascii|line]") + newline;
                try
                {
                    const unsigned long value = std::stoul(args[3], nullptr, 0);
                    if (value > 0xFFFF)
                        return std::string("Error: the port is 0..65535") + newline;
                    port = static_cast<int>(value);
                }
                catch (const std::exception&)
                {
                    return "Error: bad port '" + args[3] + "' (0x00FF or decimal)" + std::string(newline);
                }
                if (args.size() > 4)
                    mode = args[4];
            }
            const StateNode r = DeviceState::NextMagicPortControl(context, args[2] == "clear" ? -1 : (args[2] == "on" ? 1 : 0), port, mode,
                                                                  args[2] == "clear");
            const std::string error = unavailable(r);
            if (!error.empty())
                return error;
            if (const StateNode* refused = r.find("error"))
                return "Error: " + refused->s + newline;
            return "Magic port " + Field(r, "enabled") + ", port " + Field(r, "port") + ", mode " + Field(r, "mode") + ", " + Field(r, "bytes") +
                   " byte(s)" + newline;
        }
        uint64_t since = 0;
        size_t limit = 40;
        try
        {
            for (size_t i = 2; i < args.size(); i++)
            {
                const size_t eq = args[i].find('=');
                const std::string key = eq == std::string::npos ? args[i] : args[i].substr(0, eq);
                if (eq == std::string::npos || (key != "since" && key != "limit"))
                    return "Error: expected on <port> [mode] | off | clear | since=N | limit=N, got '" + args[i] + "'" + newline;
                const uint64_t number = std::stoull(args[i].substr(eq + 1));
                if (key == "since")
                    since = number;
                else
                    limit = static_cast<size_t>(number);
            }
        }
        catch (const std::exception&)
        {
            return std::string("Error: since and limit are numbers") + newline;
        }
        const StateNode report = DeviceState::NextMagicPortReport(context, since, limit);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out = "Magic port " + Field(report, "enabled") + ", port " + Field(report, "port") + ", mode " + Field(report, "mode") + ", " +
                          Field(report, "bytes") + " byte(s)" + newline;
        if (Field(report, "enabled") != "on")
            out += std::string("  (off: state next magicport on <port> [hex|dec|ascii|line])") + newline;
        if (const StateNode* items = report.find("items"))
            for (const StateNode& item : items->items)
                out += "  #" + Field(item, "seq") + " f" + Field(item, "frame") + " pc " + Field(item, "pc") + "  " + Field(item, "text") + newline;
        if (report.find("partial") && !Field(report, "partial").empty())
            out += "  (not ended) " + Field(report, "partial") + newline;
        return out;
    }
    if (sub == "nmi" || sub == "nmi-journal")
    {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off" || args[2] == "clear"))
        {
            const StateNode r = DeviceState::NextNmiJournalControl(context, args[2] == "clear" ? -1 : (args[2] == "on" ? 1 : 0),
                                                                   args[2] == "clear");
            const std::string error = unavailable(r);
            if (!error.empty())
                return error;
            return "NMI journal " + Field(r, "enabled") + ", " + Field(r, "size") + " event(s) held, " + Field(r, "evicted") + " evicted" +
                   newline;
        }
        std::string kinds, since, from, to, limit;
        const char* keys = "kinds, since, from, to, limit";
        for (size_t i = 2; i < args.size(); i++)
        {
            const size_t eq = args[i].find('=');
            if (eq == std::string::npos)
                return "Error: expected key=value, got '" + args[i] + "' (" + keys + ")" + newline;
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
            else
                return "Error: unknown option '" + key + "' (" + keys + ")" + newline;
        }
        NextIntJournalQuery query;
        std::string error;
        if (!NextIntJournalQueryFromStrings(kinds, since, from, to, limit, query, error))
            return "Error: " + error + newline;
        const StateNode report = DeviceState::NextNmiReport(context, query);
        const std::string unavailableText = unavailable(report);
        if (!unavailableText.empty())
            return unavailableText;
        std::string out;
        if (const StateNode* sources = report.find("sources"))
            for (const StateNode& src : sources->items)
            {
                out += "NMI source " + Field(src, "name") + ": ";
                if (Field(src, "modelled") != "on")
                    out += "not modelled";
                else if (Field(src, "would_fire") == "on")
                    out += "ready";
                else
                    out += "blocked - " + Field(src, "blocked_by");
                out += newline;
            }
        if (const StateNode* service = report.find("in_service"))
            out += std::string("NMI in service: ") + (Field(*service, "any") == "on" ? "yes" : "no") + " (multiface session " +
                   Field(*service, "multiface_session") + ", divmmc handler " + Field(*service, "divmmc_handler") + ")" + newline;
        if (const StateNode* mf = report.find("multiface"))
            out += "Multiface " + Field(*mf, "enabled") + " type " + Field(*mf, "type_name") + ", ports " + Field(*mf, "enable_port") + " / " +
                   Field(*mf, "disable_port") + ", memory " + (Field(*mf, "memory_in") == "on" ? "in" : "out") + ", invisible " +
                   Field(*mf, "invisible") + newline;
        if (const StateNode* reset = report.find("reset"))
            out += "Last reset: " + Field(*reset, "last") + " (NR #02 bits 1:0 = " + Field(*reset, "bits") + ")" + newline;
        if (const StateNode* ints = report.find("interrupts"))
        {
            out += "Interrupts: " + Field(*ints, "mode");
            auto names = [&](const char* key) {
                std::string list;
                if (const StateNode* items = ints->find(key))
                    for (const StateNode& item : items->items)
                        list += (list.empty() ? "" : ",") + item.s;
                return list.empty() ? std::string("none") : list;
            };
            out += ", pending " + names("pending") + ", in service " + names("in_service") + newline;
        }
        const StateNode* events = report.find("events");
        out += "NMI journal " + Field(report, "enabled") + ": " + std::to_string(events ? events->items.size() : 0) + " event(s) of " +
               Field(report, "size") + " held, " + Field(report, "evicted") + " evicted" + newline;
        if (Field(report, "enabled") == "off")
            out += "  (off: `state next nmi on` first)" + std::string(newline);
        if (events)
            for (const StateNode& e : events->items)
            {
                out += "#" + Field(e, "seq") + " frame " + Field(e, "frame") + " T " + Field(e, "t") + " PC " + Field(e, "pc") + " " +
                       Field(e, "kind") + " " + Field(e, "source");
                if (!Field(e, "vector").empty())
                    out += " vector " + Field(e, "vector");
                if (Field(e, "kind") == "nmi_request")
                    out += Field(e, "accepted") == "on" ? " accepted" : " REFUSED: " + Field(e, "reason");
                if (!Field(e, "reset_last").empty())
                    out += " (" + Field(e, "reset_last") + ")";
                out += newline;
            }
        return out;
    }
    return "Error: unknown subcommand '" + args[1] + "'. Available: (none), regs, mmu, dma, video, palette [palette= range=], ports [port= access=], nextreg [reg= changed=], copper [from= count= raw=], sprites [from= count= all=], pixel <x> <y> [sx], layer-capture [on|off], journal [regs=.. sources=.. since=N from=F to=F limit=N | on | off | clear], sd [kinds= card= since= from= to= limit= | on | off | clear], divmmc [kinds= since= from= to= limit= | on | off | clear], nmi [kinds= since= from= to= limit= | on | off | clear], audio, input" +
           newline;
}

/// The CLI command `next ...` (args without the command word): `nextreg <reg> <value> [nextreg|port|internal]` writes a NextREG,
/// any
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
