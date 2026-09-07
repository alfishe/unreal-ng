// MCP smart tools — registry core + Phase 1 "Core 5" tools
//
// Tools orchestrate existing WebAPI endpoints over the loopback IApiCaller:
//   1. emulator_manage    — lifecycle: create/list/start/stop/pause/resume/reset/destroy
//   2. load_software      — auto-detect .sna/.z80/.tap/.tzx/.trd/.scl/.fdi and load
//   3. control_execution  — stepping/running + breakpoint management
//   4. inspect_state      — multi-aspect state inspection (registers/memory/disasm/...)
//   5. type_input         — keyboard: type/tap/press/release/combo/macro
//
// Phase 2 smart tools live in their own modules: mcp-symbols (manage_symbols),
// mcp-analysis (debug_code, analyze_performance) and mcp-media (capture_media);
// they are composed into the registry by BuildFullRegistry below.
//
// Every tool accepts "target" (emulator id or "auto"); results are dual-content
// (text summary + structuredContent). Drogon-free.

#include "mcp-tools.h"

#include "mcp-analysis.h"
#include "mcp-media.h"
#include "mcp-router.h"
#include "mcp-symbols.h"
#include "mcp-tool-utils.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <sstream>

namespace mcp
{

/// region <ToolRegistry>

void ToolRegistry::Register(const std::string& name, const std::string& description, Json::Value inputSchema, ToolHandler handler)
{
    ToolDefinition definition;
    definition.name = name;
    definition.description = description;
    definition.inputSchema = std::move(inputSchema);
    definition.handler = std::move(handler);
    _tools[name] = std::move(definition);
}

const ToolDefinition* ToolRegistry::Find(const std::string& name) const
{
    auto it = _tools.find(name);
    return it == _tools.end() ? nullptr : &it->second;
}

Json::Value ToolRegistry::ToolListJson() const
{
    Json::Value tools(Json::arrayValue);
    for (const auto& [name, definition] : _tools)
    {
        Json::Value tool;
        tool["name"] = name;
        tool["description"] = definition.description;
        tool["inputSchema"] = definition.inputSchema;
        tools.append(tool);
    }
    return tools;
}

std::vector<std::string> ToolRegistry::ToolNames() const
{
    std::vector<std::string> names;
    names.reserve(_tools.size());
    for (const auto& [name, definition] : _tools)
    {
        names.push_back(name);
    }
    return names;
}

/// endregion </ToolRegistry>

/// region <Shared helpers>
// The shared helper vocabulary (Endpoint / DescribeErrorBody / FormatRegisters /
// RunSeries / ForwardCall / RunFrames / ResolveAndForward …) lives in
// mcp-tool-utils.h so every smart-tool module speaks the same language.

/// endregion </Shared helpers>

/// region <emulator_manage>

namespace
{

void RegisterEmulatorManage(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"create", "list", "list_models", "status", "start", "stop", "pause", "resume", "reset", "destroy"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Lifecycle operation. 'create' makes a new running instance; 'list' shows all instances; 'list_models' enumerates hardware models.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto' to reuse the single instance (auto-created when none exists)";
    schema["properties"]["model"]["type"] = "string";
    schema["properties"]["model"]["description"] = "Hardware model for 'create' — e.g. 48K, 128k, PLUS2, PLUS3, PENTAGON, SCORPION, ATM1..3, PROFI (see list_models)";
    schema["properties"]["ram_size"]["type"] = "integer";
    schema["properties"]["ram_size"]["description"] = "Optional RAM size in KB for 'create' (e.g. 128, 256, 512)";
    schema["required"].append("action");

    registry.Register(
        "emulator_manage",
        "Manage Unreal-NG emulator instances: create, list, switch models, start/stop/pause/resume/reset/destroy. "
        "Multi-instance: target identifies the machine; 'auto' reuses the single instance or creates a default 128k one.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            if (action == "list")
            {
                caller.Call("GET", "/api/v1/emulator", nullptr, [done](int status, Json::Value body) {
                    if (status != 200)
                    {
                        done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(body)));
                        return;
                    }
                    const Json::Value& emulators = body["emulators"];
                    std::ostringstream out;
                    out << emulators.size() << " emulator(s)";
                    for (Json::ArrayIndex i = 0; i < emulators.size(); ++i)
                    {
                        out << "\n- " << emulators[i]["id"].asString() << " state=" << emulators[i]["state"].asString()
                            << " running=" << (emulators[i]["is_running"].asBool() ? "true" : "false");
                    }
                    done(ToolResult::Ok(out.str(), std::move(body)));
                });
                return;
            }

