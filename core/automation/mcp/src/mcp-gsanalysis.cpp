#include "mcp-gsanalysis.h"

#include <memory>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

const std::vector<std::string>& GSAnalysisToolActions()
{
    static const std::vector<std::string> actions = {"status", "on", "off", "reset", "release", "coverage", "heatmap"};
    return actions;
}

namespace
{
/// `?key=value&...` of the read parameters present in @p args (numbers, strings and booleans)
std::string Query(const Json::Value& args, std::initializer_list<const char*> keys)
{
    std::string query;
    for (const char* key : keys)
    {
        if (!args.isMember(key))
            continue;
        const Json::Value& v = args[key];
        std::string value;
        if (v.isBool())
            value = v.asBool() ? "1" : "0";
        else if (v.isIntegral())
            value = std::to_string(v.asInt64());
        else if (v.isArray())
        {
            for (const Json::Value& item : v)
                value += (value.empty() ? "" : ",") + item.asString();
        }
        else
            value = v.asString();
        query += (query.empty() ? "?" : "&") + std::string(key) + "=" + UrlEncodeSegment(value);
    }
    return query;
}
}  // namespace

void RegisterGSAnalysis(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    for (const std::string& action : GSAnalysisToolActions())
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["action"]["description"] =
        "status (default), on (allocate once, mark from the card's next run), off (stop marking, keep the data), "
        "reset (clear the data), release (free it), coverage (the code / data log), heatmap (the RAM heat map)";
    schema["properties"]["cpu"]["type"] = "string";
    schema["properties"]["cpu"]["description"] = "gs (default: the GS slot's card) or ms (the ZX-MultiSound's GS)";
    schema["properties"]["decay"]["type"] = "integer";
    schema["properties"]["decay"]["description"] = "on / status: heat lost per frame, 0-255 (default 8)";
    schema["properties"]["space"]["type"] = "string";
    schema["properties"]["space"]["description"] = "coverage: rom (flash on NeoGS) or ram (default both)";
    schema["properties"]["page"]["type"] = "integer";
    schema["properties"]["page"]["description"] = "coverage: one 16 KB page of space (adds its CALL / RST entries)";
    schema["properties"]["ranges"]["type"] = "integer";
    schema["properties"]["ranges"]["description"] = "coverage: ranges listed per page and kind (default 16, 0: none)";
    schema["properties"]["raw"]["type"] = "boolean";
    schema["properties"]["raw"]["description"] = "coverage with page: its 16,384 flag bytes as base64";
    schema["properties"]["channels"]["type"] = "string";
    schema["properties"]["channels"]["description"] =
        "heatmap: comma list of the grid's channels: fetch, read, write, dac, sd_dma, zx_dma, all (default all)";
    schema["properties"]["recent"]["type"] = "string";
    schema["properties"]["recent"]["description"] =
        "heatmap: comma list of the channels whose ranges of the last `frames` frames are listed (default dac)";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["description"] = "heatmap recent: the last N host frames (default 50)";
    schema["properties"]["cells"]["type"] = "integer";
    schema["properties"]["cells"]["description"] = "heatmap: the grid's cells at most (default 4096, 0: all)";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["description"] =
        "heatmap grid: none (default here: counts and ranges only), base64 or hex";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";

    registry.Register(
        "gs_analysis",
        "Coverage and heat map of the General Sound card's own Z80 (classic GS, the ZX-MultiSound's GS, NeoGS; not the "
        "lightweight GS). Off by default and free while off: action on starts marking. coverage: a code / data log per "
        "physical byte of ROM / flash and each RAM page (exec, operand, read, write, jump target, CALL target, DAC "
        "fetch, written since the card reset), OR-merged over sessions, with code / data / DAC ranges per page, the "
        "firmware's command handlers that ran, and the reads of RAM never written since the card reset with their PC. "
        "heatmap: the RAM in cells per channel (CPU fetch / read / write, DAC, SD-MP3 DMA, ZX-DMA) decaying each frame, "
        "and the RAM ranges a channel touched in the last N frames (default: what the DAC played).",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args.isMember("action") ? args["action"].asString() : std::string("status");
            bool known = false;
            for (const std::string& a : GSAnalysisToolActions())
                known = known || a == action;
            if (!known)
            {
                done(ToolResult::Error(UnknownValueText("action", action, GSAnalysisToolActions())));
                return;
            }
            std::string suffix;
            std::shared_ptr<Json::Value> body;
            if (action == "coverage")
                suffix = "/state/audio/gs/coverage" + Query(args, {"cpu", "space", "page", "ranges", "raw"});
            else if (action == "heatmap")
            {
                Json::Value read = args;
                if (!read.isMember("format"))
                    read["format"] = "none";  // the grid is for programs; ask for base64 or hex to get it
                suffix = "/state/audio/gs/heatmap" +
                         Query(read, {"cpu", "channels", "recent", "frames", "cells", "format"});
            }
            else
            {
                suffix = "/control/audio/gs/analysis";
                body = std::make_shared<Json::Value>(Json::objectValue);
                (*body)["action"] = action;
                if (args.isMember("cpu"))
                    (*body)["cpu"] = args["cpu"];
                if (args.isMember("decay"))
                    (*body)["decay"] = args["decay"];
            }
            TargetResolver::ResolveFromArgs(args, caller, [action, suffix, body, &caller, done](bool ok,
                                                                                           const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                caller.Call(body ? "POST" : "GET", Endpoint(idOrError, suffix), body ? body.get() : nullptr,
                            [action, body, done](int status, Json::Value reply) {
                                if (status == 0)
                                {
                                    done(ToolResult::Error("WebAPI unreachable - is the emulator running with WebAPI "
                                                           "enabled (port 8090)?"));
                                    return;
                                }
                                if (status < 200 || status >= 300)
                                {
                                    done(ToolResult::Error("gs_analysis " + action + ": HTTP " + std::to_string(status) +
                                                           ": " + DescribeErrorBody(reply)));
                                    return;
                                }
                                const std::string text = reply.isMember("text") ? reply["text"].asString()
                                                                                : FlatFields(reply);
                                done(ToolResult::Ok(text, std::move(reply)));
                            });
            });
        });
}

} // namespace mcp
