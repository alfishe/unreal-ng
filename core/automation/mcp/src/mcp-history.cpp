#include "mcp-history.h"

#include <memory>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

namespace
{
const char* const kActions[] = {"list", "read", "config", "clear", "pause", "resume"};

std::string StatsLine(const Json::Value& h)
{
    std::ostringstream out;
    out << h["device"].asString() << " (" << h["what"].asString() << "): " << h["mode"].asString();
    if (h["mode"].asString() != "off")
        out << " limit " << h["limit"].asUInt64();
    if (h["paused"].asBool())
        out << " (paused)";
    out << ", written " << h["written"].asUInt64() << ", kept " << h["kept"].asUInt64() << ", dropped "
        << h["dropped"].asUInt64();
    if (h["stream"].isObject())
    {
        const Json::Value& s = h["stream"];
        out << "; stream " << s["path"].asString() << " (" << s["written"].asUInt64() << " written";
        if (s["limit"].asUInt64() != 0)
            out << " of " << s["limit"].asUInt64();
        if (!s["error"].asString().empty())
            out << ", " << s["error"].asString();
        out << ")";
    }
    return out.str();
}
}  // namespace

std::string DeviceHistoryText(const std::string& action, const Json::Value& reply)
{
    std::ostringstream out;
    if (action == "list")
    {
        if (reply["histories"].empty())
            return "No device histories on this machine";
        bool first = true;
        for (const Json::Value& h : reply["histories"])
        {
            out << (first ? "" : "\n") << StatsLine(h);
            first = false;
        }
        return out.str();
    }
    out << StatsLine(reply);
    // Each record on one line: its fields as the device reports them
    for (const Json::Value& record : reply["records"])
    {
        out << "\n ";
        for (const std::string& name : record.getMemberNames())
        {
            const Json::Value& v = record[name];
            out << " " << name << "=" << v.asString();  // numbers and bools as text too
        }
    }
    return out.str();
}

void RegisterDeviceHistory(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["action"]["type"] = "string";
    for (const char* action : kActions)
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "The registry id of the device (list shows them; the slots of inspect_state aspect slots): beta128 (WD1793 commands), ...";
    schema["properties"]["last"]["type"] = "integer";
    schema["properties"]["last"]["description"] = "read: the newest N records (default 32; 0 = all kept)";
    schema["properties"]["mode"]["type"] = "string";
    schema["properties"]["mode"]["enum"].append("off");
    schema["properties"]["mode"]["enum"].append("ring");
    schema["properties"]["mode"]["enum"].append("capture");
    schema["properties"]["mode"]["description"] =
        "config: off (zero cost), ring (the last `limit`), capture (up to `limit`, 0 = no limit)";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["description"] = "config: ring size or capture cap";
    schema["properties"]["stream"]["type"] = "string";
    schema["properties"]["stream"]["description"] =
        "config: a file on the emulator host every record is written to as well (JSON Lines; use scratch/); '' stops it";
    schema["properties"]["stream_limit"]["type"] = "integer";
    schema["properties"]["stream_limit"]["description"] = "config: records to the stream file at most (0 = no limit)";
    schema["required"].append("action");

    registry.Register(
        "device_history",
        "Device histories: one per device and kind, by the device's registry id; action=list shows the machine's. Device "
        "events (beta128: WD1793 commands with track, sector, drive, side, PC, T-states, final status; ay / chips: register "
        "writes; sd-zc / sd0: SD commands and blocks; zxbus.1: the GS card's host traffic; ts-registers, vdac2, nextreg, "
        "copper, dma, uart ...), port accesses (`<device>.ports`, `board.ports`: what the registry's claims give each "
        "device) and execution (`cpu`: the main CPU's instructions; `<slot>.cpu`: a GS / NeoGS card's Z80; `copper`). Each "
        "history is off (zero cost), a ring (the standby mode; the FDC runs one of 64 commands from the start) or a capture "
        "with any limit, and may stream every record to a JSON Lines file. A config starts afresh (records, counts, the "
        "stream file). Actions: list, read, config, clear, pause (hold: nothing recorded, the records kept), resume.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            bool known = false;
            for (const char* a : kActions)
                known = known || action == a;
            if (!known)
            {
                done(ToolResult::Error("action: '" + action + "' is not one of list, read, config, clear, pause, resume"));
                return;
            }
            const std::string device = args["device"].asString();
            if (device.empty() && action != "list")
            {
                done(ToolResult::Error("device: required for " + action + " (device_history action=list shows the ids)"));
                return;
            }
            auto argsCopy = std::make_shared<Json::Value>(args);
            TargetResolver::ResolveFromArgs(args, caller, [action, device, argsCopy, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const Json::Value& a = *argsCopy;
                const std::string base = "/history/" + UrlEncodeSegment(device);
                std::string method = "GET";
                std::string path;
                auto body = std::make_shared<Json::Value>(Json::objectValue);
                const Json::Value* payload = nullptr;
                if (action == "list")
                    path = Endpoint(idOrError, "/history");
                else if (action == "read")
                    path = Endpoint(idOrError, base + "?last=" + std::to_string(a.get("last", 32).asUInt64()));
                else
                {
                    method = "POST";
                    path = Endpoint(idOrError, base + "/" + action);
                    for (const char* key : {"mode", "limit", "stream", "stream_limit"})
                        if (a.isMember(key))
                            (*body)[key] = a[key];
                    payload = body.get();
                }
                caller.Call(method, path, payload, [action, body, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300)
                    {
                        done(ToolResult::Error("device_history " + action + ": " + reply.get("error", DescribeErrorBody(reply)).asString()));
                        return;
                    }
                    // the text first: argument order is unspecified, and gcc moves `reply` out before reading it
                    const std::string text = DeviceHistoryText(action, reply);
                    done(ToolResult::Ok(text, std::move(reply)));
                });
            });
        });
}

} // namespace mcp
