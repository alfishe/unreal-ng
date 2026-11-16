#include "mcp-vdac4.h"

#include <algorithm>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

std::string Vdac4StateText(const Json::Value& reply, size_t logLines)
{
    const Json::Value& tang = reply["tang"];
    const Json::Value& esp = reply["esp"];
    const Json::Value& app = reply["app"];
    const Json::Value& link = reply["link"];
    const Json::Value& audio = reply["audio"];
    std::ostringstream out;
    out << "VDAC4 board: Tang personality " << tang["personality"].asString() << (tang["armed"].asBool() ? ", armed" : "")
        << (tang["esp_host"].asBool() ? ", ESP host" : "") << ", RAM_G " << (tang["ram_g_3mb"].asBool() ? "3 MB" : "1 MB")
        << "\nESP: " << esp["phase"].asString() << ", " << esp["ms"].asUInt64() << " ms, boots " << esp["boots"].asUInt64()
        << ", last step " << esp["last_step"].asUInt64() << ", resources " << esp["resources"].asUInt64()
        << ", clock restarts " << esp["clock_restarts"].asUInt64() << ", ahead " << esp["clock_offset_us"].asUInt64() << " us";
    if (!esp["last_error"].asString().empty())
        out << "\nESP error: " << esp["last_error"].asString();
    out << "\nApp: ";
    if (app["running"].asBool())
        out << "running \"" << app["name"].asString() << "\" " << app["version"].asString() << " (" << app["module"].asString() << ")";
    else
        out << "none" << (app["module"].asString().empty() ? "" : " (last " + app["module"].asString() + ")");
    if (!app["error"].asString().empty())
        out << "\nApp error: " << app["error"].asString();
    out << "\nLink: " << reply["packets"]["count"].asUInt64() << " packets, pending from Z80 "
        << link["pending_from_z80"].asUInt64() << " / to Z80 " << link["pending_to_z80"].asUInt64()
        << (link["send_blocked"].asBool() ? ", send blocked" : "") << ", bad checksums " << link["bad_checksums"].asUInt64()
        << ", resyncs " << link["resyncs"].asUInt64() << ", dropped frames " << link["dropped_frames"].asUInt64();
    out << "\nAudio: ";
    if (audio["input_rate"].asUInt64() == 0)
        out << "never opened";
    else
        out << audio["input_rate"].asUInt64() << " Hz -> " << audio["output_rate"].asUInt64() << " Hz, queued "
            << audio["queued"].asUInt64() << ", underruns " << audio["underruns"].asUInt64() << ", dropped "
            << audio["dropped"].asUInt64() << (audio["sound"].asBool() ? ", sounding" : ", silent");
    const Json::Value& log = reply["log"];
    const size_t total = log.isArray() ? log.size() : 0;
    const size_t shown = std::min(total, logLines);
    if (shown != 0)
    {
        out << "\nLog (last " << shown << " of " << total << "):";
        for (size_t i = total - shown; i < total; ++i)
            out << "\n  " << log[static_cast<Json::ArrayIndex>(i)].asString();
    }
    return out.str();
}

void RegisterVdac4State(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["log"]["type"] = "integer";
    schema["properties"]["log"]["minimum"] = 0;
    schema["properties"]["log"]["maximum"] = 256;
    schema["properties"]["log"]["description"] =
        "Recent ESP log lines in structuredContent (default 50, at most 256; the text shows the last 10 of them)";

    registry.Register(
        "vdac4_state",
        "The TS-Conf VDAC4 board (Tang FPGA with an FT812-compatible core + ESP32-S3 running apps such as DOOM; the "
        "TSL-VDAC4 model): the Tang's personality (ts / loading / ft), FT_CFG, the ESP's phase (off / booting / running "
        "/ flushing / restart_wait), boots and last error, the running app (module, name, version, command range, "
        "runtime error), the Z80 link counters and recent packets, the ESP audio player (rates, queue, underruns) and "
        "the ESP log. Read-only, safe to poll (GET /vdac4/state). A machine without the board answers an error saying why.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const Json::Value& logArg = args["log"];
            size_t logLines = 50;
            if (!logArg.isNull())
            {
                if (!logArg.isIntegral() || logArg.asInt64() < 0)
                {
                    done(ToolResult::Error("log: a line count from 0 to 256"));
                    return;
                }
                logLines = std::min<size_t>(static_cast<size_t>(logArg.asUInt64()), 256);
            }
            TargetResolver::ResolveFromArgs(args, caller, [logLines, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string id = idOrError;
                caller.Call("GET", Endpoint(id, "/vdac4/state?log=" + std::to_string(logLines)), nullptr,
                            [id, logLines, done](int status, Json::Value reply) {
                                if (status == 0)
                                {
                                    done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                                    return;
                                }
                                if (status < 200 || status >= 300)
                                {
                                    std::string text = "vdac4_state [target " + id + "]: " + DescribeErrorBody(reply);
                                    if (status == 409)
                                        text += " (the board comes with the TSL-VDAC4 model, or [MISC] TS_VDAC4=1 on TS-Conf)";
                                    done(ToolResult::Error(text));
                                    return;
                                }
                                // the text first: argument order is unspecified, and gcc moves `reply` out before reading it
                                const std::string text = Vdac4StateText(reply, std::min<size_t>(logLines, 10)) +
                                                         "\n[target " + id + "]";
                                done(ToolResult::Ok(text, std::move(reply)));
                            });
            });
        });
}

} // namespace mcp