            if (action == "list_models")
            {
                ForwardCall("GET", "/api/v1/emulator/models", nullptr, caller, "Available hardware models", done);
                return;
            }

            if (action == "create")
            {
                Json::Value body;
                body["model"] = args.isMember("model") && args["model"].isString() && !args["model"].asString().empty()
                                    ? args["model"].asString()
                                    : TargetResolver::kDefaultAutoCreateModel;
                if (args.isMember("ram_size") && args["ram_size"].asUInt() > 0)
                {
                    body["ram_size"] = args["ram_size"].asUInt();
                }
                caller.Call("POST", "/api/v1/emulator/start", &body, [done](int status, Json::Value response) {
                    if (status == 201 || status == 200)
                    {
                        done(ToolResult::Ok("Created and started emulator " + response["id"].asString() + " (model " +
                                                response.get("symbolic_id", Json::Value("")).asString() + ")",
                                            std::move(response)));
                        return;
                    }
                    done(ToolResult::Error("Create failed (HTTP " + std::to_string(status) + "): " + DescribeErrorBody(response)));
                });
                return;
            }

            // Remaining actions need a resolved target
            auto forward = [&caller, &args, action, done](const std::string& id) {
                if (action == "status")
                {
                    ForwardCall("GET", Endpoint(id), nullptr, caller, "Status of " + id, done);
                }
                else if (action == "start")
                {
                    ForwardCall("POST", Endpoint(id, "/start"), nullptr, caller, "Started " + id, done);
                }
                else if (action == "stop")
                {
                    ForwardCall("POST", Endpoint(id, "/stop"), nullptr, caller, "Stopped " + id, done);
                }
                else if (action == "pause")
                {
                    ForwardCall("POST", Endpoint(id, "/pause"), nullptr, caller, "Paused " + id, done);
                }
                else if (action == "resume")
                {
                    ForwardCall("POST", Endpoint(id, "/resume"), nullptr, caller, "Resumed " + id, done);
                }
                else if (action == "reset")
                {
                    ForwardCall("POST", Endpoint(id, "/reset"), nullptr, caller, "Reset " + id, done);
                }
                else if (action == "destroy")
                {
                    ForwardCall("DELETE", Endpoint(id), nullptr, caller, "Destroyed " + id, done);
                }
                else
                {
                    done(ToolResult::Error("Unknown action '" + action + "'"));
                }
                (void)args;
            };

            TargetResolver::ResolveFromArgs(args, caller, [forward, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                forward(idOrError);
            });
        });
}

} // namespace

/// endregion </emulator_manage>

/// region <load_software>

