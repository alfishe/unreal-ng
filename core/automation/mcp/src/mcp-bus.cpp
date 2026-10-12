#include "mcp-bus.h"

#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

namespace
{
const char* const kActions[] = {"list", "enable", "disable", "clear", "events", "findings", "sd_timing", "export"};

std::string BusLine(const Json::Value& bus)
{
    std::ostringstream out;
    out << bus["id"].asString() << " (" << bus["protocol"].asString() << "): " << bus["label"].asString() << "\n";
    const Json::Value& trace = bus["trace"];
    const Json::Value& checks = bus["checks"];
    out << "  trace " << (trace["enabled"].asBool() ? (trace["protocol_only"].asBool() ? "on (protocol only)" : "on") : "off") << " size=" << trace["size"].asUInt64() << "/"
        << trace["capacity"].asUInt64() << " evicted=" << trace["evicted"].asUInt64() << " rewound=" << trace["rewound"].asUInt64()
        << " last_seq=" << trace["last_seq"].asUInt64() << "; checks " << (checks["enabled"].asBool() ? "on" : "off")
        << " findings=" << checks["findings"].asUInt64();
    if (checks["by_rule"].isObject() && !checks["by_rule"].empty())
    {
        out << " (";
        bool first = true;
        for (const std::string& rule : checks["by_rule"].getMemberNames())
        {
            out << (first ? "" : ", ") << rule << "=" << checks["by_rule"][rule].asUInt64();
            first = false;
        }
        out << ")";
    }
    out << "; byte_time_ns=" << bus["byte_time_ns"].asDouble();
    for (const Json::Value& dev : bus["devices"])
    {
        out << "\n  slot " << dev["slot"].asInt() << ": " << dev["name"].asString();
        if (dev["sd"].isObject())
        {
            const Json::Value& sd = dev["sd"];
            out << " sd present=" << (sd["present"].asBool() ? "yes" : "no");
            if (sd["present"].asBool())
                out << " type=" << sd["type"].asString() << " state=" << sd["state"].asString();
            out << " timing=" << sd["timing"].asString() << " seed=" << sd["seed"].asUInt64();
            if (sd["timing_pending"].asBool())
                out << " (pending: applied at the card's next access)";
            out << " blocks_read=" << sd["blocks_read"].asUInt64() << " blocks_written=" << sd["blocks_written"].asUInt64();
        }
    }
    return out.str();
}

std::string EventLine(const Json::Value& e)
{
    std::ostringstream out;
    out << "#" << e["seq"].asUInt64() << " f" << e["frame"].asInt64() << " t" << e["t"].asInt64() << " pc=" << e["pc"].asString() << " "
        << e["source"].asString() << " slot=" << (e["slot"].isString() ? e["slot"].asString() : std::to_string(e["slot"].asInt())) << " "
        << e["kind"].asString();
    const std::string kind = e["kind"].asString();
    if (kind == "byte")
        out << " mosi=" << e["mosi"].asString() << " miso=" << e["miso"].asString() << (e["read"].asBool() ? " (read)" : "");
    else if (kind == "early")
        out << (e["write"].asBool() ? " write " + e["value"].asString() : std::string(" read"));
    else if (kind == "command")
        out << " " << e["command"].asString() << " arg=" << e["argument"].asString() << " r1=" << e["r1"].asString()
            << (e["crc_valid"].asBool() ? "" : " crc=bad");
    else if (kind == "read")
        out << " block=" << e["block"].asUInt64();
    else if (kind == "write")
        out << " block=" << e["block"].asUInt64() << (e["ok"].asBool() ? " stored" : " refused");
    else if (kind == "reg_read" || kind == "reg_write")
        out << " " << e["register"].asString() << "=" << e["value"].asString();
    else if (kind == "ata_command")
        out << " " << e["command"].asString();
    else if (kind == "data")
        out << " word=" << e["word"].asString() << (e["write"].asBool() ? " (write)" : "");
    return out.str();
}

std::string FindingLine(const Json::Value& f)
{
    std::ostringstream out;
    out << "#" << f["seq"].asUInt64() << " " << f["severity"].asString() << " " << f["rule"].asString() << " f" << f["frame"].asInt64()
        << " t" << f["t"].asInt64() << " pc=" << f["pc"].asString() << " " << f["source"].asString()
        << " slot=" << (f["slot"].isString() ? f["slot"].asString() : std::to_string(f["slot"].asInt())) << " value=" << f["value"].asString()
        << " event=#" << f["event_seq"].asUInt64() << " - " << f["detail"].asString();
    return out.str();
}

/// Query string from the tool's arguments
std::string Query(const Json::Value& args, std::initializer_list<const char*> names)
{
    std::string query;
    for (const char* name : names)
    {
        if (!args.isMember(name))
            continue;
        const Json::Value& v = args[name];
        const std::string text = v.isString() ? v.asString() : v.isBool() ? (v.asBool() ? "1" : "0") : v.asString();
        query += (query.empty() ? "?" : "&") + std::string(name) + "=" + UrlEncodeSegment(text);
    }
    return query;
}
}  // namespace

