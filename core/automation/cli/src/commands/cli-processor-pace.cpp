// pace [show | presets | set <preset> [ms] | interval <ms> | next | prev | start | stop | toggle | step] [--json]: the
// paced run of the selected instance (core execution::RunPacer, Stream Deck use case D1): the machine steps a frame,
// N scanlines or an instruction per interval and stops at a breakpoint. The same pacer as WebAPI /run_pace, MCP
// control_execution `pace`, the actions run.pace_*, Lua / Python pace_*() and the Qt debugger's speed control

#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cli-processor.h"
#include "emulator/emulator.h"
#include "emulator/execution/runpacer.h"
#include "emulator/state/statenodejson.h"
#include "platform-sockets.h"

namespace
{
const char* const kPaceUsage =
    "pace [show]               - The pace: preset, interval, running, why it stopped last\n"
    "pace presets              - The ladder: full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line, 1op\n"
    "pace set <preset> [ms]    - Choose a preset (and the interval of frame .. 1op, 1..5000 ms)\n"
    "pace interval <ms>        - The interval of frame .. 1op\n"
    "pace next | prev          - A finer / coarser preset\n"
    "pace start | stop | toggle - Run at the pace / stop it (the machine stays paused) / either\n"
    "pace step                 - One step at the pace\n"
    "Flags: --json (the reply every surface returns)\n";

std::string Describe(const StateNode& pace)
{
    auto text = [&](const char* key) {
        const StateNode* node = pace.find(key);
        return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
    };
    auto number = [&](const char* key) {
        const StateNode* node = pace.find(key);
        return node != nullptr && node->kind == StateNode::Kind::Int ? node->i : 0;
    };
    const StateNode* running = pace.find("running");
    std::ostringstream out;
    out << "Pace " << text("preset") << " (" << text("label") << "), "
        << (running != nullptr && running->b ? "running" : "not running");
    if (number("effective_ms") > 0)
        out << ", every " << number("effective_ms") << " ms";
    out << ", ticks " << number("ticks");
    if (!text("stop_reason").empty())
        out << ", last stop: " << text("stop_reason");
    return out.str();
}
}  // namespace

void CLIProcessor::HandlePace(const ClientSession& session, const std::vector<std::string>& args)
{
    bool json = false;
    std::vector<std::string> words;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else
            words.push_back(arg);
    }
    const std::string verb = words.empty() ? std::string("show") : words[0];
    auto usage = [&] {
        std::string text = kPaceUsage;
        std::string out;
        for (char c : text)
            out += c == '\n' ? std::string(NEWLINE) : std::string(1, c);
        session.SendResponse(out);
    };
    if (verb == "help" || verb == "--help")
    {
        usage();
        return;
    }
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (verb == "show" || verb == "presets")
    {
        const auto pacer = execution::RunPacer::Find(emulator->GetId());
        const StateNode pace = pacer ? pacer->StateBody(verb == "presets") : execution::RunPacer::DefaultStateBody(verb == "presets");
        if (json)
        {
            session.SendResponse(StateNodeToJsonText(pace) + NEWLINE);
            return;
        }
        std::ostringstream out;
        out << Describe(pace) << NEWLINE;
        if (const StateNode* presets = pace.find("presets"))
            for (const StateNode& preset : presets->items)
            {
                const StateNode* interval = preset.find("interval_ms");
                out << "  " << preset.find("index")->i << "  " << preset.find("id")->s << "  (" << preset.find("label")->s
                    << ")  " << preset.find("unit")->s;
                if (interval != nullptr && interval->kind == StateNode::Kind::Int)
                    out << ", every " << interval->i << " ms";
                else if (preset.find("adjustable")->b)
                    out << ", every interval";
                out << NEWLINE;
            }
        session.SendResponse(out.str());
        return;
    }
    if (RunControlHeld(session, emulator))
        return;
    const execution::Requester requester = CliRequester(emulator->GetContext());
    const std::shared_ptr<execution::RunPacer> pacer = execution::RunPacer::For(emulator);
    execution::RunReply reply;
    if (verb == "set" || verb == "interval")
    {
        std::optional<size_t> preset;
        std::optional<int> interval;
        size_t at = 1;
        if (verb == "set")
        {
            size_t index = 0;
            if (words.size() < 2 || !execution::RunPacer::PresetIndex(words[1], index))
            {
                session.SendResponse("Usage: pace set <full|25fps|5fps|1fps|frame|100lines|10lines|1line|1op> [ms]" +
                                     std::string(NEWLINE));
                return;
            }
            preset = index;
            at = 2;
        }
        if (words.size() > at)
        {
            char* end = nullptr;
            const long ms = std::strtol(words[at].c_str(), &end, 10);
            if (end == nullptr || *end != '\0')
            {
                session.SendResponse("The interval is a number of ms: 1..5000" + std::string(NEWLINE));
                return;
            }
            interval = static_cast<int>(ms);
        }
        if (!preset && !interval)
        {
            session.SendResponse("Usage: pace interval <ms>" + std::string(NEWLINE));
            return;
        }
        reply = pacer->Set(requester, preset, interval);
    }
    else if (verb == "next" || verb == "prev")
        reply = pacer->Shift(requester, verb == "next" ? 1 : -1);
    else if (verb == "start")
        reply = pacer->Start(requester);
    else if (verb == "stop")
        reply = pacer->Stop();
    else if (verb == "toggle")
        reply = pacer->Toggle(requester);
    else if (verb == "step")
        reply = pacer->Step(requester);
    else
    {
        usage();
        return;
    }
    if (json)
    {
        StateNode body = reply.body;
        if (!reply.Ok())
        {
            body["error"] = reply.error;
            body["message"] = reply.message;
        }
        session.SendResponse(StateNodeToJsonText(body) + NEWLINE);
        return;
    }
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode* pace = reply.body.find("pace");
    session.SendResponse(reply.message + (pace != nullptr ? ". " + Describe(*pace) : std::string()) + NEWLINE);
}