namespace
{

void RegisterLoadSoftware(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "Absolute or relative path to the software file";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["drive"]["type"] = "string";
    schema["properties"]["drive"]["default"] = "A";
    schema["properties"]["drive"]["description"] = "Floppy drive for disk images (A or B)";
    schema["properties"]["play"]["type"] = "boolean";
    schema["properties"]["play"]["default"] = false;
    schema["properties"]["play"]["description"] = "Start tape playback immediately after loading a tape";
    schema["required"].append("path");

    registry.Register(
        "load_software",
        "Load software into the emulator by auto-detecting the file type: snapshots (.sna .z80), tapes (.tap .tzx), "
        "disk images (.trd .scl .fdi). The machine must be created first (target:'auto' handles that).",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string path = args["path"].asString();
            if (path.empty())
            {
                done(ToolResult::Error("Missing 'path' argument"));
                return;
            }

            // Extension detection
            auto dot = path.find_last_of('.');
            if (dot == std::string::npos || dot + 1 >= path.size())
            {
                done(ToolResult::Error("Cannot determine file type of '" + path +
                                            "'. Supported: .sna .z80 (snapshot), .tap .tzx (tape), .trd .scl .fdi (disk)"));
                return;
            }
            std::string ext = path.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            bool isSnapshot = ext == "sna" || ext == "z80";
            bool isTape = ext == "tap" || ext == "tzx";
            bool isDisk = ext == "trd" || ext == "scl" || ext == "fdi";
            if (!isSnapshot && !isTape && !isDisk)
            {
                done(ToolResult::Error("Unsupported file type '." + ext +
                                            "'. Supported: .sna .z80 (snapshot), .tap .tzx (tape), .trd .scl .fdi (disk)"));
                return;
            }

            bool play = args.isMember("play") && args["play"].asBool();
            std::string drive = args.isMember("drive") && args["drive"].isString() && !args["drive"].asString().empty()
                                    ? args["drive"].asString()
                                    : "A";

            TargetResolver::ResolveFromArgs(args, caller, [path, ext, isSnapshot, isTape, isDisk, play, drive, &caller, done](
                                                              bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;

                Json::Value body;
                body["path"] = path;

                if (isSnapshot)
                {
                    ForwardCall("POST", Endpoint(id, "/snapshot/load"), &body, caller,
                                "Loaded snapshot " + path + " into " + id, done);
                    return;
                }

                if (isTape)
                {
                    caller.Call("POST", Endpoint(id, "/tape/load"), &body, [&caller, id, path, play, done](int status, Json::Value response) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("Tape load failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response)));
                            return;
                        }
                        if (!play)
                        {
                            done(ToolResult::Ok("Loaded tape " + path + " into " + id, std::move(response)));
                            return;
                        }
                        ForwardCall("POST", Endpoint(id, "/tape/play"), nullptr, caller, "Loaded tape " + path + " and started playback on " + id, done);
                    });
                    return;
                }

                // Disk
                ForwardCall("POST", Endpoint(id, "/disk/" + drive + "/insert"), &body, caller,
                            "Inserted disk " + path + " into drive " + drive + " of " + id, done);
            });
        });
}

} // namespace

/// endregion </load_software>

/// region <control_execution>