std::string BusAnalyzerText(const std::string& action, const Json::Value& reply)
{
    std::ostringstream out;
    if (action == "list" || (reply.isMember("buses") && !reply.isMember("id")))
    {
        if (reply["buses"].empty())
            return "No buses on this machine (none of its devices has a bus the analyzer taps)";
        bool first = true;
        for (const Json::Value& bus : reply["buses"])
        {
            out << (first ? "" : "\n") << BusLine(bus);
            first = false;
        }
        return out.str();
    }
    if (action == "export")
    {
        out << "exported " << reply["bus"].asString() << " as " << reply["format"].asString() << " to " << reply["path"].asString() << " ("
            << reply["bytes"].asUInt64() << " bytes)";
        return out.str();
    }
    out << BusLine(reply);
    if (action == "events")
    {
        const Json::Value& events = reply["events"];
        out << "\n" << events.size() << " event(s)";
        if (!reply["trace"]["enabled"].asBool())
            out << " - tracing is off: bus_analyzer action=enable";
        for (const Json::Value& e : events)
            out << "\n" << EventLine(e);
    }
    else if (action == "findings")
    {
        const Json::Value& findings = reply["findings"];
        out << "\n" << findings.size() << " finding(s)";
        if (!reply["checks"]["enabled"].asBool())
            out << " - checks are off: bus_analyzer action=enable checks=true";
        for (const Json::Value& f : findings)
            out << "\n" << FindingLine(f);
    }
    else if (action == "sd_timing")
        out << "\nslot " << reply["slot"].asInt();
    return out.str();
}

