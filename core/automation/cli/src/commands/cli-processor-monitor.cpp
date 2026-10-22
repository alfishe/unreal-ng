// monitor [name] [instance=<id|index>] [--json]: the host process, the clients, each instance's pace and the
// summaries a monitoring page shows - the verbs of monitoring::Call, the same as WebAPI /api/v1/monitor[/{name}],
// MCP inspect_state aspect `monitor`, Lua / Python monitor(); the WebSocket topics of the same names push them

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "cli-processor.h"
#include "emulator/monitoring/monitoringapi.h"
#include "emulator/state/statenodejson.h"

namespace
{
const char* const kMonitorUsage =
    "monitor [all]                          - Everything: host, clients, every instance's values\n"
    "monitor host.stats                     - The process: CPU %, resident / peak memory, threads, uptime\n"
    "monitor host.clients                   - Connections and requests per plane (rest, ws, events, mcp, cli)\n"
    "monitor emulator.stats [instance=]     - fps, real-time speed %, frame time, CPU MHz, interrupts / frame\n"
    "monitor debug.breakpoints [instance=]  - Breakpoints: total, active, by kind, groups, last hit\n"
    "monitor media.slots | recording.state | debug.labels | ttd.status | debug.analyzers | controllers.state\n"
    "Flags: --json (the reply every surface returns); instance= an id or an index (default the followed one)\n";

std::string Lines(const std::string& text, const char* newline)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\n')
            out += newline;
        else
            out += c;
    }
    return out;
}

std::string Str(const StateNode* node)
{
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return "-";
    if (node->kind == StateNode::Kind::String)
        return node->s.empty() ? "-" : node->s;
    if (node->kind == StateNode::Kind::Int)
        return std::to_string(node->i);
    if (node->kind == StateNode::Kind::Bool)
        return node->b ? "yes" : "no";
    if (node->kind == StateNode::Kind::Double)
    {
        std::ostringstream out;
        out.precision(4);
        out << node->d;
        return out.str();
    }
    return StateNodeToJsonText(*node);
}

std::string Mb(const StateNode* node)
{
    if (node == nullptr || node->kind != StateNode::Kind::Int)
        return "-";
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << static_cast<double>(node->i) / (1024.0 * 1024.0) << " MB";
    return out.str();
}

void HostText(std::ostringstream& out, const StateNode& host, const char* nl)
{
    out << "Host: CPU " << Str(host.find("cpu_percent")) << " % (" << Str(host.find("cpu_percent_host")) << " % of "
        << Str(host.find("cores")) << " cores), RSS " << Mb(host.find("rss_bytes")) << " (peak "
        << Mb(host.find("peak_rss_bytes")) << "), threads " << Str(host.find("threads")) << ", up "
        << Str(host.find("uptime_s")) << " s" << nl;
}

void ClientsText(std::ostringstream& out, const StateNode& clients, const char* nl)
{
    out << "Clients:";
    for (const char* plane : {"rest", "ws", "events", "debug_ws", "mcp", "cli"})
        if (const StateNode* counts = clients.find(plane))
            out << " " << plane << " " << Str(counts->find("connections")) << "/" << Str(counts->find("requests"));
    out << " (open / requests), companions " << Str(clients.find("companions")) << ", instances "
        << Str(clients.find("instances")) << nl;
}

void StatsText(std::ostringstream& out, const StateNode& stats, const char* nl)
{
    const StateNode* frame = stats.find("frame_time_us");
    const StateNode* cpu = stats.find("cpu");
    out << "  " << Str(stats.find("state")) << ", " << Str(stats.find("fps")) << " fps of "
        << Str(stats.find("nominal_fps")) << ", speed " << Str(stats.find("speed_percent")) << " %, frame "
        << (frame ? Str(frame->find("avg")) : "-") << " us (max " << (frame ? Str(frame->find("max")) : "-")
        << "), budget " << Str(stats.find("frame_budget_percent")) << " %, halted "
        << Str(stats.find("halted_percent")) << " %" << nl;
    if (cpu != nullptr)
        out << "  CPU " << Str(cpu->find("mhz")) << " MHz (base " << Str(cpu->find("base_mhz")) << ", x"
            << Str(cpu->find("multiplier")) << ": host x" << Str(cpu->find("host_multiplier")) << ", hardware x"
            << Str(cpu->find("hw_turbo_ratio")) << "), " << Str(stats.find("interrupts_per_frame"))
            << " interrupts / frame, frame " << Str(stats.find("frame")) << ", t-states " << Str(stats.find("t_states"))
            << nl;
}

std::string Text(const std::string& verb, StateNode body, const char* nl)
{
    std::ostringstream out;
    if (verb == "all")
    {
        if (const StateNode* host = body.find("host"))
            HostText(out, *host, nl);
        if (const StateNode* clients = body.find("clients"))
            ClientsText(out, *clients, nl);
        if (const StateNode* instances = body.find("instances"))
            for (const StateNode& instance : instances->items)
            {
                out << "Instance " << Str(instance.find("index")) << " " << Str(instance.find("model")) << " "
                    << Str(instance.find("instance")) << nl;
                if (const StateNode* stats = instance.find("emulator.stats"))
                    StatsText(out, *stats, nl);
                if (const StateNode* bps = instance.find("debug.breakpoints"))
                    out << "  breakpoints " << Str(bps->find("total")) << " (" << Str(bps->find("active"))
                        << " active)" << nl;
            }
        return out.str();
    }
    if (verb == "host.stats")
    {
        HostText(out, body, nl);
        return out.str();
    }
    if (verb == "host.clients")
    {
        ClientsText(out, body, nl);
        return out.str();
    }
    if (verb == "emulator.stats")
    {
        out << "Instance " << Str(body.find("instance")) << nl;
        StatsText(out, body, nl);
        return out.str();
    }
    auto& members = body.members;
    members.erase(std::remove_if(members.begin(), members.end(), [](const auto& m) { return m.first == "ok"; }),
                  members.end());
    return StateNodeToJsonText(body) + nl;
}
}  // namespace

void CLIProcessor::HandleMonitor(const ClientSession& session, const std::vector<std::string>& args)
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
    const std::string verb = words.empty() ? "all" : words[0];
    if (verb == "help" || verb == "-h" || verb == "--help")
    {
        session.SendResponse(Lines(kMonitorUsage, NEWLINE));
        return;
    }
    StateNode request = StateNode::Object();
    for (size_t i = 1; i < words.size(); i++)
    {
        const size_t equals = words[i].find('=');
        if (equals != std::string::npos && equals > 0)
            request[words[i].substr(0, equals)] = words[i].substr(equals + 1);
        else if (request.find("instance") == nullptr)
            request["instance"] = words[i];
        else
        {
            session.SendResponse("Error: Bad Request - unexpected '" + words[i] + "'" + NEWLINE +
                                 Lines(kMonitorUsage, NEWLINE));
            return;
        }
    }
    const monitoring::Reply reply = monitoring::Call(verb, request);
    if (json)
    {
        session.SendResponse(StateNodeToJsonText(reply.body) + NEWLINE);
        return;
    }
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + Str(reply.body.find("error")) + " - " + Str(reply.body.find("message")) +
                             NEWLINE);
        return;
    }
    session.SendResponse(Text(verb, reply.body, NEWLINE));
}