namespace
{

void RegisterControlExecution(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"run", "pause", "resume", "step", "step_n", "step_over", "step_out", "run_frame", "run_frames",
                               "run_tstates", "run_to_interrupt", "bp_add", "bp_remove", "bp_enable", "bp_disable", "bp_clear",
                               "bp_list"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Execution primitive. Stepping/running actions return the new register state. bp_* actions manage breakpoints.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["count"]["type"] = "integer";
    schema["properties"]["count"]["description"] = "Instruction count for step_n";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["description"] = "Frame count for run_frames";
    schema["properties"]["tstates"]["type"] = "integer";
    schema["properties"]["tstates"]["description"] = "T-state count for run_tstates";
    schema["properties"]["address"]["type"] = "integer";
    schema["properties"]["address"]["description"] = "Breakpoint address for bp_add (Z80 address or port number)";
    schema["properties"]["type"]["type"] = "string";
    schema["properties"]["type"]["default"] = "execution";
    schema["properties"]["type"]["description"] = "Breakpoint type for bp_add: execution, read, write, port_in, port_out";
    schema["properties"]["bp_id"]["type"] = "string";
    schema["properties"]["bp_id"]["description"] = "Breakpoint id for bp_remove/bp_enable/bp_disable";
    schema["required"].append("action");

    registry.Register(
        "control_execution",
        "Advance or halt the CPU: pause/resume/run, step (1 or N instructions), step_over, step_out, run_frame(s), "
        "run_tstates, run_to_interrupt. Also manages breakpoints (bp_add/bp_remove/bp_enable/bp_disable/bp_clear/bp_list). "
        "Stepping actions automatically include the new register snapshot.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            TargetResolver::ResolveFromArgs(args, caller, [action, &args, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;

                // Simple pass-through actions
                if (action == "run" || action == "resume")
                {
                    caller.Call("POST", Endpoint(id, "/resume"), nullptr, [action, done](int status, Json::Value body) {
                        if (status >= 200 && status < 300)
                        {
                            done(ToolResult::Ok(action == "run" ? "Running (until breakpoint/pause)" : "Resumed", std::move(body)));
                        }
                        else if (status == 400 || status == 409)
                        {
                            // Idempotent: already running
                            done(ToolResult::Ok("Emulator is already running", std::move(body)));
                        }
                        else
                        {
                            done(ToolResult::Error("Resume failed (HTTP " + std::to_string(status) + "): " + DescribeErrorBody(body)));
                        }
                    });
                    return;
                }

                if (action == "pause")
                {
                    caller.Call("POST", Endpoint(id, "/pause"), nullptr, [id, &caller, done](int status, Json::Value body) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("Pause failed (HTTP " + std::to_string(status) + "): " + DescribeErrorBody(body)));
                            return;
                        }
                        // Append register snapshot — meaningful right after a pause
                        caller.Call("GET", Endpoint(id, "/registers"), nullptr, [body, done](int regStatus, Json::Value registers) mutable {
                            if (regStatus == 200)
                            {
                                done(ToolResult::Ok("Paused. " + FormatRegisters(registers), std::move(registers)));
                            }
                            else
                            {
                                done(ToolResult::Ok("Paused", std::move(body)));
                            }
                        });
                    });
                    return;
                }

                if (action == "bp_list")
                {
                    ForwardCall("GET", Endpoint(id, "/breakpoints"), nullptr, caller, "Breakpoints of " + id, done);
                    return;
                }
                if (action == "bp_clear")
                {
                    ForwardCall("DELETE", Endpoint(id, "/breakpoints"), nullptr, caller, "Cleared all breakpoints on " + id, done);
                    return;
                }
                if (action == "bp_add")
                {
                    if (!args.isMember("address"))
                    {
                        done(ToolResult::Error("bp_add requires 'address'"));
                        return;
                    }
                    Json::Value body;
                    body["address"] = args["address"];
                    body["type"] = args.isMember("type") ? args["type"].asString() : "execution";
                    ForwardCall("POST", Endpoint(id, "/breakpoints"), &body, caller, "Breakpoint added on " + id, done);
                    return;
                }
                if (action == "bp_remove" || action == "bp_enable" || action == "bp_disable")
                {
                    if (!args.isMember("bp_id"))
                    {
                        done(ToolResult::Error(action + " requires 'bp_id'"));
                        return;
                    }
                    std::string bpId = args["bp_id"].asString();
                    if (action == "bp_remove")
                    {
                        ForwardCall("DELETE", Endpoint(id, "/breakpoints/" + bpId), nullptr, caller, "Breakpoint removed", done);
                    }
                    else
                    {
                        ForwardCall("PUT", Endpoint(id, "/breakpoints/" + bpId + (action == "bp_enable" ? "/enable" : "/disable")),
                                    nullptr, caller, action == "bp_enable" ? "Breakpoint enabled" : "Breakpoint disabled", done);
                    }
                    return;
                }

                // Execution-advancing actions: perform the call, then append registers
                auto execute = [action, &args, &caller, id, done]() {
                    std::string method = "POST";
                    std::string path;
                    Json::Value body;
                    std::string summary;

                    if (action == "step")
                    {
                        path = Endpoint(id, "/step");
                        summary = "Stepped 1 instruction. ";
                    }
                    else if (action == "step_n")
                    {
                        path = Endpoint(id, "/steps");
                        body["count"] = args.isMember("count") ? args["count"].asUInt() : 1u;
                        summary = "Stepped " + std::to_string(body["count"].asUInt()) + " instructions. ";
                    }
                    else if (action == "step_over")
                    {
                        path = Endpoint(id, "/stepover");
                        summary = "Stepped over. ";
                    }
                    else if (action == "step_out")
                    {
                        path = Endpoint(id, "/stepout");
                        summary = "Stepped out of subroutine. ";
                    }
                    else if (action == "run_frame" || action == "run_frames")
                    {
                        path = Endpoint(id, "/run_frames");
                        unsigned frames = action == "run_frame" ? 1u : (args.isMember("frames") ? args["frames"].asUInt() : 1u);
                        body["frames"] = frames;
                        summary = "Ran " + std::to_string(frames) + " frame(s). ";
                    }
                    else if (action == "run_tstates")
                    {
                        path = Endpoint(id, "/run_tstates");
                        body["tstates"] = args.isMember("tstates") ? args["tstates"].asUInt() : 1u;
                        summary = "Ran " + std::to_string(body["tstates"].asUInt()) + " t-states. ";
                    }
                    else if (action == "run_to_interrupt")
                    {
                        path = Endpoint(id, "/run_to_interrupt");
                        summary = "Ran to interrupt. ";
                    }
                    else
                    {
                        done(ToolResult::Error("Unknown action '" + action + "'"));
                        return;
                    }

                    const Json::Value* bodyPtr = body.isNull() ? nullptr : &body;
                    caller.Call(method, path, bodyPtr, [summary, &caller, id, done](int status, Json::Value response) mutable {
                        if (status < 200 || status >= 300)
                        {
                            std::string hint;
                            if (status == 409)
                            {
                                hint = " Hint: the machine may need to be paused first — use action:'pause'.";
                            }
                            done(ToolResult::Error("Execution action failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response) + hint));
                            return;
                        }
                        caller.Call("GET", Endpoint(id, "/registers"), nullptr,
                                    [summary, response, done](int regStatus, Json::Value registers) mutable {
                                        if (regStatus == 200)
                                        {
                                            Json::Value structured = response;
                                            structured["registers"] = registers;
                                            done(ToolResult::Ok(summary + FormatRegisters(registers), std::move(structured)));
                                        }
                                        else
                                        {
                                            done(ToolResult::Ok(summary, std::move(response)));
                                        }
                                    });
                    });
                };
                execute();
            });
        });
}

} // namespace