void RegisterBusAnalyzer(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["action"]["type"] = "string";
    for (const char* action : kActions)
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["bus"]["type"] = "string";
    schema["properties"]["bus"]["description"] =
        "Bus id from list: zc.spi (TS-Conf, ZX-Evo), next.spi, divmmc.spi, neogs.sd, neogs.mp3.sci, neogs.mp3.sdi, ide0 / ide1 (ATA); enable / "
        "disable / clear also take 'all' (the default for them)";
    schema["properties"]["checks"]["type"] = "boolean";
    schema["properties"]["checks"]["description"] = "enable: also run the checks (default true)";
    schema["properties"]["capacity"]["type"] = "integer";
    schema["properties"]["capacity"]["description"] = "enable: events kept (default 65536)";
    schema["properties"]["protocol_only"]["type"] = "boolean";
    schema["properties"]["protocol_only"]["description"] =
        "enable: leave out single bytes, data words and status reads (selects, SD / ATA commands, blocks and findings stay): a "
        "whole boot fits the ring";
    schema["properties"]["kinds"]["type"] = "string";
    schema["properties"]["kinds"]["description"] =
        "events: comma list of select, deselect, byte, early, truncated, command, read, write, reg_read, reg_write, ata_command, "
        "data; groups spi, sd, ata";
    schema["properties"]["rules"]["type"] = "string";
    schema["properties"]["rules"]["description"] =
        "findings: comma list of early_access, select_mid_byte, deselect_mid_command, command_while_busy, command_drops_answer, "
        "crc_error, dma_collision, ata_data_without_drq, ata_command_drops_data, ata_write_while_busy";
    schema["properties"]["slot"]["type"] = "integer";
    schema["properties"]["slot"]["description"] = "events: one device slot; sd_timing: the card's slot (default the first)";
    schema["properties"]["since"]["type"] = "integer";
    schema["properties"]["since"]["description"] = "events / findings: only after this seq (incremental reads)";
    schema["properties"]["from"]["type"] = "integer";
    schema["properties"]["to"]["type"] = "integer";
    schema["properties"]["from"]["description"] = "events: first frame";
    schema["properties"]["to"]["description"] = "events: last frame";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["description"] = "events / findings: the newest N (default 60; 0 = all)";
    schema["properties"]["profile"]["type"] = "string";
    schema["properties"]["profile"]["enum"].append("ideal");
    schema["properties"]["profile"]["enum"].append("realistic");
    schema["properties"]["profile"]["enum"].append("slow");
    schema["properties"]["profile"]["description"] =
        "sd_timing: ideal (instant, default), realistic (typical card delays, random by seed), slow (the SD specification's "
        "worst case: 100 ms reads, 250 ms writes, 1 s initialization); left out: report only";
    schema["properties"]["seed"]["type"] = "integer";
    schema["properties"]["seed"]["description"] = "sd_timing: the realistic profile's seed";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["enum"].append("vcd");
    schema["properties"]["format"]["enum"].append("jsonl");
    schema["properties"]["format"]["description"] = "export: vcd (SPI wires for GTKWave / PulseView, default) or jsonl";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "export: the file on the emulator host (use scratch/)";
    schema["required"].append("action");

    registry.Register(
        "bus_analyzer",
        "Peripheral bus analyzer: the SPI buses of the machine and its cards (TS-Conf / ZX-Evo Z-Controller zc.spi with the SD "
        "card and the VDAC2 FT812, Next next.spi, DivMMC divmmc.spi, NeoGS neogs.sd and the MP3 decoder) and the IDE channels "
        "(ide0 / ide1: ATA registers, commands, data words; checks ata_data_without_drq, ata_command_drops_data, "
        "ata_write_while_busy). Records transactions "
        "(chip select, byte MOSI / MISO, early access, truncated byte) and SD protocol events (command + argument + R1, block "
        "read / written) with frame, T, PC and source (cpu, dma, card_cpu, card_dma), and checks the driver: early_access, "
        "select_mid_byte, deselect_mid_command, command_while_busy, command_drops_answer, crc_error, dma_collision (CPU on the "
        "ports during a TS-Conf SPI DMA). Off until enabled; observing changes nothing. sd_timing makes the SD card answer "
        "with realistic or worst-case delays to harden a driver. Actions: list, enable, disable, clear, events, findings, "
        "sd_timing, export.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            bool known = false;
            for (const char* a : kActions)
                known = known || action == a;
            if (!known)
            {
                done(ToolResult::Error("action: '" + action +
                                       "' is not one of list, enable, disable, clear, events, findings, sd_timing, export"));
                return;
            }
            const bool control = action == "enable" || action == "disable" || action == "clear";
            const std::string bus = args.isMember("bus") ? args["bus"].asString() : (control ? "all" : "");
            if (bus.empty() && action != "list")
            {
                done(ToolResult::Error("bus: required for " + action + " (bus_analyzer action=list shows the ids)"));
                return;
            }
            if (action == "export" && !args.isMember("path"))
            {
                done(ToolResult::Error("path: required for export (a file on the emulator host, e.g. scratch/bus.vcd)"));
                return;
            }
            auto argsCopy = std::make_shared<Json::Value>(args);
            TargetResolver::ResolveFromArgs(args, caller, [action, bus, argsCopy, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const Json::Value& a = *argsCopy;
                const std::string& id = idOrError;
                const std::string base = "/bus/" + UrlEncodeSegment(bus);
                std::string method = "GET";
                std::string path;
                auto body = std::make_shared<Json::Value>(Json::objectValue);
                const Json::Value* payload = nullptr;
                if (action == "list")
                    path = Endpoint(id, "/bus");
                else if (action == "enable" || action == "disable" || action == "clear")
                {
                    method = "POST";
                    path = Endpoint(id, base + "/trace-journal");
                    if (action == "enable")
                    {
                        (*body)["enabled"] = true;
                        (*body)["checks"] = a.get("checks", true).asBool();
                        if (a.isMember("capacity"))
                            (*body)["capacity"] = a["capacity"];
                        if (a.isMember("protocol_only"))
                            (*body)["protocol_only"] = a["protocol_only"];
                    }
                    else if (action == "disable")
                    {
                        (*body)["enabled"] = false;
                        (*body)["checks"] = false;
                    }
                    else
                        (*body)["clear"] = true;
                    payload = body.get();
                }
                else if (action == "events")
                {
                    Json::Value q = a;
                    if (!q.isMember("limit"))
                        q["limit"] = 60;
                    path = Endpoint(id, base + "/events" + Query(q, {"kinds", "slot", "since", "from", "to", "limit"}));
                }
                else if (action == "findings")
                {
                    Json::Value q = a;
                    if (!q.isMember("limit"))
                        q["limit"] = 60;
                    path = Endpoint(id, base + "/findings" + Query(q, {"rules", "since", "limit"}));
                }
                else if (action == "sd_timing")
                {
                    if (a.isMember("profile"))
                    {
                        method = "POST";
                        for (const char* key : {"slot", "profile", "seed"})
                            if (a.isMember(key))
                                (*body)[key] = a[key];
                        payload = body.get();
                        path = Endpoint(id, base + "/sd-timing");
                    }
                    else
                        path = Endpoint(id, base + "/sd-timing" + Query(a, {"slot"}));
                }
                else  // export
                {
                    Json::Value q = a;
                    if (!q.isMember("format"))
                        q["format"] = "vcd";
                    path = Endpoint(id, base + "/export" + Query(q, {"format", "path", "kinds", "slot", "since", "from", "to", "limit"}));
                }

                caller.Call(method, path, payload, [action, body, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300)
                    {
                        done(ToolResult::Error("bus_analyzer " + action + ": " + reply.get("message", DescribeErrorBody(reply)).asString()));
                        return;
                    }
                    // the text first: argument order is unspecified, and gcc moves `reply` out before reading it
                    const std::string text = BusAnalyzerText(action, reply);
                    done(ToolResult::Ok(text, std::move(reply)));
                });
            });
        });
}

} // namespace mcp
