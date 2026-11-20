#pragma once

/// @file cli-ttd-cards.h
/// @brief CLI time travel on a sound card's CPU (gs-debugger design §8): the `--cpu` flag of `ttd seek`, `ttd si-back`
/// / `si-forward`, `ttd reverse-step`, `ttd reverse-step-over` and `ttd reverse-continue`, and the text of a card move
/// and of a card's readout (`ttd position`). Header-only so core-tests drive it without a CLI socket.
///
///   ttd seek --cpu gs 123456           the card at its instruction count 123456
///   ttd si-back --cpu gs --count 5     5 card instructions back (the main CPU follows by replay)
///   ttd reverse-continue --cpu ms      back to the MultiSound GS's previous breakpoint hit
///   ttd si-forward --cpu gs --over     the card's step over through the history (--out: its step out)
///
/// Worked example: `ttd si-back --cpu gs --count 5` prints
/// `TTD: gs at instruction 1200 (from 1205)` and
/// `  Card gs: instruction 1200, PC #0A3C (card position; main CPU at frame 12 t 30411, PC #8009, +7 T inside its
/// instruction)`.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcontrol.h"
#include "emulator/state/statenode.h"

namespace CliTTDCards
{

/// `--cpu <name>` among @p args (taken out of them; args[0] is the subcommand): the name, empty when absent or "main"
inline std::string TakeCpuFlag(std::vector<std::string>& args)
{
    std::string cpu;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--cpu" && i + 1 < args.size())
        {
            cpu = args[i + 1];
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            break;
        }
    }
    std::string lower = cpu;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "main" ? std::string() : cpu;
}

inline long long Int(const StateNode* node)
{
    return node ? static_cast<long long>(node->kind == StateNode::Kind::Bool ? node->b : node->i) : 0;
}

inline bool Bool(const StateNode* node)
{
    return node && node->kind == StateNode::Kind::Bool && node->b;
}

inline std::string Text(const StateNode* node)
{
    return node && node->kind == StateNode::Kind::String ? node->s : std::string();
}

inline std::string Hex4(long long value)
{
    std::stringstream ss;
    ss << '#' << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << value;
    return ss.str();
}

/// One card CPU's readout (the position verb's cards[] entry, a card move's card)
inline std::string FormatCardLine(const StateNode& card, const char* newline)
{
    std::stringstream ss;
    ss << "  Card " << Text(card.find("cpu")) << ": ";
    if (!Bool(card.find("known")))
    {
        ss << "no instruction count (a recording made before them)" << newline;
        return ss.str();
    }
    ss << "instruction " << Int(card.find("instruction")) << ", PC " << Hex4(Int(card.find("pc")));
    const StateNode* main = card.find("main");
    if (Bool(card.find("card_position")) && main)
    {
        ss << " (card position; main CPU at frame " << Int(main->find("frame")) << " t " << Int(main->find("tinframe"))
           << ", PC " << Hex4(Int(main->find("pc")));
        if (Bool(main->find("inside")))
            ss << ", +" << Int(main->find("t_offset")) << " T inside its instruction";
        ss << ")";
    }
    else
        ss << " (as the card last ran)";
    ss << newline;
    return ss.str();
}

/// The reply of a move on a card CPU (seek / step-instruction (kind over / out too) / reverse-step / reverse-step-over
/// / reverse-continue with cpu)
inline std::string FormatCardMove(const ttd::TTDReply& reply, const char* newline)
{
    if (!reply.Ok())
        return "Error: " + reply.message + newline;
    const StateNode& b = reply.body;
    const std::string cpu = Text(b.find("cpu"));
    std::stringstream ss;
    if (Bool(b.find("reached")))
    {
        ss << "TTD: " << cpu << " at instruction " << Int(b.find("target")) << " (from " << Int(b.find("from")) << ")";
        if (Bool(b.find("over_call")))
            ss << ", stepped back over a routine of " << Int(b.find("skipped")) << " instruction(s)";
        else if (Bool(b.find("call_out_of_reach")))
            ss << ", on the return: the routine's start is out of reach";
        if (Bool(b.find("history_end")))
            ss << ", the step did not end in the history: at its end (the next step runs on live)";
        else if (Bool(b.find("truncated")) && b.find("kind"))
            ss << ", the step did not end in the frames searched (step again to go on)";
        if (const StateNode* hit = b.find("breakpoint"))
            ss << ", breakpoint " << Int(hit->find("id")) << " (" << Text(hit->find("kind")) << " "
               << Hex4(Int(hit->find("address"))) << ")";
        ss << newline;
    }
    else
        ss << "TTD: " << cpu << " did not move (" << Text(b.find("status")) << "): " << Text(b.find("message")) << newline;
    if (const StateNode* searched = b.find("searched_frames"))
        ss << "  Searched " << Int(searched) << " frame(s)"
           << (Bool(b.find("truncated")) && !b.find("kind") ? " (max_frames reached)" : "")
           << newline;
    if (const StateNode* card = b.find("card"))
        ss << FormatCardLine(*card, newline);
    return ss.str();
}

}  // namespace CliTTDCards