/// endregion </control_execution>

/// region <inspect_state>

namespace
{

void RegisterInspectState(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["aspects"]["type"] = "array";
    schema["properties"]["aspects"]["items"]["type"] = "string";
    Json::Value allowed(Json::arrayValue);
    for (const char* aspect : {"machine", "registers", "memory", "disasm", "stack", "breakpoints", "memory_banks", "screen_ocr",
                               "screen_image", "screen_digest", "timing"})
    {
        allowed.append(aspect);
    }
    schema["properties"]["aspects"]["items"]["enum"] = allowed;
    schema["properties"]["aspects"]["default"] = Json::Value(Json::arrayValue);
    schema["properties"]["aspects"]["default"].append("registers");
    schema["properties"]["aspects"]["default"].append("disasm");
    schema["properties"]["aspects"]["default"].append("screen_ocr");
    schema["properties"]["aspects"]["description"] =
        "What to inspect. Default: registers + disasm + screen_ocr. 'stack' reads 32 bytes at SP; 'memory' needs address.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["address"]["type"] = "integer";
    schema["properties"]["address"]["description"] = "Start address for 'memory' and 'disasm' (defaults: 0 and current PC)";
    schema["properties"]["size"]["type"] = "integer";
    schema["properties"]["size"]["default"] = 64;
    schema["properties"]["size"]["description"] = "Byte count for 'memory' (max 4096)";
    schema["properties"]["count"]["type"] = "integer";
    schema["properties"]["count"]["default"] = 8;
    schema["properties"]["count"]["description"] = "Instruction count for 'disasm'";
    schema["properties"]["include_image"]["type"] = "boolean";
    schema["properties"]["include_image"]["default"] = false;
    schema["properties"]["include_image"]["description"] = "Include base64 image data for 'screen_image' (large payload)";

    registry.Register(
        "inspect_state",
        "Inspect emulator state in one call: registers, memory ranges, disassembly, stack words, breakpoints, memory banks, "
        "screen OCR text, screen image metadata, screen digest hash, raster timing. Combine aspects to reduce round-trips.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn& progress) {
            // Collect aspects
            std::vector<std::string> aspects;
            if (args.isMember("aspects") && args["aspects"].isArray() && args["aspects"].size() > 0)
            {
                for (const auto& aspect : args["aspects"])
                {
                    aspects.push_back(aspect.asString());
                }
            }
            else
            {
                aspects = {"registers", "disasm", "screen_ocr"};
            }

            for (const std::string& aspect : aspects)
            {
                if (aspect != "machine" && aspect != "registers" && aspect != "memory" && aspect != "disasm" && aspect != "stack" &&
                    aspect != "breakpoints" && aspect != "memory_banks" && aspect != "screen_ocr" && aspect != "screen_image" &&
                    aspect != "screen_digest" && aspect != "timing")
                {
                    done(ToolResult::Error("Unknown aspect '" + aspect +
                                            "'. Valid: machine, registers, memory, disasm, stack, breakpoints, memory_banks, "
                                            "screen_ocr, screen_image, screen_digest, timing"));
                    return;
                }
            }

            unsigned address = args.isMember("address") ? args["address"].asUInt() : 0u;
            const bool hasAddress = args.isMember("address");
            unsigned size = args.isMember("size") ? args["size"].asUInt() : 64u;
            if (size < 1) size = 1;
            if (size > 4096) size = 4096;
            unsigned count = args.isMember("count") ? args["count"].asUInt() : 8u;
            if (count < 1) count = 1;
            if (count > 256) count = 256;
            bool includeImage = args.isMember("include_image") && args["include_image"].asBool();

            TargetResolver::ResolveFromArgs(
                args, caller,
                [aspects, address, hasAddress, size, count, includeImage, &caller, done, progress](
                    bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    const std::string& id = idOrError;

                    // Build one series step per aspect
                    std::vector<SeriesStep> steps;
                    for (const std::string& aspect : aspects)
                    {
                        if (aspect == "machine")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "registers" || aspect == "breakpoints" || aspect == "memory_banks")
                        {
                            std::string suffix = aspect == "registers" ? "/registers" : (aspect == "breakpoints" ? "/breakpoints" : "/state/memory");
                            steps.push_back([&caller, id, aspect, suffix](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, suffix), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "memory")
                        {
                            steps.push_back([&caller, id, aspect, address, size](Json::Value& acc, std::function<void(bool)> next) {
                                std::string path = Endpoint(id, "/memory/" + std::to_string(address)) + "?len=" + std::to_string(size);
                                caller.Call("GET", path, nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "disasm")
                        {
                            steps.push_back([&caller, id, aspect, address, hasAddress, count](Json::Value& acc, std::function<void(bool)> next) {
                                std::string path = Endpoint(id, "/disasm") + "?count=" + std::to_string(count);
                                if (hasAddress)
                                {
                                    path += "&address=" + std::to_string(address);
                                }
                                caller.Call("GET", path, nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "stack")
                        {
                            // Two chained calls: registers (for SP), then 32 bytes at SP decoded as words
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/registers"), nullptr, [&caller, id, aspect, &acc, next](int status, Json::Value registers) mutable {
                                    if (status != 200 || !registers.isObject() || !registers["special"].isObject())
                                    {
                                        next(true);
                                        return;
                                    }
                                    unsigned sp = registers["special"]["sp"].asUInt() & 0xFFFFu;
                                    std::string path = Endpoint(id, "/memory/" + std::to_string(sp)) + "?len=32";
                                    caller.Call("GET", path, nullptr, [sp, aspect, &acc, next](int memStatus, Json::Value memory) mutable {
                                        if (memStatus == 200 && memory["data"].isArray())
                                        {
                                            Json::Value stack;
                                            stack["sp"] = sp;
                                            Json::Value words(Json::arrayValue);
                                            const Json::Value& data = memory["data"];
                                            for (Json::ArrayIndex i = 0; i + 1 < data.size(); i += 2)
                                            {
                                                Json::Value word;
                                                word["address"] = (sp + i) & 0xFFFFu;
                                                word["value"] = (data[i].asUInt() & 0xFFu) | ((data[i + 1].asUInt() & 0xFFu) << 8);
                                                words.append(word);
                                            }
                                            stack["words"] = words;
                                            acc[aspect] = std::move(stack);
                                        }
                                        next(true);
                                    });
                                });
                            });
                        }
                        else if (aspect == "screen_ocr")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/capture/ocr"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "screen_image")
                        {
                            steps.push_back([&caller, id, aspect, includeImage](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/capture/screen"), nullptr, [aspect, includeImage, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200)
                                    {
                                        if (!includeImage && body.isMember("data"))
                                        {
                                            Json::Value meta = body;
                                            std::string sizeNote = "Set include_image:true to receive the base64 payload.";
                                            meta["_note"] = sizeNote;
                                            meta.removeMember("data");
                                            acc[aspect] = std::move(meta);
                                        }
                                        else
                                        {
                                            acc[aspect] = std::move(body);
                                        }
                                    }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "screen_digest")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/screen/digest"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "timing")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/video/beam"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200)
                                    {
                                        acc[aspect] = std::move(body);
                                        next(true);
                                    }
                                    else
                                    {
                                        // Beam endpoint missing (older build) — still report model timing basics
                                        acc[aspect] = Json::Value();
                                        next(true);
                                    }
                                });
                            });
                        }
                    }

                    RunSeries(ReportSeriesProgress(std::move(steps), progress, aspects), [aspects, done, id](Json::Value acc) {
                        std::ostringstream out;
                        out << "State of " << id << ":";
                        for (const std::string& aspect : aspects)
                        {
                            if (!acc.isMember(aspect) || acc[aspect].isNull())
                            {
                                out << "\n[" << aspect << "] unavailable";
                                continue;
                            }
                            const Json::Value& value = acc[aspect];
                            if (aspect == "registers")
                            {
                                out << "\n[registers] " << FormatRegisters(value);
                            }
                            else if (aspect == "disasm" && value["instructions"].isArray())
                            {
                                out << "\n[disasm] " << value["instructions"].size() << " instruction(s) at "
                                    << Hex16(value["address"].asUInt());
                            }
                            else if (aspect == "screen_ocr")
                            {
                                std::string text = value["text"].asString();
                                if (text.size() > 200)
                                {
                                    text = text.substr(0, 200) + "...";
                                }
                                out << "\n[screen_ocr] \"" << text << "\"";
                            }
                            else if (aspect == "memory")
                            {
                                out << "\n[memory] " << value["length"].asUInt() << " bytes at " << Hex16(value["address"].asUInt())
                                    << ": " << value["hex"].asString().substr(0, 96);
                            }
                            else if (aspect == "stack" && value["words"].isArray())
                            {
                                out << "\n[stack] top of stack (SP=" << Hex16(value["sp"].asUInt()) << "): ";
                                const Json::Value& words = value["words"];
                                for (Json::ArrayIndex i = 0; i < words.size() && i < 6; ++i)
                                {
                                    if (i > 0) out << ", ";
                                    out << Hex16(words[i]["value"].asUInt());
                                }
                            }
                            else if (aspect == "breakpoints")
                            {
                                out << "\n[breakpoints] " << value["count"].asUInt() << " active";
                            }
                            else if (aspect == "screen_digest" && value.isMember("digest"))
                            {
                                out << "\n[screen_digest] " << value["digest"].asString();
                            }
                            else
                            {
                                out << "\n[" << aspect << "] (see structuredContent)";
                            }
                        }
                        done(ToolResult::Ok(out.str(), std::move(acc)));
                    });
                });
        });
}

} // namespace

