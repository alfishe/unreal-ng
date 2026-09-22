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
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    for (const char* action : {"create", "list", "list_models", "server", "status", "start", "stop", "pause", "resume", "reset", "destroy",
                               "gs_reset", "gs_reset_card", "gs_nmi", "gs_send_command", "gs_send_data", "gs_read_status", "gs_read_data",
                               "gs_switch_personality", "gs_dump_module"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Lifecycle operation. 'create' makes a new running instance (fails with a reason on models this build "
        "cannot create — no silent fallback); 'list' shows all instances with their machine identity; "
        "'list_models' enumerates hardware models with creatable flags; 'server' reports the build fingerprint "
        "and models_creatable; 'status' reports one instance's details. 'gs_*' actions drive the General Sound "
        "card over the same /control/audio/gs endpoint the WebAPI serves (gs_reset/gs_reset_card/gs_nmi/"
        "gs_send_command/gs_send_data/gs_read_status/gs_read_data; the byte actions need 'value'); "
        "'gs_switch_personality' swaps the LLE/LW card at the next frame boundary (needs 'personality': "
        "'z80'|'lle' or 'lw'|'lightweight'); 'gs_dump_module' writes the last completed COM30..D2 module "
        "upload to a file (optional 'path', defaults to 'gs-module-dump.mod').";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto' to reuse the single instance (auto-created when none exists)";
    schema["properties"]["model"]["type"] = "string";
    schema["properties"]["model"]["description"] =
        "Hardware model short name for 'create' — e.g. 48K, 128k, PLUS3, TSL, ATM3, ATM710, ATM450, PROFI, "
        "SCORPION, PROFSCORP, GMX, KAY, QUORUM, LSY256, PHOENIX (see list_models; creatability is "
        "build-dependent — check the 'creatable' flags before assuming a machine exists)";
    schema["properties"]["ram_size"]["type"] = "integer";
    schema["properties"]["ram_size"]["description"] = "Optional RAM size in KB for 'create' (e.g. 128, 256, 512)";
    schema["properties"]["value"]["type"] = "integer";
    schema["properties"]["value"]["description"] = "Byte value (0-255) required by gs_send_command and gs_send_data";
    schema["properties"]["personality"]["type"] = "string";
    schema["properties"]["personality"]["description"] =
        "Required by gs_switch_personality: 'z80'|'lle' for the Z80 coprocessor card, 'lw'|'lightweight' for the "
        "in-tree mod-player card";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "Optional file path for gs_dump_module (defaults to 'gs-module-dump.mod' in the server's working directory)";
    schema["required"].append("action");

    registry.Register(
        "emulator_manage",
        "Manage Unreal-NG emulator instances: create, list, switch models, start/stop/pause/resume/reset/destroy. "
        "Multi-instance: target identifies the machine; 'auto' reuses the single instance or creates a default 128k one. "
        "Also drives the General Sound card (gs_reset/gs_reset_card/gs_nmi/gs_send_command/gs_send_data/"
        "gs_read_status/gs_read_data/gs_switch_personality/gs_dump_module).",
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

            // Server-level status (parity with GET /api/v1/emulator/status):
            // build fingerprint + models_creatable. Same information the CLI
            // 'status' command prints and the WebAPI serves - one source.
            if (action == "server")
            {
                ForwardCall("GET", "/api/v1/emulator/status", nullptr, caller, "Server build fingerprint and creatable models", done);
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
                else if (action == "gs_reset" || action == "gs_reset_card" || action == "gs_nmi" ||
                         action == "gs_send_command" || action == "gs_send_data" ||
                         action == "gs_read_status" || action == "gs_read_data" ||
                         action == "gs_switch_personality" || action == "gs_dump_module")
                {
                    // GS card control (GS design §11.3): forwards to the same
                    // /control/audio/gs endpoint the WebAPI serves - the "gs_"
                    // prefix maps 1:1 onto the body action names
                    Json::Value body;
                    body["action"] = action.substr(3);
                    if (action == "gs_send_command" || action == "gs_send_data")
                    {
                        if (!args.isMember("value"))
                        {
                            done(ToolResult::Error("Action '" + action + "' requires 'value' (0-255)"));
                            return;
                        }
                        body["value"] = args["value"].asInt();
                    }
                    else if (action == "gs_switch_personality")
                    {
                        if (!args.isMember("personality") || !args["personality"].isString())
                        {
                            done(ToolResult::Error("Action '" + action + "' requires 'personality' (z80, lle, lw or lightweight)"));
                            return;
                        }
                        body["personality"] = args["personality"].asString();
                    }
                    else if (action == "gs_dump_module" && args.isMember("path") && args["path"].isString())
                    {
                        body["path"] = args["path"].asString();
                    }
                    caller.Call("POST", Endpoint(id, "/control/audio/gs"), &body, [action, done](int status, Json::Value response) {
                        if (status >= 200 && status < 300)
                        {
                            if (response.isMember("value"))
                            {
                                done(ToolResult::Ok("GS " + action.substr(3) + " -> " + std::to_string(response["value"].asInt()),
                                                    std::move(response)));
                            }
                            else if (action == "gs_switch_personality")
                            {
                                done(ToolResult::Ok("GS personality switch to '" + response.get("personality", "").asString() +
                                                        "' requested (" + response.get("note", "").asString() + ")",
                                                    std::move(response)));
                            }
                            else if (action == "gs_dump_module")
                            {
                                done(ToolResult::Ok("GS module dumped: " + std::to_string(response.get("bytes", 0).asUInt64()) +
                                                        " bytes -> " + response.get("path", "").asString(),
                                                    std::move(response)));
                            }
                            else
                            {
                                done(ToolResult::Ok("GS " + action.substr(3) + " done", std::move(response)));
                            }
                            return;
                        }
                        done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(response)));
                    });
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

// Try to read a local file. Returns true if successful, populates content.
bool TryReadLocalFile(const std::string& path, std::vector<uint8_t>& content)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    const auto size = file.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024) // Max 4MB
        return false;

    content.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(content.data()), size);
    return file.good();
}

// Extract just the filename from a path
std::string ExtractFilename(const std::string& path)
{
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

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
        "disk images (.trd .scl .fdi). The machine must be created first (target:'auto' handles that). "
        "If the path exists locally on the MCP host, the file is uploaded to the emulator automatically; "
        "otherwise, the path is passed to the emulator for direct loading.",
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

            // Try to read local file for piggybacking (MCP bridge uploads embedded content)
            auto fileContent = std::make_shared<std::vector<uint8_t>>();
            const bool isLocalFile = TryReadLocalFile(path, *fileContent);
            const std::string filename = ExtractFilename(path);

            TargetResolver::ResolveFromArgs(args, caller, [path, ext, isSnapshot, isTape, isDisk, play, drive, &caller, done,
                                                           isLocalFile, fileContent, filename](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;

                // Build headers for raw upload
                std::map<std::string, std::string> headers;
                headers["X-Filename"] = filename;

                if (isSnapshot)
                {
                    if (isLocalFile)
                    {
                        ForwardCallRaw("POST", Endpoint(id, "/snapshot/load"), *fileContent, headers, caller,
                                       "Loaded snapshot " + filename + " (uploaded) into " + id, done);
                    }
                    else
                    {
                        Json::Value body;
                        body["path"] = path;
                        ForwardCall("POST", Endpoint(id, "/snapshot/load"), &body, caller,
                                    "Loaded snapshot " + path + " into " + id, done);
                    }
                    return;
                }

                if (isTape)
                {
                    auto loadDone = [&caller, id, path, filename, play, done, isLocalFile](int status, Json::Value response) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("Tape load failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response)));
                            return;
                        }
                        std::string loadedName = isLocalFile ? filename + " (uploaded)" : path;
                        if (!play)
                        {
                            done(ToolResult::Ok("Loaded tape " + loadedName + " into " + id, std::move(response)));
                            return;
                        }
                        ForwardCall("POST", Endpoint(id, "/tape/play"), nullptr, caller,
                                    "Loaded tape " + loadedName + " and started playback on " + id, done);
                    };

                    if (isLocalFile)
                    {
                        caller.CallRaw("POST", Endpoint(id, "/tape/load"), *fileContent, headers, loadDone);
                    }
                    else
                    {
                        Json::Value body;
                        body["path"] = path;
                        caller.Call("POST", Endpoint(id, "/tape/load"), &body, loadDone);
                    }
                    return;
                }

                // Disk
                if (isLocalFile)
                {
                    ForwardCallRaw("POST", Endpoint(id, "/disk/" + drive + "/insert"), *fileContent, headers, caller,
                                   "Inserted disk " + filename + " (uploaded) into drive " + drive + " of " + id, done);
                }
                else
                {
                    Json::Value body;
                    body["path"] = path;
                    ForwardCall("POST", Endpoint(id, "/disk/" + drive + "/insert"), &body, caller,
                                "Inserted disk " + path + " into drive " + drive + " of " + id, done);
                }
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
    for (const char* aspect : {"machine", "registers", "memory", "memory_map", "disasm", "stack", "breakpoints", "memory_banks", "paging", "ports", "video",
                               "screen_ocr", "screen_image", "screen_digest", "timing", "rom", "audio_ay", "audio_fm", "audio_gs", "fdc", "mouse"})
    {
        allowed.append(aspect);
    }
    schema["properties"]["aspects"]["items"]["enum"] = allowed;
    schema["properties"]["aspects"]["default"] = Json::Value(Json::arrayValue);
    schema["properties"]["aspects"]["default"].append("registers");
    schema["properties"]["aspects"]["default"].append("disasm");
    schema["properties"]["aspects"]["default"].append("screen_ocr");
    schema["properties"]["aspects"]["description"] =
        "What to inspect. Default: registers + disasm + screen_ocr. 'stack' reads 32 bytes at SP; 'memory' needs address (hexdump default). "
        "'memory_map' = sparse non-zero block overview of the 64K address space or physical RAM banks (view=address|ram, TD-3). "
        "'paging' = tagged paging latches + bank table (P1-2 design), 'ports' = static port map with semantic tags, "
        "latch bindings and live routing flags, "
        "'audio_ay' = every AY/SSG chip fully decoded, 'audio_fm' = TurboSound FM board + both YM2203 FM halves (mode, timers, "
        "channels, operators, envelopes, key-on), 'audio_gs' = General Sound card (mailbox flags, MPAG page, DAC channels, "
        "coprocessor core; reports unavailable when the card is not fitted), 'fdc' = Beta Disk WD1793 registers, status, FSM, drives, 'mouse' = "
        "Kempston mouse state incl. port routing (fitted vs shadowed).";
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
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["default"] = "hexdump";
    schema["properties"]["format"]["description"] = "Read format for 'memory': 'hexdump' (default), 'full' (JSON byte array) or 'sparse' (fill-run segments)";
    schema["properties"]["view"]["type"] = "string";
    schema["properties"]["view"]["default"] = "address";
    schema["properties"]["view"]["description"] = "'memory_map' view: 'address' (64K CPU space) or 'ram' (physical RAM pages)";
    schema["properties"]["min_run"]["type"] = "integer";
    schema["properties"]["min_run"]["default"] = 64;
    schema["properties"]["min_run"]["description"] = "'memory_map' zero-run merge threshold (short zero runs fold into data blocks)";
    schema["properties"]["max_blocks"]["type"] = "integer";
    schema["properties"]["max_blocks"]["default"] = 48;
    schema["properties"]["max_blocks"]["description"] = "'memory_map' block budget before zero-run granularity coarsens";
    schema["properties"]["include_image"]["type"] = "boolean";
    schema["properties"]["include_image"]["default"] = false;
    schema["properties"]["include_image"]["description"] = "Include base64 image data for 'screen_image' (large payload)";

    registry.Register(
        "inspect_state",
        "Inspect emulator state in one call: registers, memory ranges, disassembly, stack words, breakpoints, memory banks, "
        "paging state (tagged latches + bank table), static port map with tags (ports), video mode (resolution, color depth, "
        "EFF7 state for Pentagon 16col/HWMC), screen OCR text, screen image metadata, screen digest hash, raster timing, "
        "ROM signatures, AY/SSG chips (audio_ay), TurboSound FM YM2203 halves (audio_fm), General Sound card (audio_gs), Beta Disk WD1793 (fdc), "
        "Kempston mouse + port routing (mouse). Combine aspects to reduce round-trips.",
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
                if (aspect != "machine" && aspect != "registers" && aspect != "memory" && aspect != "memory_map" && aspect != "disasm" && aspect != "stack" &&
                    aspect != "breakpoints" && aspect != "memory_banks" && aspect != "paging" && aspect != "ports" && aspect != "video" &&
                    aspect != "screen_ocr" && aspect != "screen_image" && aspect != "screen_digest" && aspect != "timing" && aspect != "rom" && aspect != "audio_ay" &&
                    aspect != "audio_fm" && aspect != "audio_gs" && aspect != "fdc" && aspect != "mouse")
                {
                    done(ToolResult::Error("Unknown aspect '" + aspect +
                                            "'. Valid: machine, registers, memory, memory_map, disasm, stack, breakpoints, memory_banks, paging, ports, "
                                            "screen_ocr, screen_image, screen_digest, timing, rom, audio_ay, audio_fm, audio_gs, fdc, mouse"));
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
            std::string format = args.isMember("format") ? args["format"].asString() : "hexdump";
            if (format != "hexdump" && format != "full" && format != "sparse") format = "hexdump";
            std::string view = args.isMember("view") ? args["view"].asString() : "address";
            if (view != "address" && view != "ram") view = "address";
            unsigned minRun = args.isMember("min_run") ? args["min_run"].asUInt() : 64u;
            if (minRun < 1) minRun = 1;
            unsigned maxBlocks = args.isMember("max_blocks") ? args["max_blocks"].asUInt() : 48u;
            if (maxBlocks < 1) maxBlocks = 1;

            TargetResolver::ResolveFromArgs(
                args, caller,
                [aspects, address, hasAddress, size, count, includeImage, format, view, minRun, maxBlocks, &caller, done, progress](
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
                            steps.push_back([&caller, id, aspect, address, size, format](Json::Value& acc, std::function<void(bool)> next) {
                                std::string path = Endpoint(id, "/memory/" + std::to_string(address)) + "?len=" + std::to_string(size) +
                                                   "&format=" + format;
                                caller.Call("GET", path, nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "memory_map")
                        {
                            steps.push_back([&caller, id, aspect, view, minRun, maxBlocks](Json::Value& acc, std::function<void(bool)> next) {
                                std::string path = Endpoint(id, "/memory/map") + "?view=" + view + "&min_run=" + std::to_string(minRun) +
                                                   "&max_blocks=" + std::to_string(maxBlocks);
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
                                    // format=full: the stack aspect machine-parses the byte
                                    // array; the TD-3 hexdump default would drop data[]
                                    std::string path = Endpoint(id, "/memory/" + std::to_string(sp)) + "?len=32&format=full";
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
                        else if (aspect == "video")
                        {
                            // Video mode: resolution, color depth, EFF7 state for Pentagon 16col/HWMC modes
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/screen/mode"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
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
                        else if (aspect == "rom")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/memory/rom"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "fdc")
                        {
                            // Core DeviceState::Fdc via the WebAPI; 404 = no Beta Disk on this machine
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/fdc"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "mouse")
                        {
                            // /mouse/status: fitment + counters + the routing answer (fitted vs
                            // shadowed by TR-DOS / registered peripherals - design Q4)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/mouse/status"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "paging")
                        {
                            // /state/paging: tagged paging latches + bank table (P1-2 design)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/paging"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "ports")
                        {
                            // /ports: static port map with semantic tags + latch bindings
                            // and the live routing flags (P1-5 + tagged registry)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/ports"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "audio_ay" || aspect == "audio_fm")
                        {
                            // Overview first, then every chip's full report (core DeviceState::AyChip / FmChip)
                            const std::string base = aspect == "audio_ay" ? "/state/audio/ay" : "/state/audio/fm";
                            steps.push_back([&caller, id, aspect, base](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, base), nullptr, [&caller, id, aspect, base, &acc, next](int status, Json::Value body) mutable {
                                    if (status != 200)
                                    {
                                        acc[aspect] = Json::Value(Json::objectValue);
                                        acc[aspect]["available"] = false;
                                        acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable");
                                        next(true);
                                        return;
                                    }
                                    Json::Value overview = std::move(body);
                                    const unsigned count = overview.isMember("chips") && overview["chips"].isArray() ? overview["chips"].size() : 0u;
                                    acc[aspect] = std::move(overview);
                                    acc[aspect]["chip_details"] = Json::Value(Json::arrayValue);
                                    // Fetch chips sequentially (0..count-1)
                                    auto fetch = std::make_shared<std::function<void(unsigned)>>();
                                    *fetch = [&caller, id, aspect, base, &acc, next, count, fetch](unsigned index) {
                                        if (index >= count) { next(true); return; }
                                        caller.Call("GET", Endpoint(id, base + "/" + std::to_string(index)), nullptr,
                                                    [aspect, &acc, index, fetch](int st, Json::Value detail) mutable {
                                                        if (st == 200) acc[aspect]["chip_details"].append(std::move(detail));
                                                        (*fetch)(index + 1);
                                                    });
                                    };
                                    (*fetch)(0);
                                });
                            });
                        }
                        else if (aspect == "audio_gs")
                        {
                            // GS card state via the WebAPI (GS design §10.1);
                            // 404 = no GS fitted ([SOUND] GSType selects the card)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/audio/gs"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
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
                                out << "\n[memory] " << value["length"].asUInt() << " bytes at " << Hex16(value["address"].asUInt()) << ": ";
                                if (value.isMember("hexdump"))
                                {
                                    // First two hexdump lines (32 bytes) keep the summary compact
                                    const std::string dump = value["hexdump"].asString();
                                    size_t cut = dump.find('\n');
                                    cut = cut == std::string::npos ? dump.size() : dump.find('\n', cut + 1);
                                    out << dump.substr(0, cut == std::string::npos ? dump.size() : cut);
                                }
                                else if (value.isMember("segments"))
                                {
                                    out << value["segments"].size() << " sparse segment(s), "
                                        << value["non_zero"].asUInt() << " non-zero bytes";
                                }
                                else
                                {
                                    out << value["hex"].asString().substr(0, 96);
                                }
                            }
                            else if (aspect == "memory_map" && value.isMember("blocks"))
                            {
                                out << "\n[memory_map] " << value["model"].asString() << " " << value["view"].asString()
                                    << " view: " << value["block_count"].asUInt() << " block(s), "
                                    << value["non_zero_bytes"].asUInt() << "/" << value["total_size"].asUInt() << " non-zero bytes";
                                const Json::Value& blocks = value["blocks"];
                                for (Json::ArrayIndex i = 0; i < blocks.size() && i < 8; ++i)
                                {
                                    out << "\n  " << blocks[i]["address"].asString() << " " << blocks[i]["type"].asString() << " "
                                        << blocks[i]["status"].asString() << ", size " << blocks[i]["size"].asUInt()
                                        << ", non_zero " << blocks[i]["non_zero"].asUInt();
                                    if (!blocks[i]["hash"].asString().empty())
                                        out << ", hash " << blocks[i]["hash"].asString().substr(0, 8);
                                }
                                if (blocks.size() > 8)
                                    out << "\n  ... " << (blocks.size() - 8) << " more block(s)";
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
                            else if (aspect == "fdc")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[fdc] " << value["description"].asString();
                                else
                                {
                                    out << "\n[fdc] " << value["fsm_state"].asString() << ", last " << value["last_command"].asString()
                                        << ", status " << value["registers"]["status"].asUInt() << " track " << value["registers"]["track"].asUInt()
                                        << " sector " << value["registers"]["sector"].asUInt() << ", drive " << value["selected_drive"].asUInt()
                                        << " side " << value["side"].asUInt() << ", " << value["density"].asString();
                                    const Json::Value& drives = value["drives"];
                                    for (Json::ArrayIndex i = 0; i < drives.size(); ++i)
                                        if (drives[i]["present"].asBool() && drives[i]["inserted"].asBool())
                                            out << "\n  " << drives[i]["letter"].asString() << ": " << drives[i]["path"].asString()
                                                << " track " << drives[i]["track"].asInt() << (drives[i]["motor_on"].asBool() ? " motor on" : "")
                                                << (drives[i]["write_protected"].asBool() ? " wp" : "");
                                }
                            }
                            else if (aspect == "audio_ay")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[audio_ay] " << value["description"].asString();
                                else
                                {
                                    out << "\n[audio_ay] " << value["description"].asString();
                                    const Json::Value& chips = value["chip_details"];
                                    for (Json::ArrayIndex i = 0; i < chips.size(); ++i)
                                    {
                                        const Json::Value& ch = chips[i]["channels"];
                                        out << "\n  chip " << i << ":";
                                        for (Json::ArrayIndex c = 0; c < ch.size(); ++c)
                                            out << " " << ch[c]["name"].asString() << "=" << ch[c]["volume"].asUInt()
                                                << (ch[c]["tone_enabled"].asBool() ? "T" : "") << (ch[c]["noise_enabled"].asBool() ? "N" : "")
                                                << (ch[c]["envelope_enabled"].asBool() ? "E" : "") << "@" << int(ch[c]["frequency_hz"].asDouble()) << "Hz";
                                    }
                                }
                            }
                            else if (aspect == "audio_gs")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[audio_gs] " << value["description"].asString();
                                else
                                {
                                    out << "\n[audio_gs] " << value["device"].asString()
                                        << ", page " << value["page"].asUInt() << ", ram " << value["ram_kb"].asUInt() << " KB"
                                        << (value["rom_loaded"].asBool() ? ", rom ok" : ", rom missing")
                                        << (value["command_pending"].asBool() ? ", cmd pending" : "")
                                        << (value["data_pending"].asBool() ? ", data pending" : "");
                                    const Json::Value& gsChannels = value["channels"];
                                    for (Json::ArrayIndex i = 0; i < gsChannels.size(); ++i)
                                        out << "\n  ch" << i << ": sample " << gsChannels[i]["sample"].asUInt()
                                            << " vol " << gsChannels[i]["volume"].asUInt();
                                }
                            }
                            else if (aspect == "mouse")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[mouse] " << value["description"].asString();
                                else
                                {
                                    out << "\n[mouse] " << (value["present"].asBool() ? "fitted" : "not fitted")
                                        << ", x " << value["x"].asInt() << " y " << value["y"].asInt()
                                        << (value["wheel_enabled"].asBool() ? ", wheel" : "");
                                    if (value.isMember("routing"))
                                        out << ", ports " << (value["routing"]["ports_decoded"].asBool() ? "decoded" : "shadowed")
                                            << " (" << value["routing"]["note"].asString() << ")";
                                }
                            }
                            else if (aspect == "paging")
                            {
                                out << "\n[paging] model " << value["model"].asString()
                                    << ", locked " << (value["paging_locked"].asBool() ? "yes" : "no")
                                    << ", trdos " << (value["trdos_active"].asBool() ? "active" : "inactive");
                                const Json::Value& latches = value["latches"];
                                for (Json::ArrayIndex i = 0; i < latches.size(); ++i)
                                {
                                    out << "\n  " << latches[i]["latch"].asString() << "=" << latches[i]["value"].asString();
                                    if (latches[i].isMember("decoded"))
                                    {
                                        const Json::Value& d = latches[i]["decoded"];
                                        for (auto it = d.begin(); it != d.end(); ++it)
                                            out << " " << it.name() << "=" << ((*it).isBool() ? ((*it).asBool() ? "1" : "0") : (*it).asString());
                                    }
                                }
                                const Json::Value& banks = value["banks"];
                                for (Json::ArrayIndex i = 0; i < banks.size(); ++i)
                                    out << "\n  bank" << banks[i]["bank"].asInt() << " " << banks[i]["address_range"].asString()
                                        << " " << banks[i]["type"].asString() << " p" << banks[i]["page"].asInt();
                            }
                            else if (aspect == "ports" && value["entries"].isArray())
                            {
                                out << "\n[ports] model " << value["model"].asString();
                                const Json::Value& portEntries = value["entries"];
                                for (Json::ArrayIndex i = 0; i < portEntries.size(); ++i)
                                {
                                    out << "\n  " << portEntries[i]["port"].asString() << " " << portEntries[i]["device"].asString();
                                    const Json::Value& tagNames = portEntries[i]["tags"];
                                    if (tagNames.isArray() && tagNames.size() > 0)
                                    {
                                        out << " [";
                                        for (Json::ArrayIndex t = 0; t < tagNames.size(); ++t)
                                            out << (t > 0 ? "," : "") << tagNames[t].asString();
                                        out << "]";
                                    }
                                    if (!portEntries[i]["latch"].isNull())
                                        out << " latch=" << portEntries[i]["latch"].asString();
                                    if (!portEntries[i]["gate"].isNull())
                                        out << " gate: " << portEntries[i]["gate"].asString();
                                }
                                if (value.isMember("live"))
                                {
                                    const Json::Value& live = value["live"];
                                    out << "\n  live: trdos " << (live["trdos_active"].asBool() ? "active" : "inactive")
                                        << ", mouse ports " << (live["mouse_ports_decoded"].asBool() ? "decoded" : "shadowed");
                                }
                            }
                            else if (aspect == "audio_fm")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[audio_fm] " << value["description"].asString();
                                else
                                {
                                    out << "\n[audio_fm] board chip " << value["board"]["selected_chip"].asUInt()
                                        << (value["board"]["fm_enabled"].asBool() ? ", FM on" : ", FM muted");
                                    const Json::Value& chips = value["chip_details"];
                                    for (Json::ArrayIndex i = 0; i < chips.size(); ++i)
                                    {
                                        out << "\n  chip " << i << ": ch3 " << chips[i]["mode"]["channel3_mode"].asString()
                                            << ", keyed " << chips[i]["keyed_channels"].asUInt() << ", sounding " << chips[i]["sounding_channels"].asUInt();
                                        const Json::Value& ch = chips[i]["channels"];
                                        for (Json::ArrayIndex c = 0; c < ch.size(); ++c)
                                            if (ch[c]["sounding"].asBool() || ch[c]["key_on"].asBool())
                                                out << "\n    ch" << c << (ch[c]["key_on"].asBool() ? " key-on" : " releasing") << " mask " << ch[c]["key_on_mask"].asUInt()
                                                    << " alg " << ch[c]["algorithm"].asUInt() << " " << ch[c]["frequency_hz"].asDouble() << " Hz";
                                    }
                                }
                            }
                            else if (aspect == "rom" && value.isMember("pages"))
                            {
                                out << "\n[rom] " << value["pages"].size() << " page(s)";
                                const Json::Value& pages = value["pages"];
                                for (Json::ArrayIndex i = 0; i < pages.size() && i < 4; ++i)
                                {
                                    const Json::Value& page = pages[i];
                                    out << "\n  [" << i << "] ";
                                    if (page.isMember("title") && !page["title"].asString().empty())
                                        out << page["title"].asString();
                                    else
                                        out << "(unknown)";
                                    if (page.isMember("sha256"))
                                        out << " sha256:" << page["sha256"].asString().substr(0, 16) << "...";
                                }
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

/// region <mouse_input>

namespace
{

void RegisterMouseInput(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"move", "press", "release", "click", "buttons", "wheel", "release_all", "status"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Kempston mouse (relative device). 'move' shifts counters by dx/dy emulated pixels; 'click' presses a button for N "
        "frames. Input is applied immediately; call control_execution run_frames to let the program react.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["dx"]["type"] = "integer";
    schema["properties"]["dx"]["minimum"] = -127;
    schema["properties"]["dx"]["maximum"] = 127;
    schema["properties"]["dx"]["description"] = "+ = right (move; optional pre-move for click)";
    schema["properties"]["dy"]["type"] = "integer";
    schema["properties"]["dy"]["minimum"] = -127;
    schema["properties"]["dy"]["maximum"] = 127;
    schema["properties"]["dy"]["description"] = "+ = UP (move; optional pre-move for click)";
    Json::Value buttonEnum(Json::arrayValue);
    for (const char* button : {"left", "right", "middle"})
    {
        buttonEnum.append(button);
    }
    schema["properties"]["button"]["type"] = "string";
    schema["properties"]["button"]["enum"] = buttonEnum;
    schema["properties"]["pressed"]["type"] = "array";
    schema["properties"]["pressed"]["items"]["type"] = "string";
    schema["properties"]["pressed"]["items"]["enum"] = buttonEnum;
    schema["properties"]["pressed"]["description"] = "Exact pressed set for 'buttons' ([] = none)";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["minimum"] = 1;
    schema["properties"]["frames"]["default"] = 2;
    schema["properties"]["frames"]["description"] = "Hold time for 'click'";
    schema["properties"]["steps"]["type"] = "integer";
    schema["properties"]["steps"]["minimum"] = -7;
    schema["properties"]["steps"]["maximum"] = 7;
    schema["properties"]["steps"]["description"] = "Wheel notches, + = away from user";
    schema["required"].append("action");

    registry.Register(
        "mouse_input",
        "Send Kempston mouse input to the emulator: relative move (dx/dy), press/release/click buttons, set the exact "
        "pressed set, wheel steps, release_all, status. Values are forwarded as-is; the WebAPI validates ranges.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            if (action == "status")
            {
                ResolveAndForward(args, "GET", "/mouse/status", nullptr, caller, "Mouse status", done);
                return;
            }
            if (action == "release_all")
            {
                ResolveAndForward(args, "POST", "/mouse/release_all", nullptr, caller, "Released all mouse buttons", done);
                return;
            }
            if (action == "move")
            {
                if (!args.isMember("dx") && !args.isMember("dy"))
                {
                    done(ToolResult::Error("move requires 'dx' or 'dy'"));
                    return;
                }
                Json::Value body;
                body["dx"] = args.isMember("dx") ? args["dx"] : Json::Value(0);
                body["dy"] = args.isMember("dy") ? args["dy"] : Json::Value(0);
                ResolveAndForward(args, "POST", "/mouse/move", &body, caller, "Mouse moved", done);
                return;
            }
            if (action == "press" || action == "release")
            {
                if (!args.isMember("button"))
                {
                    done(ToolResult::Error(action + " requires 'button'"));
                    return;
                }
                Json::Value body;
                body["button"] = args["button"];
                ResolveAndForward(args, "POST", "/mouse/" + action, &body, caller,
                                  "Mouse " + action + ": " + args["button"].asString(), done);
                return;
            }
            if (action == "buttons")
            {
                if (!args.isMember("pressed"))
                {
                    done(ToolResult::Error("buttons requires 'pressed'"));
                    return;
                }
                Json::Value body;
                body["pressed"] = args["pressed"];
                ResolveAndForward(args, "POST", "/mouse/buttons", &body, caller, "Mouse buttons set", done);
                return;
            }
            if (action == "wheel")
            {
                if (!args.isMember("steps"))
                {
                    done(ToolResult::Error("wheel requires 'steps'"));
                    return;
                }
                Json::Value body;
                body["steps"] = args["steps"];
                ResolveAndForward(args, "POST", "/mouse/wheel", &body, caller, "Mouse wheel moved", done);
                return;
            }
            if (action == "click")
            {
                if (!args.isMember("button"))
                {
                    done(ToolResult::Error("click requires 'button'"));
                    return;
                }
                Json::Value clickBody;
                clickBody["button"] = args["button"];
                if (args.isMember("frames")) clickBody["frames"] = args["frames"];
                const std::string okText = "Mouse click: " + args["button"].asString();

                if (!args.isMember("dx") && !args.isMember("dy"))
                {
                    ResolveAndForward(args, "POST", "/mouse/click", &clickBody, caller, okText, done);
                    return;
                }

                // Pre-move then click, in order; the click is skipped if the move fails
                Json::Value moveBody;
                moveBody["dx"] = args.isMember("dx") ? args["dx"] : Json::Value(0);
                moveBody["dy"] = args.isMember("dy") ? args["dy"] : Json::Value(0);

                TargetResolver::ResolveFromArgs(
                    args, caller, [&caller, moveBody, clickBody, okText, done](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        const std::string id = idOrError;

                        // Each step records its response under its name; a failure records
                        // the HTTP status and body under "failed" and stops the series.
                        auto makeStep = [&caller, id](const std::string& name, const std::string& suffix, Json::Value body) -> SeriesStep {
                            auto bodyHolder = std::make_shared<Json::Value>(std::move(body));
                            return [&caller, id, name, suffix, bodyHolder](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("POST", Endpoint(id, suffix), bodyHolder.get(), [bodyHolder, name, &acc, next](int status, Json::Value response) {
                                    if (status >= 200 && status < 300)
                                    {
                                        acc[name] = std::move(response);
                                        next(true);
                                        return;
                                    }
                                    acc["failed"]["step"] = name;
                                    acc["failed"]["status"] = status;
                                    acc["failed"]["body"] = std::move(response);
                                    next(false);
                                });
                            };
                        };

                        std::vector<SeriesStep> steps;
                        steps.push_back(makeStep("move", "/mouse/move", moveBody));
                        steps.push_back(makeStep("click", "/mouse/click", clickBody));

                        RunSeries(std::move(steps), [done, okText, id](Json::Value acc) {
                            if (acc.isMember("failed"))
                            {
                                const Json::Value& failed = acc["failed"];
                                const int status = failed["status"].asInt();
                                if (status == 0)
                                {
                                    done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                                    return;
                                }
                                std::string text = "Mouse " + failed["step"].asString() + " failed: WebAPI returned HTTP " +
                                                   std::to_string(status);
                                const std::string details = DescribeErrorBody(failed["body"]);
                                if (!details.empty())
                                {
                                    text += ": " + details;
                                }
                                done(ToolResult::Error(text));
                                return;
                            }
                            done(ToolResult::Ok(okText + " (after pre-move) [target " + id + "]", std::move(acc)));
                        });
                    });
                return;
            }

            done(ToolResult::Error("Unknown action '" + action + "'"));
        });
}

} // namespace

/// endregion </mouse_input>

/// region <time_travel>

namespace
{

/// Sparkline representation for coverage summary heatmap display.
static std::string MakeSparkline(const std::vector<uint32_t>& values)
{
    static const char* kBars[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    if (values.empty()) return "";
    uint32_t maxVal = *std::max_element(values.begin(), values.end());
    if (maxVal == 0) maxVal = 1;
    std::string spark;
    for (uint32_t v : values)
    {
        size_t idx = static_cast<size_t>((static_cast<uint64_t>(v) * 8) / maxVal);
        if (idx > 8) idx = 8;
        spark += kBars[idx];
    }
    return spark;
}

/// Percent-encodes a path segment (RFC 3986 unreserved characters kept
/// literal). Labels are free-form text ("umt entry"), so they must not be
/// spliced raw into a URL path.
std::string UrlEncodeSegment(const std::string& text)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size());
    for (char c : text)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += c;
        }
        else
        {
            encoded += '%';
            encoded += kHex[uc >> 4];
            encoded += kHex[uc & 0xF];
        }
    }
    return encoded;
}

void RegisterTimeTravel(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"status", "bookmark_add", "bookmark_list", "bookmark_delete", "seek_bookmark",
                               "coverage_probe", "coverage_scan", "coverage_summary"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "'status' reports the TTD session; "
        "'bookmark_add'/'bookmark_list'/'bookmark_delete'/'seek_bookmark' manage advisory bookmarks; "
        "'coverage_probe' checks if a frame touched an address range; "
        "'coverage_scan' lists frames in a range touching an address range; "
        "'coverage_summary' returns bucketed address activity heatmaps";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto' to reuse the single instance";
    schema["properties"]["label"]["type"] = "string";
    schema["properties"]["label"]["description"] =
        "Bookmark label for bookmark_add / bookmark_delete / seek_bookmark";
    schema["properties"]["frame"]["type"] = "integer";
    schema["properties"]["frame"]["description"] = "Frame number for bookmark_add or coverage_probe";
    schema["properties"]["from_frame"]["type"] = "integer";
    schema["properties"]["from_frame"]["description"] = "Starting frame for coverage_scan / coverage_summary";
    schema["properties"]["to_frame"]["type"] = "integer";
    schema["properties"]["to_frame"]["description"] = "Ending frame for coverage_scan / coverage_summary";
    schema["properties"]["kind"]["type"] = "string";
    schema["properties"]["kind"]["description"] = "Coverage kind: 'executed', 'written', or 'read'";
    schema["properties"]["addr_from"]["type"] = "string";
    schema["properties"]["addr_from"]["description"] = "Start Z80 address for coverage query (e.g. '0xBF00' or 48896)";
    schema["properties"]["addr_to"]["type"] = "string";
    schema["properties"]["addr_to"]["description"] = "End Z80 address for coverage query (e.g. '0xBFFF' or 49151)";
    schema["properties"]["phys_page"]["type"] = "integer";
    schema["properties"]["phys_page"]["description"] = "Optional physical page index (0..255) for coverage query";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["description"] = "Max results for coverage_scan (default 200) or max buckets for coverage_summary (default 100)";
    schema["properties"]["bucket_size"]["type"] = "integer";
    schema["properties"]["bucket_size"]["description"] = "Frames per bucket for coverage_summary (default: auto)";
    schema["properties"]["tinframe"]["type"] = "integer";
    schema["properties"]["tinframe"]["default"] = 0;
    schema["properties"]["tinframe"]["description"] = "Optional T-states within the frame for bookmark_add";
    schema["required"].append("action");

    registry.Register(
        "time_travel",
        "Time-travel debugging (TTD): session status, agent bookmarks, and TTD coverage index queries.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();

            if (action == "bookmark_add" || action == "bookmark_delete" || action == "seek_bookmark")
            {
                const std::string label = args["label"].asString();
                if (label.empty())
                {
                    done(ToolResult::Error("Action '" + action + "' requires a non-empty 'label'"));
                    return;
                }
            }

            auto forward = [&caller, &args, action, done](const std::string& id) {
                if (action == "status")
                {
                    ForwardCall("GET", Endpoint(id, "/ttd/status"), nullptr, caller, "TTD status of " + id, done);
                }
                else if (action == "bookmark_add")
                {
                    Json::Value body;
                    body["label"] = args["label"].asString();
                    if (args.isMember("frame"))
                    {
                        body["frame"] = args["frame"];
                    }
                    if (args.isMember("tinframe"))
                    {
                        body["tinframe"] = args["tinframe"];
                    }
                    ForwardCall("POST", Endpoint(id, "/ttd/bookmarks"), &body, caller,
                                "Bookmark '" + args["label"].asString() + "' added on " + id, done);
                }
                else if (action == "bookmark_list")
                {
                    caller.Call("GET", Endpoint(id, "/ttd/bookmarks"), nullptr, [done](int status, Json::Value body) {
                        if (status != 200)
                        {
                            done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(body)));
                            return;
                        }
                        const Json::Value& bookmarks = body["bookmarks"];
                        std::ostringstream out;
                        out << bookmarks.size() << " bookmark(s)";
                        for (Json::ArrayIndex i = 0; i < bookmarks.size(); ++i)
                        {
                            out << "\n- '" << bookmarks[i]["label"].asString() << "' @ frame "
                                << bookmarks[i]["frame"].asUInt64();
                            if (bookmarks[i]["tinframe"].asUInt() != 0)
                            {
                                out << " t=" << bookmarks[i]["tinframe"].asUInt();
                            }
                        }
                        out << "\n(advisory — never a replay barrier)";
                        done(ToolResult::Ok(out.str(), std::move(body)));
                    });
                }
                else if (action == "bookmark_delete")
                {
                    ForwardCall("DELETE", Endpoint(id, "/ttd/bookmarks/" + UrlEncodeSegment(args["label"].asString())),
                                nullptr, caller, "Bookmark '" + args["label"].asString() + "' removed from " + id, done);
                }
                else if (action == "seek_bookmark")
                {
                    Json::Value body;
                    body["bookmark"] = args["label"].asString();
                    ForwardCall("POST", Endpoint(id, "/ttd/seek"), &body, caller,
                                "Seek to bookmark '" + args["label"].asString() + "' on " + id, done);
                }
                else if (action == "coverage_probe")
                {
                    std::string query = "/ttd/coverage/probe?";
                    if (args.isMember("frame")) query += "frame=" + std::to_string(args["frame"].asUInt64()) + "&";
                    if (args.isMember("kind")) query += "kind=" + UrlEncodeSegment(args["kind"].asString()) + "&";
                    if (args.isMember("addr_from")) query += "addr_from=" + UrlEncodeSegment(args["addr_from"].asString()) + "&";
                    if (args.isMember("addr_to")) query += "addr_to=" + UrlEncodeSegment(args["addr_to"].asString()) + "&";
                    if (args.isMember("phys_page")) query += "phys_page=" + std::to_string(args["phys_page"].asUInt()) + "&";
                    if (query.back() == '&' || query.back() == '?') query.pop_back();

                    caller.Call("GET", Endpoint(id, query), nullptr, [done](int status, Json::Value body) {
                        if (status != 200) {
                            done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(body)));
                            return;
                        }
                        bool available = body["index_available"].asBool();
                        bool touched = body["touched"].asBool();
                        std::string msg = available ? (touched ? "Touched: TRUE" : "Touched: FALSE")
                                                    : "Index not available for this session";
                        done(ToolResult::Ok(msg, std::move(body)));
                    });
                }
                else if (action == "coverage_scan")
                {
                    std::string query = "/ttd/coverage/scan?";
                    if (args.isMember("from_frame")) query += "from_frame=" + std::to_string(args["from_frame"].asUInt64()) + "&";
                    if (args.isMember("to_frame")) query += "to_frame=" + std::to_string(args["to_frame"].asUInt64()) + "&";
                    if (args.isMember("kind")) query += "kind=" + UrlEncodeSegment(args["kind"].asString()) + "&";
                    if (args.isMember("addr_from")) query += "addr_from=" + UrlEncodeSegment(args["addr_from"].asString()) + "&";
                    if (args.isMember("addr_to")) query += "addr_to=" + UrlEncodeSegment(args["addr_to"].asString()) + "&";
                    if (args.isMember("phys_page")) query += "phys_page=" + std::to_string(args["phys_page"].asUInt()) + "&";
                    if (args.isMember("limit")) query += "limit=" + std::to_string(args["limit"].asUInt()) + "&";
                    if (query.back() == '&' || query.back() == '?') query.pop_back();

                    caller.Call("GET", Endpoint(id, query), nullptr, [done](int status, Json::Value body) {
                        if (status != 200) {
                            done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(body)));
                            return;
                        }
                        if (!body["index_available"].asBool()) {
                            done(ToolResult::Ok("Index not available for this session", std::move(body)));
                            return;
                        }
                        std::ostringstream out;
                        out << "Matched " << body["matching_frames"].asUInt64() << " / " << body["scanned_frames"].asUInt64()
                            << " scanned frames (first=" << body["first_match"].asUInt64() << ", last=" << body["last_match"].asUInt64() << ")";
                        if (body.isMember("covered_from")) {
                            out << " [index covers " << body["covered_from"].asUInt64() << ".." << body["covered_to"].asUInt64() << "]";
                        }
                        if (body["truncated"].asBool()) out << " [TRUNCATED]";
                        done(ToolResult::Ok(out.str(), std::move(body)));
                    });
                }
                else if (action == "coverage_summary")
                {
                    std::string query = "/ttd/coverage/summary?";
                    if (args.isMember("from_frame")) query += "from_frame=" + std::to_string(args["from_frame"].asUInt64()) + "&";
                    if (args.isMember("to_frame")) query += "to_frame=" + std::to_string(args["to_frame"].asUInt64()) + "&";
                    if (args.isMember("kind")) query += "kind=" + UrlEncodeSegment(args["kind"].asString()) + "&";
                    if (args.isMember("bucket_size")) query += "bucket_size=" + std::to_string(args["bucket_size"].asUInt64()) + "&";
                    if (args.isMember("limit")) query += "limit=" + std::to_string(args["limit"].asUInt()) + "&";
                    if (query.back() == '&' || query.back() == '?') query.pop_back();

                    caller.Call("GET", Endpoint(id, query), nullptr, [done](int status, Json::Value body) {
                        if (status != 200) {
                            done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(body)));
                            return;
                        }
                        if (!body["index_available"].asBool()) {
                            done(ToolResult::Ok("Coverage index not available for this session", std::move(body)));
                            return;
                        }
                        const Json::Value& buckets = body["buckets"];
                        std::vector<uint32_t> execVals, writeVals, readVals;
                        for (const auto& b : buckets) {
                            execVals.push_back(b["executed_distinct"].asUInt());
                            writeVals.push_back(b["written_distinct"].asUInt());
                            readVals.push_back(b["read_distinct"].asUInt());
                        }
                        std::ostringstream out;
                        out << "Coverage summary (" << body["from_frame"].asUInt64() << ".." << body["to_frame"].asUInt64();
                        if (body.isMember("covered_from")) {
                            out << ", index covers " << body["covered_from"].asUInt64() << ".." << body["covered_to"].asUInt64();
                        }
                        out << ", bucket_size=" << body["bucket_size"].asUInt64() << ", buckets=" << buckets.size() << "):\n";
                        out << "  exec  " << MakeSparkline(execVals) << "\n";
                        out << "  write " << MakeSparkline(writeVals) << "\n";
                        out << "  read  " << MakeSparkline(readVals);
                        done(ToolResult::Ok(out.str(), std::move(body)));
                    });
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

/// endregion </time_travel>

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
    RegisterMouseInput(*registry);

    // TD-4 — time_travel (status + agent bookmarks; seed of the full TTD tool)
    RegisterTimeTravel(*registry);

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
