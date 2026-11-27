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
        << (tang["esp_host"].asBool() ? ", ESP host" : "") << ", RAM_G " << (tang["ram_g_3mb"].asBool() ? "3 MB" : "2 MB")
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
    const Json::Value& threads = reply["threads"];
    if (threads.isObject())
        out << "\nThreads: ESP jobs " << (threads["esp_thread"].asBool() ? "on a worker" : "inline") << ", FT work "
            << (threads["ft_thread"].asBool() ? "on its own thread" : "inline") << ", FT drawing "
            << (threads["ft_draw_thread"].asBool() ? "on its renderer thread" : "inline")
            << (threads.isMember("ft_draw_threads") ? " in " + std::to_string(threads["ft_draw_threads"].asUInt()) + " line threads" : "")
            << ", latency "
            << threads["esp_latency_ms"].asUInt64() << " ms; " << threads["esp_jobs"].asUInt64() << " jobs ("
            << threads["esp_job_ms"].asDouble() << " ms), emulation waited " << threads["esp_waits"].asUInt64()
            << " times (" << threads["esp_wait_ms"].asDouble() << " ms); FT ops " << threads["ft_ops"].asUInt64()
            << ", reads " << threads["ft_reads"].asUInt64();
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

std::string Vdac4EspMemoryText(const Json::Value& reply)
{
    const std::string hex = reply["hex"].asString();
    const uint32_t address = reply["address"].asUInt();
    std::ostringstream out;
    out << "ESP memory, " << (hex.size() / 2) << " bytes at 0x" << std::hex << address
        << (reply["app"].asBool() ? " of the app's ELF image" : "");
    static const char kDigits[] = "0123456789abcdef";
    for (size_t offset = 0; offset * 2 < hex.size(); offset += 16)
    {
        const uint32_t at = address + static_cast<uint32_t>(offset);
        out << "\n";
        for (int shift = 28; shift >= 0; shift -= 4)
            out << kDigits[(at >> shift) & 0x0F];
        out << ":";
        for (size_t i = offset; i < offset + 16 && i * 2 < hex.size(); ++i)
            out << ' ' << hex.substr(i * 2, 2);
    }
    return out.str();
}

namespace
{

void RegisterVdac4EspMemory(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["address"]["type"] = "string";
    schema["properties"]["address"]["description"] = "32-bit address, decimal or 0x hex";
    schema["properties"]["size"]["type"] = "integer";
    schema["properties"]["size"]["minimum"] = 1;
    schema["properties"]["size"]["maximum"] = 4096;
    schema["properties"]["size"]["description"] = "Bytes (default 16, at most 4096)";
    schema["properties"]["app"]["type"] = "boolean";
    schema["properties"]["app"]["description"] =
        "The address is one of the running app's ELF image (as a haptic probe `cpu: esp` takes it)";
    schema["required"].append("address");

    registry.Register(
        "vdac4_esp_memory",
        "Read the TS-Conf VDAC4 board's ESP32-S3 memory (the TSL-VDAC4 model): bytes at a D-bus / I-bus / SRAM "
        "address, or with app=true at an address of the running app's ELF image (DOOM, R-Type VDAC4; the sections "
        "`vdac4_state` lists). A hex dump in the text, {address, size, app, hex} in structuredContent. Read-only, no "
        "pause (GET /vdac4/esp/memory). Finds the game state a haptic profile's `cpu: esp` probes watch.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const Json::Value& addressArg = args["address"];
            std::string address;
            if (addressArg.isString())
                address = addressArg.asString();
            else if (addressArg.isIntegral() && addressArg.asInt64() >= 0)
                address = std::to_string(addressArg.asUInt64());
            if (address.empty())
            {
                done(ToolResult::Error("address: a 32-bit address, decimal or 0x hex"));
                return;
            }
            const Json::Value& sizeArg = args["size"];
            if (!sizeArg.isNull() && (!sizeArg.isIntegral() || sizeArg.asInt64() < 1 || sizeArg.asInt64() > 4096))
            {
                done(ToolResult::Error("size: 1 to 4096 bytes"));
                return;
            }
            const std::string query = "/vdac4/esp/memory?address=" + address +
                                      "&size=" + std::to_string(sizeArg.isNull() ? 16 : sizeArg.asInt64()) +
                                      (args["app"].asBool() ? "&app=1" : "");
            TargetResolver::ResolveFromArgs(args, caller, [query, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string id = idOrError;
                caller.Call("GET", Endpoint(id, query), nullptr, [id, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300)
                    {
                        done(ToolResult::Error("vdac4_esp_memory [target " + id + "]: " + DescribeErrorBody(reply)));
                        return;
                    }
                    const std::string text = Vdac4EspMemoryText(reply) + "\n[target " + id + "]";
                    done(ToolResult::Ok(text, std::move(reply)));
                });
            });
        });
}

} // namespace

void RegisterVdac4State(ToolRegistry& registry)
{
    RegisterVdac4EspMemory(registry);
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
        "runtime error), the Z80 link counters and recent packets, the ESP audio player (rates, queue, underruns), "
        "the threads the ESP app's jobs run on (modes, latency, job and wait counters) and the ESP log. Read-only, safe to poll (GET /vdac4/state). A machine without the board answers an error saying why.",
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
