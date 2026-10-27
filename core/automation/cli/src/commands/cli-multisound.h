#pragma once

/// @file cli-multisound.h
/// @brief CLI `multisound` and `midi` (ZX-MultiSound tdd-integration.md §5): the card's report (DeviceState::MultiSound)
/// and the MIDI side (DeviceState::Midi) as text, `midi panic` through MidiControl - the same trees and action as the
/// WebAPI, MCP, Lua and Python. Header-only so core-tests drive it without a CLI socket.
///
///   multisound [--full | --json]      the card: slot, options, CPLD latches, YM pair, SAA, GS, DACs, MIDI summary
///   midi [--json]                     the line, the bank and the 16 parts with the notes sounding
///   midi panic                        every voice stops (a TTD live input)
///
/// Worked example: `midi` on a card playing a tune prints `ch 1  prog   1 Acoustic Grand Piano  vol 100 ... C4 E4 G4`.

#include <cstdio>
#include <string>

#include "emulator/state/devicestate.h"

namespace CliMultiSound
{

inline std::string Text(const StateNode* node)
{
    return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
}

inline long long Int(const StateNode* node)
{
    return node != nullptr ? static_cast<long long>(node->kind == StateNode::Kind::Bool ? node->b : node->i) : 0;
}

inline bool Bool(const StateNode* node)
{
    return node != nullptr && node->kind == StateNode::Kind::Bool && node->b;
}

/// `multisound`: the card in a few lines ("\n" line ends)
inline std::string Summary(const StateNode& report)
{
    if (!Bool(report.find("available")))
        return "multisound: " + Text(report.find("description")) + "\n";
    std::string out = Text(report.find("card")) + " in " + Text(report.find("slot")) + ", fit " +
                      Text(report.find("fit")) + "\n";
    const StateNode* options = report.find("options");
    out += "  options: " + Text(options->find("text")) + "\n";
    if (const StateNode* note = options->find("ctrl_mask_note"))
        out += "  ctrlMask: " + note->s + "\n";
    if (const StateNode* shadowed = report.find("shadowed_devices"))
    {
        for (const StateNode& device : shadowed->items)
            out += "  built-in " + Text(device.find("id")) + ": " + Text(device.find("state")) + "\n";
    }
    const StateNode* logic = report.find("logic");
    char line[256];
    std::snprintf(line, sizeof line, "  CPLD: YM chip %lld%s, FM %s, SAA clock %s, ROM lock %s, GS page #%02llX\n",
                  Int(logic->find("ym_chip")), Bool(logic->find("ym_read_status")) ? " (status read)" : "",
                  Bool(logic->find("fm_muted")) ? "muted" : "on", Bool(logic->find("saa_clock")) ? "on" : "off",
                  Bool(logic->find("rom_lock")) ? "on" : "off", Int(logic->find("gs_page")));
    out += line;
    for (const StateNode& chip : report.find("ym")->find("chips")->items)
    {
        const StateNode* fm = chip.find("fm");
        std::snprintf(line, sizeof line, "  YM2203 %lld (%s): FM keyed %lld, sounding %lld\n", Int(chip.find("index")),
                      Text(chip.find("part")).c_str(), Int(fm->find("keyed_channels")), Int(fm->find("sounding_channels")));
        out += line;
    }
    const StateNode* saa = report.find("saa");
    int voices = 0;
    for (const StateNode& voice : saa->find("voices")->items)
        voices += Bool(voice.find("tone_enabled")) || Bool(voice.find("noise_enabled")) ? 1 : 0;
    std::snprintf(line, sizeof line, "  SAA1099: sound %s, clock %s, %d voice(s) on\n",
                  Bool(saa->find("sound_enabled")) ? "on" : "off", Bool(saa->find("clock_enabled")) ? "on" : "off", voices);
    out += line;
    const StateNode* gs = report.find("gs");
    std::snprintf(line, sizeof line, "  GS: %s, %lld KB, firmware %s, PC #%04llX\n", Text(gs->find("device")).c_str(),
                  Int(gs->find("ram_kb")), Bool(gs->find("firmware_ready")) ? "ready" : "booting",
                  Int(gs->find("cpu")->find("pc")));
    out += line;
    out += "  DAC:";
    for (const StateNode& dac : report.find("dac")->items)
    {
        std::snprintf(line, sizeof line, " %lld=%02llX/%lld", Int(dac.find("channel")), Int(dac.find("sample")),
                      Int(dac.find("volume")));
        out += line;
    }
    out += "\n";
    const StateNode* midi = report.find("midi");
    std::snprintf(line, sizeof line, "  MIDI: bank %s, %lld byte(s), %lld framing error(s), %lld voice(s)\n",
                  Text(midi->find("bank")->find("status")).c_str(), Int(midi->find("bytes_received")),
                  Int(midi->find("framing_errors")), Int(midi->find("active_voices")));
    out += line;
    return out;
}

/// `midi`: the line, the bank and one line per part that has a program or sounds
inline std::string MidiText(const StateNode& report)
{
    if (!Bool(report.find("available")))
        return "midi: " + Text(report.find("description")) + "\n";
    char line[512];
    const StateNode* lineNode = report.find("line");
    const StateNode* bank = report.find("bank");
    std::string out = Text(report.find("synthesizer")) + " in " + Text(report.find("slot")) + ", bank " +
                      Text(bank->find("status")) + (Text(bank->find("name")).empty() ? "" : " (" + Text(bank->find("name")) + ")") + "\n";
    std::snprintf(line, sizeof line, "  line: %s, level %lld, %lld edge(s)\n", Text(lineNode->find("source")).c_str(),
                  Int(lineNode->find("level")), Int(lineNode->find("edges")));
    out += line;
    const StateNode* counters = report.find("counters");
    std::snprintf(line, sizeof line, "  voices %lld / %lld (fading %lld); bytes %lld, framing errors %lld, stolen %lld\n",
                  Int(report.find("active_voices")), Int(report.find("polyphony_limit")), Int(report.find("fading_voices")),
                  Int(counters->find("bytes_received")), Int(counters->find("framing_errors")),
                  Int(counters->find("voices_stolen")));
    out += line;
    for (const StateNode& part : report.find("parts")->items)
    {
        std::string notes;
        for (const StateNode& note : part.find("notes")->items)
            notes += " " + note.s;
        std::snprintf(line, sizeof line, "  ch %2lld  prog %3lld %-24s vol %3lld pan %3lld voices %2lld%s%s\n",
                      Int(part.find("channel")), Int(part.find("program")), Text(part.find("preset")).c_str(),
                      Int(part.find("volume")), Int(part.find("pan")), Int(part.find("active_voices")),
                      Bool(part.find("muted")) ? " muted" : "", notes.c_str());
        out += line;
    }
    return out;
}

} // namespace CliMultiSound