/// endregion </inspect_state>

/// region <type_input>

namespace
{

void RegisterTypeInput(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"type", "tap", "press", "release", "combo", "macro", "release_all", "status", "list_keys"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Input operation. 'type' sends text (auto-shift); 'tap' presses a key for N frames; 'combo' presses several keys at once.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["text"]["type"] = "string";
    schema["properties"]["text"]["description"] = "Text for 'type' (BASIC command when tokenized=true)";
    schema["properties"]["key"]["type"] = "string";
    schema["properties"]["key"]["description"] = "Single key name for tap/press/release (see list_keys)";
    schema["properties"]["keys"]["type"] = "array";
    schema["properties"]["keys"]["items"]["type"] = "string";
    schema["properties"]["keys"]["description"] = "Key names for 'combo'";
    schema["properties"]["name"]["type"] = "string";
    schema["properties"]["name"]["description"] = "Macro name for 'macro'";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["default"] = 2;
    schema["properties"]["frames"]["description"] = "Hold duration in frames for tap/combo";
    schema["properties"]["delay_frames"]["type"] = "integer";
    schema["properties"]["delay_frames"]["default"] = 2;
    schema["properties"]["delay_frames"]["description"] = "Inter-key delay for 'type'";
    schema["properties"]["tokenized"]["type"] = "boolean";
    schema["properties"]["tokenized"]["default"] = false;
    schema["properties"]["tokenized"]["description"] = "Type as tokenized BASIC keywords (cursor-accurate entry)";
    schema["required"].append("action");

    registry.Register(
        "type_input",
        "Send keyboard input to the emulator: type text or tokenized BASIC commands, tap/press/release keys, chords "
        "(combo), named macros, release_all for stuck keys. Use list_keys to discover key names.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            if (action == "status")
            {
                ResolveAndForward(args, "GET", "/keyboard/status", nullptr, caller, "Keyboard status", done);
                return;
            }
            if (action == "list_keys")
            {
                ResolveAndForward(args, "GET", "/keyboard/keys", nullptr, caller, "Known key names", done);
                return;
            }
            if (action == "release_all")
            {
                ResolveAndForward(args, "POST", "/keyboard/release_all", nullptr, caller, "Released all keys", done);
                return;
            }
            if (action == "type")
            {
                if (!args.isMember("text"))
                {
                    done(ToolResult::Error("type requires 'text'"));
                    return;
                }
                Json::Value body;
                body["text"] = args["text"].asString();
                if (args.isMember("delay_frames")) body["delay_frames"] = args["delay_frames"].asUInt();
                if (args.isMember("tokenized")) body["tokenized"] = args["tokenized"].asBool();
                ResolveAndForward(args, "POST", "/keyboard/type", &body, caller, "Text queued for typing", done);
                return;
            }
            if (action == "tap" || action == "press" || action == "release")
            {
                if (!args.isMember("key"))
                {
                    done(ToolResult::Error(action + " requires 'key'"));
                    return;
                }
                Json::Value body;
                body["key"] = args["key"].asString();
                if (action == "tap" && args.isMember("frames")) body["frames"] = args["frames"].asUInt();
                std::string suffix = action == "tap" ? "/keyboard/tap" : (action == "press" ? "/keyboard/press" : "/keyboard/release");
                ResolveAndForward(args, "POST", suffix, &body, caller, "Key " + action + ": " + args["key"].asString(), done);
                return;
            }
            if (action == "combo")
            {
                if (!args.isMember("keys") || !args["keys"].isArray() || args["keys"].size() == 0)
                {
                    done(ToolResult::Error("combo requires 'keys' array"));
                    return;
                }
                Json::Value body;
                body["keys"] = args["keys"];
                if (args.isMember("frames")) body["frames"] = args["frames"].asUInt();
                ResolveAndForward(args, "POST", "/keyboard/combo", &body, caller, "Key combo tapped", done);
                return;
            }
            if (action == "macro")
            {
                if (!args.isMember("name"))
                {
                    done(ToolResult::Error("macro requires 'name'"));
                    return;
                }
                Json::Value body;
                body["name"] = args["name"].asString();
                ResolveAndForward(args, "POST", "/keyboard/macro", &body, caller, "Macro executed", done);
                return;
            }

            done(ToolResult::Error("Unknown action '" + action + "'"));
        });
}

} // namespace

/// endregion </type_input>

/// region <Registry composition>

std::unique_ptr<ToolRegistry> BuildFullRegistry(IApiCaller::Ptr caller)
{
    auto registry = std::make_unique<ToolRegistry>();

    // Phase 1 — Core 5 smart tools
    RegisterEmulatorManage(*registry);
    RegisterLoadSoftware(*registry);
    RegisterControlExecution(*registry);
    RegisterInspectState(*registry);
    RegisterTypeInput(*registry);

    // Phase 2 — smart tools
    RegisterManageSymbols(*registry);
    RegisterDebugCode(*registry);
    RegisterAnalyzePerformance(*registry);
    RegisterCaptureMedia(*registry);

    // Router tools (search_api + invoke_api)
    RegisterRouterTools(*registry, std::move(caller));

    return registry;
}

/// endregion </Registry composition>

} // namespace mcp
