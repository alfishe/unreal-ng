// MCP smart tools — registry core + Phase 1 "Core 5" tools
//
// Tools orchestrate existing WebAPI endpoints over the loopback IApiCaller:
//   1. emulator_manage    — lifecycle: create/list/start/stop/pause/resume/reset/destroy
//   2. load_software      — auto-detect .sna/.z80/.szx/.spg/.rzx, tapes (.tap/.tzx/...), .trd/.scl/.fdi and load
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
#include "mcp-slots.h"
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

/// A slot change reply (WebAPI /slots/{slot}/{verb}, SlotControl) as a tool result: the status and message, the
/// plan's lines (what it removes, shadows, loses) and the restart (the new emulator id). Refusals are errors that
/// carry the whole plan, so the caller sees every card a replaceIfIncompatible would remove
ToolResult SlotReplyResult(int status, Json::Value response, const std::string& what = "slots")
{
    if (status == 0)
        return ToolResult::Error("WebAPI unreachable - is the emulator running with WebAPI enabled (port 8090)?");
    std::string text = what + ": " + response.get("status", "").asString();
    const std::string message = response.get("message", "").asString();
    if (!message.empty())
        text += ": " + message;
    for (const Json::Value& line : response["plan"]["lines"])
        text += "\n  " + line.asString();
    const Json::Value& restart = response["restart"];
    if (restart.get("restarted", false).asBool())
        text += "\nrestarted: emulator " + restart.get("previousEmulatorId", "").asString() + " -> " +
                restart.get("emulatorId", "").asString() + (restart.get("started", false).asBool() ? " (running)" : "");
    for (const Json::Value& line : response["media"]["lines"])
        text += "\n  media: " + line.asString();
    const std::string networkNote = response["network"].get("note", "").asString();
    if (!networkNote.empty())
        text += "\nnetwork: " + networkNote;
    if (status >= 200 && status < 300)
        return ToolResult::Ok(std::move(text), std::move(response));
    return ToolResult::Error(text);
}

void RegisterEmulatorManage(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"create", "switch_model", "list", "list_models", "server", "status", "zxpoly_status", "start", "stop", "pause", "resume", "reset", "destroy",
                               "transfer_state",
                               "gs_reset", "gs_reset_card", "gs_nmi", "gs_send_command", "gs_send_data", "gs_read_status", "gs_read_data",
                               "gs_switch_personality", "gs_dump_module", "gs_sd_insert", "gs_sd_eject", "gs_flash_save",
                               "gs_stereo_mode",
                               "rom_flash_status", "rom_flash_save", "rom_flash_discard",
                               "slots_catalog", "slots_matrix", "slots_plug", "slots_remove", "slots_set",
                               "network_configure"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Lifecycle operation. 'create' makes a new running instance (fails with a reason on models this build "
        "cannot create — no silent fallback); 'list' shows all instances with their machine identity; "
        "'list_models' enumerates hardware models with creatable flags; 'server' reports the build fingerprint "
        "and models_creatable; 'status' reports one instance's details. 'switch_model' replaces the target with "
        "a new instance of 'model' (new id; the machine state is lost, the media follow into the same slots with "
        "their unsaved writes; media with unsaved writes the new model has no slot for need 'stranded'; a ZX-Poly "
        "configuration name as 'model' switches to that machine). 'transfer_state' copies the target's running "
        "state in memory into another instance: 'to' names an existing one, or 'model' (+ optional 'ram_size') "
        "creates a new one with the source's sound cards; same model = full clone, another model = what it can "
        "express (pages, CPU, paging, TSFM / GS / NeoGS RAM+flash / MoonSound SRAM ...); refused with a per-item "
        "reason when the target cannot hold the state; 'check': true (with 'to') only decides; floppies and the tape follow as clean in-memory copies with a postfixed path, SD / HDD / CD are NOT moved. 'create' with 'zxpoly': true starts a "
        "ZX-Poly machine (four synchronized instances of 'model', default PENTAGON; optional 'zxpoly_file': a "
        ".zxp snapshot, a .prom ROM image or a multiloader disk); the returned id is its master, the slaves are "
        "hidden members. 'zxpoly_status' reports a ZX-Poly machine's modules, platform registers, lock, video "
        "mode and lockstep check. 'gs_*' actions drive the General Sound "
        "card over the same /control/audio/gs endpoint the WebAPI serves (gs_reset/gs_reset_card/gs_nmi/"
        "gs_send_command/gs_send_data/gs_read_status/gs_read_data; the byte actions need 'value'; writes, "
        "resets and NMI apply at the next instruction boundary, reads are side-effect-free peeks); "
        "'gs_switch_personality' replaces the card in the GS slot (needs 'personality': 'z80'|'lle', "
        "'lw'|'lightweight' or 'ngs'|'neogs'): a slot change applied by a machine restart (new emulator id; "
        "'replace_if_incompatible' / 'dry_run' as for slots_plug); NeoGS only: 'gs_sd_insert' (needs 'path' to a raw "
        "image), 'gs_sd_eject', 'gs_flash_save' (applied at the next instruction boundary; insert/eject are "
        "refused while TTD records), 'gs_stereo_mode' (needs 'mode': 'separated' as on the board, 'gs' 50% "
        "cross-feed like the classic GS, or 'mono'; applied at the next frame); 'gs_dump_module' writes the last completed COM30..D2 module "
        "upload to a file (optional 'path', defaults to 'gs-module-dump.mod'). "
        "ZX-Evo flash ROM (TS-Conf, ATM3; GET/POST /memory/rom/flash): 'rom_flash_status' reports the saved flash "
        "(the file zxevo-flash-<machine>-<ROM image SHA-256>.rom in the settings folder, unsaved changes, files of "
        "other ROM images that are not used), 'rom_flash_save' writes it now, 'rom_flash_discard' deletes it (the "
        "shipped ROM image returns at the next reset). "
        "ZX-bus slots (the cards on the machine's buses; the slot report is inspect_state aspect 'slots'): "
        "'slots_catalog' lists every card with its options and how it fits this machine (slot, fit real / adapter / "
        "unrealistic, outcome fits / needs-replace / refused); 'slots_matrix' the compatibility tables (optional "
        "'table'); 'slots_plug' puts 'card' into 'slot' (zxbus.next, ay-socket, auto) with optional 'options' "
        "(\"dip=ym,saa gsRam=2m\"), 'slots_remove' takes the card out of 'slot', 'slots_set' changes its 'options'. "
        "A change is planned first: one that would remove a card (or fit one unrealistically) is refused with the "
        "plan unless 'replace_if_incompatible' is true; 'dry_run' returns the plan only; 'media_disposition' "
        "save | discard for unsaved media of a removed card. Applied, the machine restarts with the new slot set: "
        "a new emulator id (restart.emulatorId), the machine state is lost, the media follow. "
        "'network_configure' changes the [NETWORK] settings ('settings': {\"card\": \"zxnetusb,zxwifi\", \"host_access\": "
        "false, \"hosts\": \"name=10.0.2.7\", ...}, the keys of POST /network/config): a change of the ZX-bus network "
        "cards (zxnetusb, zxwifi) is a slot change applied by a restart like slots_plug ('replace_if_incompatible', "
        "'dry_run', 'media_disposition' apply; the other settings go to the restarted machine), any other setting "
        "applies in place (status accepted).";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto' to reuse the single instance (auto-created when none exists)";
    schema["properties"]["model"]["type"] = "string";
    schema["properties"]["model"]["description"] =
        "Hardware model short name for 'create' / 'switch_model' / 'transfer_state' (new destination) — e.g. 48K, 128k, PLUS3, TSL, ATM3, ATM710, ATM450, PROFI, "
        "SCORPION, PROFSCORP, SPRINTER, GMX, KAY, QUORUM, LSY256, PHOENIX (see list_models; creatability is "
        "build-dependent — check the 'creatable' flags before assuming a machine exists). ZX-Poly "
        "configurations ZXPOLY-48K, ZXPOLY-128K, ZXPOLY-PENTAGON create the four-instance machine by name "
        "(same as 'zxpoly': true with the base model)";
    schema["properties"]["ram_size"]["type"] = "integer";
    schema["properties"]["ram_size"]["description"] = "Optional RAM size in KB for 'create' / 'switch_model' / 'transfer_state' with 'model' (e.g. 128, 256, 512)";
    schema["properties"]["ram_power_on"]["type"] = "string";
    schema["properties"]["ram_power_on"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["ram_power_on"]["enum"].append("random");
    schema["properties"]["ram_power_on"]["enum"].append("zero");
    schema["properties"]["ram_power_on"]["description"] =
        "'create' / 'switch_model': RAM contents of the new machine - 'zero' (every RAM page reads 0: reproducible "
        "runs, the machine depends on nothing outside it) or 'random' (noise in the screen pages 5 and 7 like real "
        "DRAM). Default for create: the model's unreal.ini ([MISC] RAMPowerOn, random when unset); for "
        "switch_model: the current machine's mode. A ZX-Poly machine applies it to all four modules";
    schema["properties"]["sprinter_bios"]["type"] = "string";
    schema["properties"]["sprinter_bios"]["description"] =
        "'create' with model SPRINTER: the BIOS image - 3.07 (default), 3.06, 3.04 (DSS 1.71 needs 3.06+) or a file in "
        "rom/sprinter (on a running Sprinter: invoke_api POST /api/v1/emulator/{id}/sprinter/bios {bios, reset})";
    schema["properties"]["sprinter_fast_start"]["type"] = "boolean";
    schema["properties"]["sprinter_fast_start"]["description"] =
        "'create' with model SPRINTER: true skips the PLD loader (~1.7 s emulated); default [SPRINTER] FastStart";
    for (const char* key : {"sprinter_isa_slot1", "sprinter_isa_slot2"})
    {
        schema["properties"][key]["type"] = "string";
        schema["properties"][key]["description"] =
            "'create' with model SPRINTER: the card in ISA slot 1 / 2 - none | ne2000 | zxbus | ram | el3c509b | "
            "sprinteresp | modem | dual16552 (default [ISA] Slot1 = none, Slot2 = ne2000; a kind not built yet is "
            "refused in the slot report /state/isa); fixed for the instance's lifetime";
    }
    schema["properties"]["profi_keyboard"]["type"] = "string";
    schema["properties"]["profi_keyboard"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* value : {"default", "matrix", "xt", "xttable"})
        schema["properties"]["profi_keyboard"]["enum"].append(value);
    schema["properties"]["profi_keyboard"]["description"] =
        "'create' with model PROFI / PROFI3: the keyboard on the board's connector - xt (the PROFI-XT controller on its "
        "reconstructed firmware: PC keys, F1-F10 / Home / End / PgUp / PgDn / Ins / Del as letter + EXT), xttable (the "
        "same from its key table), matrix (the Spectrum matrix), default (v5 xt, v3 matrix). See unreal://machine/profi";
    schema["properties"]["profi_zq3_mhz"]["type"] = "integer";
    schema["properties"]["profi_zq3_mhz"]["description"] =
        "'create' with model PROFI: the v5's third crystal, an even 16-24 MHz (default 20): the hi-res CPU clock is "
        "ZQ3 / 4 (5 MHz), ZQ3 / 2 with TURBO";
    schema["properties"]["profi_ay_clock"]["type"] = "string";
    schema["properties"]["profi_ay_clock"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["profi_ay_clock"]["enum"].append("old");
    schema["properties"]["profi_ay_clock"]["enum"].append("new");
    schema["properties"]["profi_ay_clock"]["description"] =
        "'create' with model PROFI: jumper SB7 - old = the AY at 1.5 MHz in hi-res (default), new = 1.75 MHz always";
    schema["properties"]["stranded"]["type"] = "string";
    schema["properties"]["stranded"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* value : {"refuse", "save", "discard", "keep"})
        schema["properties"]["stranded"]["enum"].append(value);
    schema["properties"]["stranded"]["description"] =
        "switch_model: unsaved writes on media the new model has no slot for - refuse (default: the switch fails "
        "and lists them), save (into their own files), discard, keep (detached media on the new machine)";
    schema["properties"]["to"]["type"] = "string";
    schema["properties"]["to"]["description"] =
        "transfer_state: existing destination emulator id (otherwise 'model' creates a new destination)";
    schema["properties"]["check"]["type"] = "boolean";
    schema["properties"]["check"]["description"] =
        "transfer_state with 'to': only report whether the transfer is possible and what would move; nothing changes";
    schema["properties"]["zxpoly"]["type"] = "boolean";
    schema["properties"]["zxpoly"]["description"] =
        "For 'create': start a ZX-Poly machine (four synchronized instances of 'model'; default PENTAGON)";
    schema["properties"]["zxpoly_file"]["type"] = "string";
    schema["properties"]["zxpoly_file"]["description"] =
        "For 'create' with zxpoly: host path of a .zxp snapshot, a .prom ZX-Poly ROM image (Test ROM) or a "
        "multiloader disk (.trd/.scl; needs a model with TR-DOS)";
    schema["properties"]["value"]["type"] = "integer";
    schema["properties"]["value"]["description"] = "Byte value (0-255) required by gs_send_command and gs_send_data";
    schema["properties"]["personality"]["type"] = "string";
    schema["properties"]["personality"]["description"] =
        "Required by gs_switch_personality: 'z80'|'lle' for the Z80 coprocessor card, 'ngs'|'neogs' for NeoGS, 'lw'|'lightweight' for the "
        "in-tree mod-player card";
    schema["properties"]["mode"]["type"] = "string";
    schema["properties"]["mode"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* mode : {"separated", "gs", "mono"})
        schema["properties"]["mode"]["enum"].append(mode);
    schema["properties"]["mode"]["description"] =
        "Required by gs_stereo_mode (NeoGS): how the DAC channels reach the two sides - 'separated' (as on the board), "
        "'gs' (50% cross-feed like the classic GS) or 'mono'";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "File path: optional for gs_dump_module (defaults to 'gs-module-dump.mod' in the server's working directory), "
        "required for gs_sd_insert (the SD card image)";
    schema["properties"]["slot"]["type"] = "string";
    schema["properties"]["slot"]["description"] =
        "slots_plug / slots_remove / slots_set: the slot - ay-socket, <bus>.<n> (zxbus.1, edge.1), <bus>.next, or auto "
        "(plug: where the planner puts the card)";
    schema["properties"]["card"]["type"] = "string";
    schema["properties"]["card"]["description"] = "slots_plug: the card id (slots_catalog): multisound, neogs, gs, tsfm, ...";
    schema["properties"]["options"]["description"] =
        "slots_plug / slots_set: card options, \"dip=ym,saa gsRam=2m\" or {\"dip\": \"ym,saa\"}";
    schema["properties"]["adapter"]["type"] = "string";
    schema["properties"]["adapter"]["description"] = "slots_plug: an adapter in the slot (the card sits behind it)";
    schema["properties"]["replace_if_incompatible"]["type"] = "boolean";
    schema["properties"]["replace_if_incompatible"]["description"] =
        "slots_plug / slots_remove / slots_set / gs_switch_personality: allow the removals (and an unrealistic fit) "
        "the plan lists; without it such a change is refused with the plan";
    schema["properties"]["dry_run"]["type"] = "boolean";
    schema["properties"]["dry_run"]["description"] = "Slot changes: return the plan only, change nothing";
    schema["properties"]["media_disposition"]["type"] = "string";
    schema["properties"]["media_disposition"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["media_disposition"]["enum"].append("save");
    schema["properties"]["media_disposition"]["enum"].append("discard");
    schema["properties"]["media_disposition"]["description"] =
        "Slot changes: unsaved media of a removed card (sd.ngs) - save into their files or discard";
    schema["properties"]["table"]["type"] = "string";
    schema["properties"]["table"]["description"] = "slots_matrix: one table (functions, cards, card-x-card, machines, card-x-machine)";
    schema["properties"]["settings"]["type"] = "object";
    schema["properties"]["settings"]["description"] =
        "network_configure: the [NETWORK] settings, e.g. {\"card\": \"zxwifi\", \"zx_wifi\": \"at\", \"host_access\": true} "
        "(the keys of POST /network/config: card, host_access, dns_mode, hosts, forwards, remote_access, com_port, "
        "zx_wifi, esp_chip, ...)";
    schema["properties"]["slots"]["type"] = "object";
    schema["properties"]["slots"]["description"] =
        "'create': the new machine's slot set in the [SLOTS] key form, replacing its INI's: {\"zxbus.1\": "
        "\"multisound\", \"zxbus.1.dip\": \"ym,saa,gs,sd\", \"ay-socket\": \"none\"}";
    schema["required"].append("action");

    registry.Register(
        "emulator_manage",
        "Manage Unreal-NG emulator instances: create, list, switch models, start/stop/pause/resume/reset/destroy. "
        "Multi-instance: target identifies the machine; 'auto' reuses the single instance or creates a default 128k one. "
        "Also drives the General Sound card (gs_reset/gs_reset_card/gs_nmi/gs_send_command/gs_send_data/"
        "gs_read_status/gs_read_data/gs_switch_personality/gs_dump_module; NeoGS: gs_sd_insert/gs_sd_eject/gs_flash_save/gs_stereo_mode) "
        "the ZX-Evo's saved flash ROM (rom_flash_status/rom_flash_save/rom_flash_discard), "
        "and the ZX-bus slots (slots_catalog/slots_matrix/slots_plug/slots_remove/slots_set; a change restarts the machine); "
        "network_configure changes the network settings (a network card change is a slot change: a restart).",
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
                const bool zxpoly = args.isMember("zxpoly") && args["zxpoly"].asBool();
                const bool hasModel = args.isMember("model") && args["model"].isString() && !args["model"].asString().empty();
                body["model"] = hasModel ? args["model"].asString()
                                         : std::string(zxpoly ? "PENTAGON" : TargetResolver::kDefaultAutoCreateModel);
                if (zxpoly)
                {
                    // Same request the WebAPI documents: {"zxpoly": {"file": ...}}
                    body["zxpoly"] = Json::Value(Json::objectValue);
                    if (args.isMember("zxpoly_file") && args["zxpoly_file"].isString())
                        body["zxpoly"]["file"] = args["zxpoly_file"].asString();
                }
                if (args.isMember("ram_size") && args["ram_size"].asUInt() > 0)
                {
                    body["ram_size"] = args["ram_size"].asUInt();
                }
                if (args.isMember("ram_power_on") && args["ram_power_on"].isString())
                    body["ram_power_on"] = args["ram_power_on"].asString();
                if (args.isMember("sprinter_bios") && args["sprinter_bios"].isString())
                    body["sprinter"]["bios"] = args["sprinter_bios"].asString();
                if (args.isMember("sprinter_fast_start") && args["sprinter_fast_start"].isBool())
                    body["sprinter"]["fast_start"] = args["sprinter_fast_start"].asBool();
                if (args.isMember("sprinter_isa_slot1") && args["sprinter_isa_slot1"].isString())
                    body["sprinter"]["isa_slot1"] = args["sprinter_isa_slot1"].asString();
                if (args.isMember("sprinter_isa_slot2") && args["sprinter_isa_slot2"].isString())
                    body["sprinter"]["isa_slot2"] = args["sprinter_isa_slot2"].asString();
                if (args.isMember("profi_keyboard") && args["profi_keyboard"].isString())
                    body["profi"]["keyboard"] = args["profi_keyboard"].asString();
                if (args.isMember("profi_zq3_mhz") && args["profi_zq3_mhz"].isInt())
                    body["profi"]["zq3_mhz"] = args["profi_zq3_mhz"].asInt();
                if (args.isMember("profi_ay_clock") && args["profi_ay_clock"].isString())
                    body["profi"]["ay_clock"] = args["profi_ay_clock"].asString();
                if (args.isMember("slots") && args["slots"].isObject())
                    body["slots"] = args["slots"];
                caller.Call("POST", "/api/v1/emulator/start", &body, [done](int status, Json::Value response) {
                    if (status == 201 || status == 200)
                    {
                        // the text first: argument order is unspecified, and gcc moves `response` out before reading it
                        const std::string text = "Created and started emulator " + response["id"].asString() + " (model " +
                                                 response.get("symbolic_id", Json::Value("")).asString() + ")";
                        done(ToolResult::Ok(text, std::move(response)));
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
                else if (action == "rom_flash_status")
                {
                    ForwardCall("GET", Endpoint(id, "/memory/rom/flash"), nullptr, caller, "Saved flash of " + id, done);
                }
                else if (action == "rom_flash_save" || action == "rom_flash_discard")
                {
                    Json::Value body;
                    body["action"] = action == "rom_flash_save" ? "save" : "discard";
                    ForwardCall("POST", Endpoint(id, "/memory/rom/flash"), &body, caller,
                                "Flash " + body["action"].asString() + " on " + id, done);
                }
                else if (action == "zxpoly_status")
                {
                    ForwardCall("GET", Endpoint(id, "/zxpoly"), nullptr, caller, "ZX-Poly status of " + id, done);
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
                else if (action == "transfer_state")
                {
                    const bool hasTo = args.isMember("to") && args["to"].isString() && !args["to"].asString().empty();
                    const bool hasModel =
                        args.isMember("model") && args["model"].isString() && !args["model"].asString().empty();
                    if (hasTo == hasModel)
                    {
                        done(ToolResult::Error("transfer_state needs exactly one of 'to' (an existing emulator id) or "
                                               "'model' (a new instance, see list_models)"));
                        return;
                    }
                    Json::Value body;
                    if (hasTo)
                        body["to"] = args["to"].asString();
                    else
                        body["model"] = args["model"].asString();
                    if (hasModel && args.isMember("ram_size") && args["ram_size"].asUInt() > 0)
                        body["ram_size"] = args["ram_size"].asUInt();
                    if (args.isMember("check") && args["check"].asBool())
                        body["check"] = true;
                    caller.Call("POST", Endpoint(id, "/snapshot/transfer"), &body, [id, done](int status, Json::Value response) {
                        // The report carries the per-item reasons on success and on refusal alike
                        const std::string summary = response.get("summary", "").asString();
                        if (status >= 200 && status < 300)
                        {
                            std::string message = response.get("check", false).asBool()
                                                      ? "Transfer check " + id + " -> " + response.get("target_id", "").asString()
                                                      : "Transferred " + id + " -> " + response.get("target_id", "").asString() +
                                                            (response.get("created", false).asBool() ? " (new instance)" : "");
                            if (!summary.empty())
                                message += "\n" + summary;
                            done(ToolResult::Ok(std::move(message), std::move(response)));
                            return;
                        }
                        std::string message = "Transfer refused (HTTP " + std::to_string(status) + "): " +
                                              (summary.empty() ? DescribeErrorBody(response) : summary);
                        done(ToolResult::Error(std::move(message)));
                    });
                }
                else if (action == "switch_model")
                {
                    if (!args.isMember("model") || !args["model"].isString() || args["model"].asString().empty())
                    {
                        done(ToolResult::Error("switch_model needs 'model' (see list_models)"));
                        return;
                    }
                    Json::Value body;
                    body["model"] = args["model"].asString();
                    if (args.isMember("ram_size") && args["ram_size"].asUInt() > 0)
                        body["ram_size"] = args["ram_size"].asUInt();
                    if (args.isMember("stranded") && args["stranded"].isString())
                        body["stranded"] = args["stranded"].asString();
                    if (args.isMember("ram_power_on") && args["ram_power_on"].isString())
                        body["ram_power_on"] = args["ram_power_on"].asString();
                    const std::string model = body["model"].asString();
                    caller.Call("POST", Endpoint(id, "/model"), &body, [id, model, done](int status, Json::Value response) {
                        if (status >= 200 && status < 300)
                        {
                            // The cards that went along or were dropped (ZX-bus slots R-OP-9), then the media
                            std::string message = "Switched " + id + " to " + model + ": new emulator " +
                                                  response.get("new_emulator_id", "").asString();
                            for (const Json::Value& line : response["report"])
                                message += "\n  " + line.asString();
                            done(ToolResult::Ok(std::move(message), std::move(response)));
                            return;
                        }
                        done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(response)));
                    });
                }
                else if (action == "slots_catalog" || action == "slots_matrix")
                {
                    std::string path = action == "slots_catalog" ? "/slots/catalog" : "/slots/matrix";
                    if (action == "slots_matrix" && args.isMember("table") && args["table"].isString())
                        path += "?table=" + args["table"].asString();
                    caller.Call("GET", Endpoint(id, path), nullptr, [action, done](int status, Json::Value response) {
                        if (status >= 200 && status < 300)
                        {
                            std::string text;
                            if (action == "slots_catalog")
                            {
                                text = "Cards for " + response.get("model", "").asString() + ":";
                                for (const Json::Value& card : response["cards"])
                                {
                                    const Json::Value& here = card["thisMachine"];
                                    text += "\n- " + card["id"].asString() + " (" + card["name"].asString() + "): " +
                                            here.get("outcome", "").asString() + " in " + here.get("slot", "").asString() +
                                            ", fit " + here.get("fit", "").asString();
                                }
                            }
                            else
                            {
                                for (const Json::Value& table : response["tables"])
                                    text += "## " + table["name"].asString() + "\n\n" + table["markdown"].asString() + "\n";
                            }
                            done(ToolResult::Ok(std::move(text), std::move(response)));
                            return;
                        }
                        done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(response)));
                    });
                }
                else if (action == "network_configure")
                {
                    if (!args.isMember("settings") || !args["settings"].isObject() || args["settings"].empty())
                    {
                        done(ToolResult::Error("Action 'network_configure' requires 'settings' (an object, e.g. "
                                               "{\"card\": \"zxnetusb\"})"));
                        return;
                    }
                    Json::Value body = args["settings"];
                    if (args.isMember("replace_if_incompatible"))
                        body["replaceIfIncompatible"] = args["replace_if_incompatible"].asBool();
                    if (args.isMember("dry_run"))
                        body["dryRun"] = args["dry_run"].asBool();
                    if (args.isMember("media_disposition") && args["media_disposition"].isString())
                        body["mediaDisposition"] = args["media_disposition"].asString();
                    caller.Call("POST", Endpoint(id, "/network/config"), &body, [done](int status, Json::Value response) {
                        done(SlotReplyResult(status, std::move(response), "network"));
                    });
                }
                else if (action == "slots_plug" || action == "slots_remove" || action == "slots_set")
                {
                    const std::string slot = args.get("slot", "").asString();
                    if (slot.empty() && action != "slots_plug")
                    {
                        done(ToolResult::Error("Action '" + action + "' requires 'slot' (see inspect_state aspect slots)"));
                        return;
                    }
                    Json::Value body(Json::objectValue);
                    if (action == "slots_plug")
                    {
                        if (!args.isMember("card") || !args["card"].isString() || args["card"].asString().empty())
                        {
                            done(ToolResult::Error("Action 'slots_plug' requires 'card' (see slots_catalog)"));
                            return;
                        }
                        body["card"] = args["card"].asString();
                        if (args.isMember("adapter") && args["adapter"].isString())
                            body["adapter"] = args["adapter"].asString();
                    }
                    if (args.isMember("options"))
                        body["options"] = args["options"];
                    if (args.isMember("replace_if_incompatible"))
                        body["replaceIfIncompatible"] = args["replace_if_incompatible"].asBool();
                    if (args.isMember("dry_run"))
                        body["dryRun"] = args["dry_run"].asBool();
                    if (args.isMember("media_disposition") && args["media_disposition"].isString())
                        body["mediaDisposition"] = args["media_disposition"].asString();
                    const std::string verb = action == "slots_plug" ? "plug" : action == "slots_remove" ? "remove" : "options";
                    caller.Call("POST", Endpoint(id, "/slots/" + (slot.empty() ? std::string("auto") : slot) + "/" + verb), &body,
                                [done](int status, Json::Value response) { done(SlotReplyResult(status, std::move(response))); });
                }
                else if (action == "gs_reset" || action == "gs_reset_card" || action == "gs_nmi" ||
                         action == "gs_send_command" || action == "gs_send_data" ||
                         action == "gs_read_status" || action == "gs_read_data" ||
                         action == "gs_switch_personality" || action == "gs_dump_module" || action == "gs_sd_insert" ||
                         action == "gs_sd_eject" || action == "gs_flash_save" || action == "gs_stereo_mode")
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
                            done(ToolResult::Error("Action '" + action + "' requires 'personality' (z80, lle, lw, lightweight, ngs or neogs)"));
                            return;
                        }
                        body["personality"] = args["personality"].asString();
                        if (args.isMember("replace_if_incompatible"))
                            body["replaceIfIncompatible"] = args["replace_if_incompatible"].asBool();
                        if (args.isMember("dry_run"))
                            body["dryRun"] = args["dry_run"].asBool();
                        if (args.isMember("media_disposition") && args["media_disposition"].isString())
                            body["mediaDisposition"] = args["media_disposition"].asString();
                    }
                    else if ((action == "gs_dump_module" || action == "gs_sd_insert") && args.isMember("path") &&
                             args["path"].isString())
                    {
                        body["path"] = args["path"].asString();
                    }
                    else if (action == "gs_sd_insert")
                    {
                        done(ToolResult::Error("Action 'gs_sd_insert' requires 'path' (an SD card image)"));
                        return;
                    }
                    else if (action == "gs_stereo_mode")
                    {
                        if (!args.isMember("mode") || !args["mode"].isString())
                        {
                            done(ToolResult::Error("Action 'gs_stereo_mode' requires 'mode' (separated, gs or mono)"));
                            return;
                        }
                        body["mode"] = args["mode"].asString();
                    }
                    caller.Call("POST", Endpoint(id, "/control/audio/gs"), &body, [action, done](int status, Json::Value response) {
                        if (action == "gs_switch_personality" && response.isMember("status"))
                        {
                            // A slot change (Q10): the plan, the restart, the new emulator id
                            done(SlotReplyResult(status, std::move(response)));
                            return;
                        }
                        if (status >= 200 && status < 300)
                        {
                            if (response.isMember("value"))
                            {
                                std::string message = "GS " + action.substr(3) + " -> " + std::to_string(response["value"].asInt());
                                done(ToolResult::Ok(std::move(message), std::move(response)));
                            }
                            else if (action == "gs_dump_module")
                            {
                                std::string message = "GS module dumped: " + std::to_string(response.get("bytes", 0).asUInt64()) +
                                                        " bytes -> " + response.get("path", "").asString();
                                done(ToolResult::Ok(std::move(message), std::move(response)));
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
    schema["properties"]["drive"]["description"] = "Floppy drive for disk images (A-D or 0-3; autostart needs A)";
    schema["properties"]["play"]["type"] = "boolean";
    schema["properties"]["play"]["default"] = false;
    schema["properties"]["play"]["description"] = "Start tape playback immediately after loading a tape";
    schema["properties"]["autostart"]["type"] = "boolean";
    schema["properties"]["autostart"]["default"] = false;
    schema["properties"]["autostart"]["description"] =
        "Disk images only, drive A only: quick-reset into TR-DOS and run the disk, same as the Qt UI's "
        "drag-and-drop autostart. Ignored for snapshots/tapes and for drives other than A.";
    schema["properties"]["commit"]["type"] = "string";
    schema["properties"]["commit"]["description"] =
        "Snapshots only: who writes the machine. Omitted = the plan decides (the machine's own policy, else the legacy "
        "commit that has always loaded snapshots); 'legacy' = the legacy commit even where the machine has a policy; "
        "or a registered policy name (an unknown name is refused and the answer lists the known ones). The answer's "
        "report shows the commit that ran, or why the load was refused";
    schema["properties"]["inspect"]["type"] = "boolean";
    schema["properties"]["inspect"]["default"] = false;
    schema["properties"]["inspect"]["description"] =
        "Snapshots only: do not load, report what loading would do on this machine (the file's contents - format, "
        "banks, CPU, paging, extensions - and the plan: who would commit it, or the refusal and its reason). Nothing "
        "is written";
    schema["required"].append("path");

    registry.Register(
        "load_software",
        "Load software into the emulator by auto-detecting the file type: snapshots (.sna .z80 .szx), TS-Conf programs (.spg, switches the machine to model TSL: the answer's emulator_id is then the new one), RZX input recordings (.rzx, played on the machine the recording needs), tapes (.tap .tzx .spc .sta .ltp .zxt), "
        "disk images (.trd .scl .fdi .udi .dsk .td0 .mgt .img .ima). The machine must be created first (target:'auto' "
        "handles that). If the path exists locally on the MCP host, the file is uploaded to the emulator "
        "automatically; otherwise, the path is passed to the emulator for direct loading.",
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
                                            "'. Supported: .sna .z80 .szx .spg (snapshot), .rzx (input recording), .tap .tzx .spc .sta .ltp .zxt (tape), "
                                            ".trd .scl .fdi .udi .dsk .td0 .mgt .img .ima (disk)"));
                return;
            }
            std::string ext = path.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            bool isSnapshot = ext == "sna" || ext == "z80" || ext == "szx" || ext == "spg" || ext == "rzx";
            const auto& tapeExtensions = TapeExtensions();
            bool isTape = std::find(tapeExtensions.begin(), tapeExtensions.end(), ext) != tapeExtensions.end();
            bool isDisk = ext == "trd" || ext == "scl" || ext == "fdi" || ext == "udi" || ext == "dsk" ||
                          ext == "td0" || ext == "mgt" || ext == "img" || ext == "ima";
            if (!isSnapshot && !isTape && !isDisk)
            {
                done(ToolResult::Error("Unsupported file type '." + ext +
                                            "'. Supported: .sna .z80 .szx .spg (snapshot), .rzx (input recording), .tap .tzx .spc .sta .ltp .zxt (tape), "
                                            ".trd .scl .fdi .udi .dsk .td0 .mgt .img .ima (disk)"));
                return;
            }

            bool play = args.isMember("play") && args["play"].asBool();
            bool autostart = args.isMember("autostart") && args["autostart"].asBool();
            const bool inspect = args.isMember("inspect") && args["inspect"].asBool();
            std::string commit = args.isMember("commit") && args["commit"].isString() ? args["commit"].asString() : "";
            // A policy name is an identifier: it goes into a query string as is
            if (commit.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos)
            {
                done(ToolResult::Error("'commit' is a name: letters, digits, '_', '.' and '-' only"));
                return;
            }
            std::string drive = args.isMember("drive") && args["drive"].isString() && !args["drive"].asString().empty()
                                    ? args["drive"].asString()
                                    : "A";

            // Try to read local file for piggybacking (MCP bridge uploads embedded content)
            auto fileContent = std::make_shared<std::vector<uint8_t>>();
            const bool isLocalFile = TryReadLocalFile(path, *fileContent);
            const std::string filename = ExtractFilename(path);

            TargetResolver::ResolveFromArgs(args, caller, [path, ext, isSnapshot, isTape, isDisk, play, autostart, drive, inspect, commit, &caller, done,
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
                if (autostart && isDisk)
                {
                    headers["X-Autostart"] = "true";
                }

                // An RZX recording plays on the model it needs (the model
                // switches when this machine is another one: a new target id)
                if (ext == "rzx")
                {
                    if (isLocalFile)
                    {
                        ForwardCallRaw("POST", Endpoint(id, "/rzx/play"), *fileContent, headers, caller,
                                       "Playing RZX " + filename + " (uploaded) from " + id, done);
                    }
                    else
                    {
                        Json::Value body;
                        body["path"] = path;
                        ForwardCall("POST", Endpoint(id, "/rzx/play"), &body, caller, "Playing RZX " + path + " from " + id,
                                    done);
                    }
                    return;
                }

                if (isSnapshot)
                {
                    // inspect = what a load would do (nothing written); the commit choice rides on a query for an upload
                    const std::string action = inspect ? "/snapshot/inspect" : "/snapshot/load";
                    const std::string verb = inspect ? "Inspected snapshot " : "Loaded snapshot ";
                    if (isLocalFile)
                    {
                        const std::string endpoint =
                            Endpoint(id, action) + (commit.empty() ? std::string() : "?commit=" + commit);
                        ForwardCallRaw("POST", endpoint, *fileContent, headers, caller,
                                       verb + filename + " (uploaded) on " + id, done);
                    }
                    else
                    {
                        Json::Value body;
                        body["path"] = path;
                        if (!commit.empty())
                            body["commit"] = commit;
                        ForwardCall("POST", Endpoint(id, action), &body, caller, verb + path + " on " + id, done);
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
                    if (autostart)
                    {
                        body["autostart"] = true;
                    }
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
                               "bp_list", "bp_reset_hits", "port_out", "wait"})
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
    schema["properties"]["address_end"]["type"] = "integer";
    schema["properties"]["address_end"]["description"] = "Optional for bp_add: range end, inclusive (execution / read / write)";
    schema["properties"]["slot_only"]["type"] = "boolean";
    schema["properties"]["slot_only"]["description"] = "Optional for bp_add with page: only through the slot of address";
    schema["properties"]["port_mask"]["type"] = "integer";
    schema["properties"]["port_mask"]["description"] =
        "Optional for bp_add of port_in / port_out: match (port & port_mask) == (address & port_mask); 255 catches #FE "
        "on any high byte";
    schema["properties"]["hits"]["type"] = "string";
    schema["properties"]["hits"]["description"] =
        "Optional for bp_add: stop on the Nth hit only ('5'), from the Nth on ('>=5') or every Nth ('%5'); every hit is "
        "counted (bp_list shows hit_count)";
    schema["properties"]["page"]["type"] = "string";
    schema["properties"]["page"]["description"] =
        "Optional for bp_add of execution / read / write: 'ram32', 'rom3' or 'cache0' - a physical breakpoint on that "
        "page at offset address & #3FFF, through whatever slot shows the page (slot_only: only through the slot of "
        "address)";
    schema["properties"]["note"]["type"] = "string";
    schema["properties"]["note"]["description"] = "Optional annotation for bp_add";
    schema["properties"]["group"]["type"] = "string";
    schema["properties"]["group"]["description"] = "Optional group for bp_add (created on use; default 'default')";
    schema["properties"]["bp_id"]["type"] = "string";
    schema["properties"]["bp_id"]["description"] = "Breakpoint id for bp_remove/bp_enable/bp_disable/bp_reset_hits (none: all)";
    schema["properties"]["port"]["type"] = "string";
    schema["properties"]["port"]["description"] = "Port for port_out: 0..#FFFF as 0x13AF, #13AF, 13AFh or decimal";
    schema["properties"]["value"]["type"] = "string";
    schema["properties"]["value"]["description"] = "Byte for port_out: 0..#FF, the same forms";
    schema["properties"]["since"]["type"] = "integer";
    schema["properties"]["since"]["description"] =
        "For wait: the debugger seq last seen (inspect_state snapshot shows it); omitted = the current one";
    schema["properties"]["timeout_ms"]["type"] = "integer";
    schema["properties"]["timeout_ms"]["description"] = "For wait: 0..60000, default 10000";
    schema["required"].append("action");

    registry.Register(
        "control_execution",
        "Advance or halt the CPU: pause/resume/run, step (1 or N instructions), step_over, step_out, run_frame(s), "
        "run_tstates, run_to_interrupt. Also manages breakpoints (bp_add/bp_remove/bp_enable/bp_disable/bp_clear/bp_list/"
        "bp_reset_hits; bp_add takes ranges, physical pages, port masks and hit policies). "
        "port_out writes a port through the machine's decoder like a CPU OUT (paging, TS-Conf registers, AY, border), "
        "without breakpoints or device waits, as a TTD tool edit; it works paused or running. "
        "wait blocks until something a debugger shows changes (a stop, a run start, a tool edit) or timeout_ms passes. "
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
                                // the text first: argument order is unspecified (gcc moves `registers` out first)
                                const std::string text = "Paused. " + FormatRegisters(registers);
                                done(ToolResult::Ok(text, std::move(registers)));
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
                if (action == "bp_reset_hits")
                {
                    Json::Value body(Json::objectValue);
                    if (args.isMember("bp_id"))
                        body["id"] = args["bp_id"];
                    ForwardCall("POST", Endpoint(id, "/breakpoints/hits/reset"), &body, caller,
                                args.isMember("bp_id") ? "Hit counter reset" : "All hit counters reset", done);
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
                    for (const char* key : {"address_end", "page", "slot_only", "port_mask", "hits", "hit_mode", "hit_target",
                                            "note", "group"})
                        if (args.isMember(key))
                            body[key] = args[key];
                    ForwardCall("POST", Endpoint(id, "/breakpoints"), &body, caller, "Breakpoint added on " + id, done);
                    return;
                }
                if (action == "wait")
                {
                    std::string path = Endpoint(id, "/debug/wait") + "?timeout_ms=" +
                                       std::to_string(args.isMember("timeout_ms") ? args["timeout_ms"].asUInt() : 10000u);
                    if (args.isMember("since"))
                        path += "&since=" + std::to_string(args["since"].asUInt64());
                    caller.Call("GET", path, nullptr, [done](int status, Json::Value response) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("wait failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response)));
                            return;
                        }
                        const Json::Value& pause = response["pause"];
                        std::string text = response.get("changed", false).asBool() ? "Changed: seq " : "No change: seq ";
                        text += std::to_string(response.get("seq", 0).asUInt64()) + ", " +
                                response.get("state", "").asString();
                        if (pause.get("reason", "none").asString() == "breakpoint")
                        {
                            char where[48];
                            std::snprintf(where, sizeof(where), " at breakpoint #%u (%04X)", pause.get("breakpoint_id", 0).asUInt(),
                                          pause.get("address", 0).asUInt());
                            text += where;
                        }
                        else if (pause.get("reason", "none").asString() != "none")
                            text += " (last stop: " + pause.get("reason", "").asString() + ")";
                        done(ToolResult::Ok(text, std::move(response)));
                    });
                    return;
                }
                if (action == "port_out")
                {
                    if (!args.isMember("port") || !args.isMember("value"))
                    {
                        done(ToolResult::Error("port_out requires 'port' and 'value'"));
                        return;
                    }
                    Json::Value body;
                    body["port"] = args["port"];
                    body["value"] = args["value"];
                    caller.Call("POST", Endpoint(id, "/ports/out"), &body, [done](int status, Json::Value response) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("port_out failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response)));
                            return;
                        }
                        const std::string text = "Port " + response.get("port", "").asString() + " <- " +
                                                 response.get("value", "").asString() + " (" +
                                                 response.get("moment", "").asString() + ")";
                        done(ToolResult::Ok(text, std::move(response)));
                    });
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
                        // A breakpoint that ended a step says so (the structured result carries `stop`)
                        if (response.isObject() && response.isMember("stop") &&
                            response["stop"].get("reason", "").asString() == "breakpoint")
                        {
                            const Json::Value& stop = response["stop"];
                            char where[96];
                            std::snprintf(where, sizeof(where), "Stopped at breakpoint #%u (%s) at $%04X after %u instruction(s). ",
                                          stop.get("breakpoint_id", 0).asUInt(), stop.get("access", "").asString().c_str(),
                                          stop.get("address", 0).asUInt(), response.get("executed", 0).asUInt());
                            summary = where;
                        }
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

/// One-line summary of a GET /ttd/status body (shared by inspect_state's
/// 'ttd' aspect and time_travel's 'status' action). When a GET /ttd/position
/// body was merged in as "position", the current frame is named too.
/// " on PENTAGON, General Sound z80, TurboSound turbosound" - the recorded machine of
/// a ttd/status or ttd/file-info answer ("" without one)
std::string FormatTtdMachine(const Json::Value& machine)
{
    if (!machine.isObject())
        return {};
    std::ostringstream out;
    out << " on " << (machine["model"].isString() ? machine["model"].asString()
                                                  : "model id " + std::to_string(machine["model_id"].asUInt()));
    out << ", General Sound " << machine["general_sound"].asString() << ", TurboSound "
        << machine["turbo_sound"].asString();
    return out.str();
}

/// One line for GET /api/v1/ttd/file-info
std::string FormatTtdFileInfo(const Json::Value& info)
{
    if (!info["ok"].asBool())
        return "cannot read " + info["path"].asString() + ": " + info["error"].asString();
    std::ostringstream out;
    out << info["path"].asString() << ": recorded" << FormatTtdMachine(info["machine"]) << ", frames "
        << info["session_start_frame"].asUInt64() << ".." << info["session_end_frame"].asUInt64() << ", "
        << info["checkpoint_count"].asUInt64() << " checkpoint(s), " << info["file_bytes"].asUInt64() << " bytes";
    const Json::Value& devices = info["machine"]["peripherals"];
    if (devices.isArray() && !devices.empty())
    {
        out << "; devices:";
        for (const Json::Value& d : devices)
            out << " " << d.asString();
    }
    if (info["recorded_by"].isString())
        out << "; recorded by " << info["recorded_by"].asString();
    return out.str();
}

/// What the write journal covers (D40), from a status / journal response
std::string DescribeJournalSegments(const Json::Value& status)
{
    const Json::Value& spans = status["write_journal_segments"];
    if (!spans.isArray() || spans.empty())
        return "covers nothing: write searches replay";
    if (status["write_journal_complete"].asBool())
        return "covers the whole session";
    std::ostringstream out;
    out << "covers " << spans.size() << " span(s):";
    for (Json::ArrayIndex i = 0; i < spans.size() && i < 4; ++i)
        out << (i ? "," : "") << " frames " << spans[i]["from_frame"].asUInt64() << ".." << spans[i]["to_frame"].asUInt64();
    if (spans.size() > 4)
        out << ", ...";
    out << "; write searches outside replay";
    return out.str();
}

std::string FormatTtdStatus(const Json::Value& status)
{
    if (!status["ttd_available"].asBool())
    {
        return "TTD engine not available in this build";
    }
    if (status["unavailable_reason"].isString())
    {
        return "not available for this machine: " + status["unavailable_reason"].asString();
    }
    const std::string state = status["state"].asString();
    const uint64_t checkpoints = status["checkpoint_count"].asUInt64();
    std::ostringstream out;
    if (state == "idle" && checkpoints == 0)
    {
        out << "idle, no history (action 'start' begins recording)";
        return out.str();
    }
    out << state << ", frames " << status["session_start_frame"].asUInt64() << ".." << status["current_end_frame"].asUInt64()
        << ", " << checkpoints << " checkpoint(s)";
    if (status["machine"].isObject())
        out << ", recorded" << FormatTtdMachine(status["machine"]);
    if (status.isMember("position") && status["position"].isMember("current"))
    {
        const Json::Value& current = status["position"]["current"];
        out << ", at frame " << current["frame"].asUInt64();
        if (current["tinframe"].asUInt() != 0)
        {
            out << " t=" << current["tinframe"].asUInt();
        }
    }
    out << ", write journal " << (status["write_journal_enabled"].asBool() ? "on" : "off");
    out << " (" << DescribeJournalSegments(status) << ")";
    if (status["bookmark_count"].asUInt64() > 0)
    {
        out << ", " << status["bookmark_count"].asUInt64() << " bookmark(s)";
    }
    if (status["input_event_count"].asUInt64() > 0 || status["external_event_count"].asUInt64() > 0)
    {
        out << ", " << status["input_event_count"].asUInt64() << " input event(s), "
            << status["external_event_count"].asUInt64() << " replay barrier(s)";
    }
    if (status.isMember("input_history_complete") && !status["input_history_complete"].asBool())
    {
        out << " (loaded file predates saved input: in-frame replay may differ from the recording)";
    }
    if (status["history_limit_frames"].asUInt64() != 0 || status["history_limit_bytes"].asUInt64() != 0)
    {
        out << ", history limit";
        if (status["history_limit_frames"].asUInt64() != 0)
            out << " " << status["history_limit_frames"].asUInt64() << " frames";
        if (status["history_limit_bytes"].asUInt64() != 0)
            out << " " << status["history_limit_bytes"].asUInt64() / (1024 * 1024) << " MB";
        out << " (" << status["history_bytes"].asUInt64() / (1024 * 1024) << " MB held, "
            << status["evicted_checkpoints"].asUInt64() << " oldest checkpoint(s) released)";
    }
    if (status["port_journal_active"].asBool())
    {
        out << ", port journals: " << status["port_read_count"].asUInt64() << " IN, "
            << status["port_write_count"].asUInt64() << " OUT (replay isolated from media and host devices; "
            << "'port_events' searches them)";
    }
    else if (status["port_journal_off_reason"].isString())
    {
        out << ", port journals off (" << status["port_journal_off_reason"].asString() << ")";
    }
    if (status["port_replay_value_mismatches"].asUInt64() > 0 || status["port_replay_divergences"].asUInt64() > 0)
    {
        out << ", replay: " << status["port_replay_value_mismatches"].asUInt64()
            << " device answer(s) differed (the CPU got the recorded values), "
            << status["port_replay_divergences"].asUInt64() << " divergence(s)";
    }
    if (status["last_drop_reason"].isString())
    {
        out << ", last session dropped: " << status["last_drop_reason"].asString();
    }
    if (status["loaded_from_file"].asBool())
    {
        out << ", loaded from " << status["source_path"].asString();
    }
    if (state == "recording")
    {
        out << " - speed held at 1x, turbo and fast tape/disk off until stop";
    }
    else if (state == "detached")
    {
        out << " - positioned in history, emulator paused";
    }
    else
    {
        out << " - history retained, browse with seek/step";
    }
    return out.str();
}

void RegisterInspectState(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["aspects"]["type"] = "array";
    schema["properties"]["aspects"]["items"]["type"] = "string";
    Json::Value allowed(Json::arrayValue);
    for (const char* aspect : {"machine", "registers", "memory", "memory_map", "disasm", "stack", "breakpoints", "memory_banks", "paging", "ports", "video",
                               "screen", "screen_flash", "screen_attributes", "screen_ocr", "screen_image", "screen_digest", "timing", "video_layout", "video_text", "rom", "audio_ay", "audio_fm", "audio_gs", "audio_covox", "audio_moonsound", "audio_opl4_fm", "audio_opl4_pcm", "fdc", "ide", "cdaudio", "rtc", "profi", "isa", "network", "mouse",
                               "ttd", "contention", "tsconf", "tsconf_tsu", "sprinter", "sprinter_ports", "sprinter_text",
                               "sprinter_video", "sprinter_palette", "sprinter_sound_ring", "sprinter_bios", "sprinter_zx_mode",
                               "sprinter_pld_journal", "memory_region", "video_changes", "audio_mixer", "snapshot", "pchist", "slots",
                               "audio_multisound", "audio_midi"})
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
        "'snapshot' = one coherent debugger picture read at one moment (GET /debug/snapshot): seq, state, last stop, regs, "
        "prev_regs, pages, stack, time, 'count' code lines from PC, the 'windows' memory windows (base64) and 'pchist' PC "
        "history entries. 'pchist' = the PC history alone (GET /debug/pchist): the newest 'count' instructions the CPU "
        "started with their window's page; the first request starts recording. "
        "'paging' = tagged paging latches + bank table (P1-2 design), 'ports' = static port map with semantic tags, "
        "latch bindings and live routing flags, "
        "'audio_ay' = every AY/SSG chip fully decoded, 'audio_fm' = TurboSound FM board + both YM2203 FM halves (mode, timers, "
        "channels, operators, envelopes, key-on), 'audio_gs' = General Sound card (mailbox flags, MPAG page, DAC channels, "
        "coprocessor core, and a 'neogs' object with windows, clock, SD card, MP3 decoder and DMA on the NeoGS card; "
        "reports unavailable when the card is not fitted), 'audio_covox' = Covox / SoundDrive (fitment, the ports this model "
        "decodes, ports shared with Beta-128, the four DAC latches), 'audio_moonsound' = MoonSound OPL4 overview (NEW/NEW2, "
        "address latches, #F8/#F9 mix, wave memory, keyed FM channels and PCM slots), 'audio_opl4_fm' = its 18 FM channels "
        "(F-number, block, Hz, key-on, feedback, route, timers, register banks), 'audio_opl4_pcm' = its 24 wavetable slots "
        "(wave, octave, playback rate, key-on, level, pan, addresses, envelope), 'fdc' = Beta Disk WD1793 registers, status, FSM, drives, "
        "'ide' = IDE board (scheme, latches, both units' task file, command in progress, CD sense; unavailable without a board), "
        "'cdaudio' = the ATAPI CD drives' audio (disc and tracks, status playing / paused / completed / error, head as LBA / MSF / "
        "track / index, play range, page 0Eh volume and routing, mixer row; control it with invoke_api POST "
        "/api/v1/emulator/{id}/cdaudio/{verb}: play track=N, pause, resume, stop, volume, mixer), "
        "'network' also lists the expansion slots (slots: the Sprinter's ISA NE2000 with its DP8390 registers, the 3C509B with its ID port, window, FIFOs, EEPROM and link) and the "
        "Ethernet gateway (ethernet_gateway: mode nat | bridge, leases, ARP, TCP / UDP, counters; in bridge its bridge part: "
        "adapter, open, error, library, frame counters); its frame capture and frame injection go "
        "through invoke_api GET /api/v1/emulator/{id}/network/frames?link=isa2.eth[&format=pcap] and POST "
        "/api/v1/emulator/{id}/network/frame {link, hex}; the host adapters for the bridge: GET "
        "/api/v1/emulator/{id}/network/adapters, and the mode through the network settings (ethernet_mode, bridge_adapter); "
        "its traffic section is the tap of everything every adapter sent and received (records through invoke_api GET "
        "/api/v1/emulator/{id}/network/traffic?since=&adapter=&kind=&last=[&format=pcapng], control with POST "
        "{action: clear | start (path: a pcapng file, unbounded) | stop | ring | stream (port: a live pcapng stream for "
        "Wireshark, 0 = any free port) | stream-stop}), "
        "'audio_multisound' = the ZX-MultiSound card (options, the built-ins its slot shadows, CPLD latches, the YM2203 "
        "pair as the TSFM report's chips, SAA1099 voices and envelopes, the General Sound report, DACs, MIDI summary), "
        "'audio_midi' = its MIDI line and SAM2695 synthesizer (16 parts: program, preset name, volume, pan, voices, "
        "notes sounding; polyphony, counters, bank; a panic is invoke_api POST /api/v1/emulator/{id}/control/audio/midi "
        "{action: panic}), "
        "'slots' = the ZX-bus slot report (board, buses with kind / arbitration / retrofit note, every slot's card, "
        "options, adapter, fit real / adapter / unrealistic, state, functions, port claims, media, and the built-in "
        "devices: active, switched off, shadowed by a slot, replaced in their socket; change it with emulator_manage "
        "slots_plug / slots_remove / slots_set), "
        "'rtc' = CMOS clock (part, ports, NVRAM file, time base, time, registers A-D, alarms, every cell; unavailable without one - "
        "write cells with invoke_api POST /api/v1/emulator/{id}/rtc/cells {start, bytes}), "
        "'profi' = the ZX Profi's board chips (the port map in force, the 8255, the 8253 counters, the 8251 and the #B3 latch; "
        "unavailable on other machines), "
        "'isa' = the Sprinter's ISA-8 slots (the #9FBD latch, whether window 3 shows a slot, per slot the configured and "
        "fitted card - an NE2000's chip, base, MAC, registers; a 3C509B's also resources.id_port (#100-#1F0, its isolation "
        "state); the ZX-bus adapter's 'zx_bus' object: the General Sound / "
        "NeoGS behind it, its ports #B3 / #BB / #33 through ISA I/O #xxB3 / #xxBB / #xx33, its reset from ISA RESET DRV, "
        "its mailbox status - and cycle counters; per slot 'irq_line': the IRQ line's level, "
        "who drives it, its route to the Z84C15 PIO port B (PB0 / PB1), the PIO's bit-mode setup, pending / under service, "
        "whether it reaches the CPU, and edge / request / acknowledge counters ('irq_summary' in one line); "
        "unavailable on other machines - run an "
        "ISA cycle with invoke_api POST /api/v1/emulator/{id}/control/isa {action: io_read|io_write|io_peek|mem_read|"
        "mem_write|mem_peek|reset|latch, slot, address, value}; the access journal - who touched which card register, "
        "frame / T / PC, with the interrupt events (event: irq - line edges, PIO requests, acknowledges, RETI; also in "
        "irq_events, a ring that polling does not flush) - with "
        "invoke_api GET /api/v1/emulator/{id}/state/isa/journal?last=N), "
        "'screen_attributes' = per-cell ink/paper/bright/flash decoded from the classic ZX attribute memory layout "
        "(32x24 cells, read straight off the RAM page, not the Z80 bank mapping) - prefer this over a screenshot when "
        "you only need the color/attribute layout, 'video_layout' = the video mode's layers (surface size, beam window, "
        "dots per T) and framebuffer placement (works for ATM, Profi, AlCo modes too), 'video_text' = exact text of an ATM / "
        "ZX-Evo text mode (80x25 codes and attributes; unavailable in bitmap modes - use screen_ocr), 'mouse' = "
        "the machine's mouse (Kempston interface, Sprinter serial mouse, ZX-Evo PS/2: device, fitted, ports, serial line) incl. Kempston port routing, 'ttd' = time-travel session: state "
        "(idle/recording/detached), recorded frame range, checkpoint count, current position (use the time_travel tool to act on it). "
        "'tsconf' = the TS-Conf machine (memory map, video, TSU summary, interrupts, DMA, clock, SD), 'tsconf_tsu' = its TSU "
        "objects for debug views (tile layers, all 85 sprite descriptors decoded, the 256 CRAM cells); both unavailable on "
        "other machines. 'sprinter' = the Sprinter Sp2000 (PLD configuration and module, CNF map / DOS / PN5, the four "
        "windows with physical page and kind, ALL_MODE / PORT_Y / RGMOD / HOLD, the cells #C0-#FF, turbo, frame "
        "length, a video summary of the mode table, the Z84C15 with the keyboard FIFO, the floppy density latch, BIOS "
        "images), 'sprinter_ports' = its decoded port table for the current map / DOS / PN5 (code, name, address "
        "pattern; one port or another map: invoke_api GET /api/v1/emulator/{id}/state/sprinter/ports/lookup?port=21BC "
        "and /state/sprinter/ports?map=0&dos=1&rw=r), 'sprinter_text' = its screen text (80 x 32 from the mode table's "
        "text squares: BIOS SETUP, DSS; video_text and screen_ocr fall back to it), 'sprinter_video' = the mode table "
        "per square as a map (one letter a square: G graphics 320, g 640, T text 40, t text 80, Z Spectrum cell, B border, . blank, * INT) "
        "with HOLD / frame length / RGMOD / PORT_Y and the palettes in use (every square decoded: invoke_api GET "
        "/api/v1/emulator/{id}/state/sprinter/video), 'sprinter_palette' = the palettes the picture uses (R, G, B per pen "
        "as video RAM holds them; ?k=0-7|all through invoke_api), 'sprinter_sound_ring' = the Covox-Blaster sample ring, "
        "'sprinter_bios' = the BIOS images, the one loaded (by CRC-32) and the start options (select: invoke_api POST "
        "/api/v1/emulator/{id}/sprinter/bios {bios: 3.06, reset: true}), 'sprinter_zx_mode' = the ZX (Spectrum) "
        "mode report: active, the launcher configuration (each .ZX option from the hardware: CNF turbo / map / clean "
        "bits, ALL_MODE, frame, INT; the launcher's own text and option table in RAM), the best-matching mode file "
        "(SP.ZX, P128.ZX, ORIGIN.ZX ...) with confidence, the clock (CNF request, F12, MHz, why), the ROM set by CRC, "
        "and the table decode of #7FFD / #1FFD / #01FD / #xxFD / #FE / #1F with each port's effect now, "
        "'sprinter_pld_journal' = who changed the PLD setup and when (frame, T, PC): port table writes with the decodes "
        "they changed, CNF / turbo, the clock, #7FFD / #1FFD by the port used, ALL_MODE, RGMOD, HOLD, frame length, the "
        "PLD load, F12, Ctrl+Alt+Del, resets (pld_journal_kinds = 'cnf,port_1ffd' filters; pld_journal_source = 'ttd' "
        "reads the TTD recording's OUTs to those codes instead); "
        "all unavailable on other machines. 'memory_region' = bytes of a device memory region outside the CPU's pages "
        "(region, default 'vram' = the Sprinter's 256 KB video RAM; address = offset, size = byte count; list: invoke_api "
        "GET /api/v1/emulator/{id}/memory/regions; write: POST /memory/region/{name} {offset, hex}). 'video_changes' = "
        "the video change log of every machine: latch changes (mode, #7FFD, border, #FF77, the Sprinter's RGMOD / HOLD / "
        "PORT_Y / ALL_MODE / frame height) with frame T, beam line, PC, and the palette / mode table writes per frame. "
        "'audio_mixer' = the per-device mixer (source key, muted, solo, volume, gain_db, peak, active; set: invoke_api "
        "PUT /api/v1/emulator/{id}/audio/mixer/{source} {muted, solo, volume}; capture one device: capture_media "
        "audio_capture with source).";
    schema["properties"]["pld_journal_kinds"]["type"] = "string";
    schema["properties"]["pld_journal_kinds"]["description"] =
        "'sprinter_pld_journal': comma list of kinds (port_table, cnf, clock, port_7ffd, port_1ffd, all_mode, rgmod, hold, "
        "frame_lines, pld_load, pld_configured, f12, ctrl_alt_del, reset); empty = all";
    schema["properties"]["pld_journal_source"]["type"] = "string";
    schema["properties"]["pld_journal_source"]["description"] = "'sprinter_pld_journal': live (default) or ttd (the recording)";
    schema["properties"]["region"]["type"] = "string";
    schema["properties"]["region"]["default"] = "vram";
    schema["properties"]["region"]["description"] = "'memory_region': the region name (GET /memory/regions)";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["address"]["type"] = "integer";
    schema["properties"]["address"]["description"] = "Start address for 'memory' and 'disasm' (defaults: 0 and current PC)";
    schema["properties"]["size"]["type"] = "integer";
    schema["properties"]["size"]["default"] = 64;
    schema["properties"]["size"]["description"] = "Byte count for 'memory' (max 4096)";
    schema["properties"]["count"]["type"] = "integer";
    schema["properties"]["count"]["default"] = 8;
    schema["properties"]["count"]["description"] =
        "Instruction count for 'disasm' (and the code lines of 'snapshot'; the entries of 'pchist', at most 1024)";
    schema["properties"]["pchist"]["type"] = "integer";
    schema["properties"]["pchist"]["description"] = "For 'snapshot': PC history entries to include (0 = none, at most 1024)";
    schema["properties"]["windows"]["type"] = "array";
    schema["properties"]["windows"]["items"]["type"] = "string";
    schema["properties"]["windows"]["description"] =
        "'snapshot' memory windows '<space>:<address>:<length>' (space cpu | ram | ram5 | rom2 | cache0; at most 8, each "
        "at most 65536 bytes, returned as base64)";
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
    schema["properties"]["screen"]["type"] = "integer";
    schema["properties"]["screen"]["description"] =
        "'screen_attributes': which screen to read (0 = page 5, 1 = page 7 on shadow-capable models). Omitted reads both when shadow-capable, else just the one.";

    registry.Register(
        "inspect_state",
        "Inspect emulator state in one call: registers, memory ranges, disassembly, stack words, breakpoints, memory banks, "
        "paging state (tagged latches + bank table), static port map with tags (ports), video mode (video: resolution, colour depth, "
        "memory layout, displayed RAM pages, #EFF7/#DFFD/#FF77), screen state (screen: active screen and RAM pages, per-screen "
        "Z80 mapping, #7FFD, contention), FLASH timing (screen_flash), per-cell ink/paper/bright/flash (screen_attributes), "
        "screen OCR text, screen image metadata, screen digest hash, raster timing (timing: the beam and the layer pixel under it), "
        "the mode's layers and beam windows (video_layout), the exact text of ATM / ZX-Evo text modes (video_text; the pixel "
        "behind a point and the pixels a byte feeds: GET /video/pixel and /video/address through invoke_api), "
        "ROM signatures, AY/SSG chips (audio_ay), TurboSound FM YM2203 halves (audio_fm), General Sound card (audio_gs), Covox / SoundDrive (audio_covox), MoonSound OPL4 (audio_moonsound, audio_opl4_fm, audio_opl4_pcm), Beta Disk WD1793 (fdc), IDE board (ide), CMOS clock (rtc), ZX Profi board chips (profi), "
        "the machine's mouse device + port routing (mouse), time-travel session state and position (ttd), memory contention: rule, switch, "
        "interface, contended slots, per-kind waits while debugging (contention), the TS-Conf (tsconf, tsconf_tsu) and the "
        "Sprinter Sp2000 machines (sprinter, sprinter_ports, sprinter_text, sprinter_video, sprinter_palette, sprinter_sound_ring, "
        "sprinter_zx_mode, sprinter_pld_journal). "
        "Combine aspects to reduce round-trips.",
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
                    aspect != "screen" && aspect != "screen_flash" && aspect != "screen_attributes" && aspect != "screen_ocr" && aspect != "screen_image" && aspect != "screen_digest" && aspect != "timing" && aspect != "video_layout" && aspect != "video_text" && aspect != "rom" && aspect != "audio_ay" &&
                    aspect != "audio_fm" && aspect != "audio_gs" && aspect != "audio_covox" && aspect != "audio_moonsound" && aspect != "audio_opl4_fm" &&
                    aspect != "audio_opl4_pcm" && aspect != "fdc" && aspect != "ide" && aspect != "cdaudio" && aspect != "rtc" && aspect != "profi" && aspect != "isa" && aspect != "network" && aspect != "mouse" && aspect != "ttd" && aspect != "contention" &&
                    aspect != "tsconf" && aspect != "tsconf_tsu" && aspect != "sprinter" && aspect != "sprinter_ports" && aspect != "sprinter_text" &&
                    aspect != "sprinter_video" && aspect != "sprinter_palette" && aspect != "sprinter_sound_ring" && aspect != "sprinter_bios" &&
                    aspect != "sprinter_zx_mode" && aspect != "sprinter_pld_journal" &&
                    aspect != "memory_region" && aspect != "video_changes" && aspect != "audio_mixer" && aspect != "snapshot" && aspect != "pchist" &&
                    aspect != "slots" && aspect != "audio_multisound" && aspect != "audio_midi")
                {
                    done(ToolResult::Error("Unknown aspect '" + aspect +
                                            "'. Valid: machine, registers, memory, memory_map, disasm, stack, breakpoints, memory_banks, paging, ports, video, "
                                            "screen, screen_flash, screen_attributes, screen_ocr, screen_image, screen_digest, timing, video_layout, video_text, rom, audio_ay, audio_fm, audio_gs, audio_covox, audio_moonsound, audio_opl4_fm, audio_opl4_pcm, fdc, ide, cdaudio, rtc, profi, isa, mouse, ttd, contention, tsconf, tsconf_tsu, sprinter, sprinter_ports, sprinter_text, sprinter_video, sprinter_palette, sprinter_sound_ring, sprinter_bios, sprinter_zx_mode, sprinter_pld_journal, memory_region, video_changes, audio_mixer, snapshot, pchist, slots, audio_multisound, audio_midi"));
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
            const std::string region = args.isMember("region") && args["region"].isString() ? args["region"].asString() : "vram";
            // The PLD journal query (sprinter_pld_journal)
            std::string pldQuery = "?limit=40";
            if (args.isMember("pld_journal_kinds") && args["pld_journal_kinds"].isString())
                pldQuery += "&kinds=" + args["pld_journal_kinds"].asString();
            if (args.isMember("pld_journal_source") && args["pld_journal_source"].isString())
                pldQuery += "&source=" + args["pld_journal_source"].asString();
            const bool hasScreenArg = args.isMember("screen");
            const int screenArg = hasScreenArg ? args["screen"].asInt() : -1;
            // The snapshot's memory windows (GET /debug/snapshot?memory=...)
            std::string snapshotWindows;
            if (args.isMember("windows") && args["windows"].isArray())
                for (const Json::Value& window : args["windows"])
                    if (window.isString())
                        snapshotWindows += "&memory=" + window.asString();
            if (args.isMember("pchist") && args["pchist"].isIntegral())
                snapshotWindows += "&pchist=" + std::to_string(args["pchist"].asUInt());

            TargetResolver::ResolveFromArgs(
                args, caller,
                [aspects, address, hasAddress, size, count, includeImage, format, view, minRun, maxBlocks, hasScreenArg, screenArg, region, pldQuery, snapshotWindows, &caller, done, progress](
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
                        else if (aspect == "pchist")
                        {
                            const std::string path =
                                Endpoint(id, "/debug/pchist") + "?depth=" + std::to_string(std::min(count, 1024u));
                            steps.push_back([&caller, path, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", path, nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    acc[aspect] = std::move(body);
                                    if (status != 200)
                                        acc[aspect]["http_status"] = status;
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "snapshot")
                        {
                            // One coherent picture (GET /debug/snapshot): registers, previous registers, pages, stack,
                            // time, code from PC and the asked memory windows, read at one moment
                            const std::string path =
                                Endpoint(id, "/debug/snapshot") + "?disasm=" + std::to_string(std::min(count, 100u)) + snapshotWindows;
                            steps.push_back([&caller, path, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", path, nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    acc[aspect] = std::move(body);
                                    if (status != 200)
                                        acc[aspect]["http_status"] = status;
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
                        else if (aspect == "screen" || aspect == "screen_flash")
                        {
                            // Screen state (verbose: per-screen RAM page + Z80 mapping, #7FFD) and FLASH timing
                            const std::string path = aspect == "screen" ? "/state/screen?verbose=true" : "/state/screen/flash";
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "audio_mixer")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/audio/mixer"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "video_changes")
                        {
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/video/changes"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "video_layout" || aspect == "video_text")
                        {
                            // Video debug translation (PLAN #42): the mode's layers / the exact text of a text mode.
                            // Pixel sources and byte -> pixels take arguments: GET /video/pixel, /video/address via invoke_api
                            const std::string path = aspect == "video_layout" ? "/video/layout" : "/video/text";
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "screen_attributes")
                        {
                            // Per-cell ink/paper/bright/flash decoded from screen attribute memory
                            std::string path = "/state/screen/attributes";
                            if (hasScreenArg)
                                path += "?screen=" + std::to_string(screenArg);
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "video")
                        {
                            // Video mode: resolution, color depth, EFF7 state for Pentagon 16col/HWMC modes, PROFIHR 512x240 for Profi
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
                            // Core DeviceState::Fdc via the WebAPI (WD1793, or the uPD765A on a +3); 404 = no disk controller
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/fdc"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "ide")
                        {
                            // Core DeviceState::Ide via the WebAPI; 404 = no IDE board
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/ide"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "cdaudio")
                        {
                            // Core CdAudioControl::State via the WebAPI (available false without a CD drive)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/cdaudio"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "tsconf_tsu")
                        {
                            // Core DeviceState::TsConfTsu via the WebAPI; 404 = not a TS-Conf machine
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/tsconf/tsu"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "tsconf")
                        {
                            // Core DeviceState::TsConf via the WebAPI; 404 = not a TS-Conf machine
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/tsconf"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "sprinter" || aspect == "sprinter_ports" || aspect == "sprinter_text" ||
                                 aspect == "sprinter_video" || aspect == "sprinter_palette" || aspect == "sprinter_sound_ring" ||
                                 aspect == "sprinter_bios" || aspect == "sprinter_zx_mode" || aspect == "sprinter_pld_journal")
                        {
                            // Core DeviceState::Sprinter / SprinterPortTable / SprinterText / SprinterVideo /
                            // SprinterPalette / SprinterSoundRing via the WebAPI; 404 = not a Sprinter
                            const std::string path = aspect == "sprinter"            ? "/state/sprinter"
                                                     : aspect == "sprinter_ports"    ? "/state/sprinter/ports"
                                                     : aspect == "sprinter_video"    ? "/state/sprinter/video?squares=0"
                                                     : aspect == "sprinter_palette"  ? "/state/sprinter/palette"
                                                     : aspect == "sprinter_sound_ring" ? "/state/sprinter/sound/ring"
                                                     : aspect == "sprinter_bios"     ? "/state/sprinter/bios"
                                                     : aspect == "sprinter_zx_mode"  ? "/state/sprinter/zx-mode"
                                                     : aspect == "sprinter_pld_journal" ? "/state/sprinter/pld-journal" + pldQuery
                                                                                     : "/state/sprinter/text";
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "memory_region")
                        {
                            // Core DeviceState::MemoryRegionRead via the WebAPI; 404 / 400 = no such region or range
                            const std::string path = "/memory/region/" + region + "?offset=" + std::to_string(address) +
                                                     "&length=" + std::to_string(size) + "&format=hex";
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "rtc")
                        {
                            // Core DeviceState::Rtc via the WebAPI; 404 = no CMOS clock
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/rtc"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "profi")
                        {
                            // Core DeviceState::ProfiPeripherals via the WebAPI; 404 = not a ZX Profi
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/profi"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "isa")
                        {
                            // Core DeviceState::Isa via the WebAPI; 404 = no ISA slots
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/isa"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "network")
                        {
                            // Core DeviceState::Network via the WebAPI; available=false without an adapter
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/network"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "contention")
                        {
                            // Core DeviceState::Contention via the WebAPI
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/contention"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
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
                        else if (aspect == "ttd")
                        {
                            // TTD session: GET /ttd/status, then GET /ttd/position merged
                            // in as "position" (skipped when the engine is not compiled in)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/ttd/status"), nullptr, [&caller, id, aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status != 200)
                                    {
                                        next(true);
                                        return;
                                    }
                                    const bool available = body["ttd_available"].asBool();
                                    acc[aspect] = std::move(body);
                                    if (!available)
                                    {
                                        next(true);
                                        return;
                                    }
                                    caller.Call("GET", Endpoint(id, "/ttd/position"), nullptr, [aspect, &acc, next](int posStatus, Json::Value position) mutable {
                                        if (posStatus == 200)
                                        {
                                            acc[aspect]["position"] = std::move(position);
                                        }
                                        next(true);
                                    });
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
                        else if (aspect == "audio_moonsound" || aspect == "audio_opl4_fm" || aspect == "audio_opl4_pcm")
                        {
                            // MoonSound via the WebAPI (DeviceState::MoonSound*); 404 = not fitted
                            const std::string path = aspect == "audio_opl4_fm"    ? "/state/audio/moonsound/fm"
                                                     : aspect == "audio_opl4_pcm" ? "/state/audio/moonsound/pcm"
                                                                                  : "/state/audio/moonsound";
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "audio_multisound" || aspect == "audio_midi")
                        {
                            // ZX-MultiSound / its MIDI synthesizer via the WebAPI (DeviceState::MultiSound / Midi); 404 = not fitted
                            const std::string path = aspect == "audio_midi" ? "/state/audio/midi" : "/state/audio/multisound";
                            steps.push_back([&caller, id, aspect, path](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, path), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "slots")
                        {
                            // The slot report via the WebAPI (SlotControl list = DeviceState::Slots)
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/slots"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200) acc[aspect] = std::move(body);
                                    else { acc[aspect] = Json::Value(Json::objectValue); acc[aspect]["available"] = false; acc[aspect]["description"] = body.isMember("message") ? body["message"] : Json::Value("unavailable"); }
                                    next(true);
                                });
                            });
                        }
                        else if (aspect == "audio_covox")
                        {
                            // Covox / SoundDrive via the WebAPI (DeviceState::Covox); 404 = not fitted
                            steps.push_back([&caller, id, aspect](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, "/state/audio/covox"), nullptr, [aspect, &acc, next](int status, Json::Value body) mutable {
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
                            else if (aspect == "snapshot" && value.isMember("regs"))
                            {
                                const Json::Value& special = value["regs"]["special"];
                                char pc[8];
                                std::snprintf(pc, sizeof(pc), "%04X", special["pc"].asUInt());
                                out << "\n[snapshot] seq " << value["seq"].asUInt64() << ", " << value["state"].asString()
                                    << " (read " << value["consistency"].asString() << "), last stop "
                                    << value["pause"]["reason"].asString() << ", PC=" << pc << ", frame "
                                    << value["time"]["frame"].asUInt64() << " t " << value["time"]["t"].asUInt64() << ", "
                                    << value["disasm"].size() << " code line(s), " << value["memory"].size() << " memory window(s)";
                            }
                            else if (aspect == "pchist" && value.isMember("entries"))
                            {
                                out << "\n[pchist] " << value["entries"].size() << " of " << value["total"].asUInt64()
                                    << (value["started_now"].asBool() ? " (recording started now)" : "") << ":";
                                for (const Json::Value& entry : value["entries"])
                                {
                                    char item[24];
                                    std::snprintf(item, sizeof(item), " %04X %s%u", entry["address"].asUInt(),
                                                  entry["kind"].asString().c_str(), entry["page"].asUInt());
                                    out << item;
                                }
                            }
                            else if (aspect == "snapshot" && value.isMember("message"))
                                out << "\n[snapshot] refused: " << value["message"].asString();
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
                            else if (aspect == "screen_digest" && value.isMember("combined"))
                            {
                                out << "\n[screen_digest] " << value["combined"].asString();
                            }
                            else if (aspect == "contention")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[contention] " << value["description"].asString();
                                else
                                {
                                    out << "\n[contention] rule " << value["rule"].asString() << ", switch " << value["switch"].asString()
                                        << (value["effective"].asBool() ? ", in effect" : ", not in effect") << ", interface "
                                        << value["memory_interface"].asString() << ", io " << value["io_rule"].asString() << ", slots";
                                    const Json::Value& slots = value["slots"];
                                    for (Json::ArrayIndex i = 0; i < slots.size(); ++i)
                                        out << " " << (slots[i]["contended"].asBool() ? "C" : "-");
                                    if (value["even_m1"].asBool())
                                        out << ", Even M1";
                                    if (value.isMember("scorpion_turbo_logic"))
                                        out << ", Turbo+ logic " << value["scorpion_turbo_logic"].asString();
                                    if (value.isMember("atm710_turbo_waits"))
                                        out << ", 7 MHz RAM waits " << value["atm710_turbo_waits"].asString();
                                    if (value["statistics"].isObject())
                                    {
                                        const Json::Value& last = value["statistics"]["last_frame"];
                                        out << "; last frame " << last["accesses"].asUInt64() << " contended accesses, "
                                            << last["wait_t"].asUInt64() << " T waited";
                                    }
                                }
                            }
                            else if (aspect == "network")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[network] " << value["description"].asString();
                                else
                                {
                                    const Json::Value& card = value["card"];
                                    const Json::Value& net = value["virtual_network"];
                                    const Json::Value& com = value["com_port"];
                                    if (card["kind"].asString() != "none")
                                        out << "\n[network] " << card["kind"].asString() << (card["w5300_running"].asBool() ? " running" : " in reset")
                                            << (card["int_to_z80"].asBool() ? ", /INT low" : "") << ", ip " << card["ip"].asString() << ", " << net["sockets"].size() << " socket(s), "
                                            << net["dhcp_leases"].size() << " lease(s), host access " << (net["host_access"].asBool() ? "on" : "off")
                                            << ", guest servers listen on " << net["listen_address"].asString()
                                            << (net["remote_access"].asBool() ? " (remote access on)" : " (this computer only)");
                                    if (com["fitted"].asBool())
                                        out << "\n[com] " << com["flavor"].asString()
                                            << (com.isMember("avr_firmware") ? " (" + com["avr_firmware"].asString() + ")" : std::string())
                                            << " UART, " << com["peer"].asString()
                                            << (com.isMember("target") ? " " + com["target"].asString() : std::string())
                                            << (com["connected"].asBool() ? "" : " (not connected)") << ", " << com["baud"].asUInt() << " baud, rx "
                                            << com["rx_fifo"].asInt() << " / tx " << com["tx_fifo"].asInt() << " in FIFO, in " << com["bytes_in"].asUInt64()
                                            << " / out " << com["bytes_out"].asUInt64() << " bytes";
                                    const Json::Value& machineSerial = value["machine_serial"];
                                    if (machineSerial["fitted"].asBool())
                                        out << (machineSerial["flavor"].asString() == "usart8251"
                                                    ? std::string("\n[com] 8251 COM port, ")
                                                    : "\n[com] keyboard controller " + machineSerial["kbc_firmware"].asString() + " RS-232, ")
                                            << machineSerial["peer"].asString()
                                            << (machineSerial.isMember("target") ? " " + machineSerial["target"].asString() : std::string())
                                            << (machineSerial["connected"].asBool() ? "" : " (not connected)") << ", " << machineSerial["baud"].asUInt()
                                            << " baud"
                                            << (machineSerial.isMember("peer_baud") ? " (module " + std::to_string(machineSerial["peer_baud"].asUInt()) + ")" : std::string())
                                            << ", RTS " << (machineSerial["rts"].asBool() ? "on" : "off") << ", in "
                                            << machineSerial["bytes_in"].asUInt64() << " / out " << machineSerial["bytes_out"].asUInt64() << " bytes, lost "
                                            << machineSerial["lost"].asUInt64();
                                    const Json::Value& ioEsp = value["atm2ioesp"];
                                    if (ioEsp["fitted"].asBool())
                                        out << "\n[com] ATM2IOESP at " << ioEsp["address"].asString() << ", " << ioEsp["peer"].asString()
                                            << (ioEsp.isMember("target") ? " " + ioEsp["target"].asString() : std::string())
                                            << (ioEsp["connected"].asBool() ? "" : " (not connected)") << ", " << ioEsp["baud"].asUInt()
                                            << " baud, rx " << ioEsp["rx_fifo"].asInt() << " / tx " << ioEsp["tx_fifo"].asInt() << " in FIFO, in "
                                            << ioEsp["bytes_in"].asUInt64() << " / out " << ioEsp["bytes_out"].asUInt64() << " bytes, overruns "
                                            << ioEsp["overruns"].asUInt64();
                                    const Json::Value& zifi = value["zifi"];
                                    if (zifi["fitted"].asBool())
                                        out << "\n[zifi] " << zifi["avr_firmware"].asString() << " API " << zifi["api"].asInt() << ", data register "
                                            << zifi["data_register"].asString() << ", ISR " << zifi["isr"].asString() << " IMR " << zifi["imr"].asString()
                                            << ", rings zifi " << zifi["zifi_rx"].asInt() << " in / " << zifi["zifi_tx"].asInt() << " out, rs232 "
                                            << zifi["rs232_rx"].asInt() << " / " << zifi["rs232_tx"].asInt() << "; line "
                                            << (zifi.isMember("peer") ? zifi["peer"].asString() : std::string("none"))
                                            << (zifi.isMember("target") ? " " + zifi["target"].asString() : std::string())
                                            << (zifi.isMember("esp") ? " [" + zifi["esp"]["firmware"].asString() +
                                                                           (zifi["esp"].isMember("native_session")
                                                                                ? ", " + zifi["esp"]["native_session"]["activity"].asString() +
                                                                                      (zifi["esp"]["native_session"]["last_error"].asString().empty()
                                                                                           ? std::string()
                                                                                           : ", last error \"" + zifi["esp"]["native_session"]["last_error"].asString() + "\"") +
                                                                                      (zifi["esp"]["native_session"]["file_bridge"]["ftp"]["running"].asBool()
                                                                                           ? ", FTP on " + zifi["esp"]["native_session"]["file_bridge"]["ftp"]["port"].asString() +
                                                                                                 " (host " + zifi["esp"]["native_session"]["file_bridge"]["ftp"]["host_port"].asString() +
                                                                                                 "), " + std::to_string(zifi["esp"]["native_session"]["file_bridge"]["ftp"]["sessions"].size()) +
                                                                                                 " session(s)"
                                                                                           : std::string()) +
                                                                                      (zifi["esp"]["native_session"]["file_bridge"]["wc_update"]["running"].asBool()
                                                                                           ? ", WC update: " + zifi["esp"]["native_session"]["file_bridge"]["wc_update"]["state"].asString()
                                                                                           : std::string())
                                                                                : std::string()) +
                                                                           "]"
                                                                     : std::string())
                                            << ", in " << zifi["bytes_in"].asUInt64() << " / out " << zifi["bytes_out"].asUInt64() << " bytes, dropped "
                                            << zifi["dropped"].asUInt64();
                                    // A UART line at a glance: its peer, and a Hayes modem's mode, call and lines
                                    auto uartLine = [](const Json::Value& ch, const std::string& name) {
                                        std::string line = "; " + name + " " + ch["base"].asString() + ", UART " +
                                                           std::to_string(ch["uart"]["baud"].asUInt64()) + " baud MCR " +
                                                           ch["uart"]["mcr"].asString() + ", " + ch["peer"]["kind"].asString();
                                        if (ch.isMember("modem"))
                                        {
                                            const Json::Value& m = ch["modem"];
                                            line += " " + m["mode"].asString() + " " + m["call"]["dialed"].asString() + ", DCD " +
                                                    (m["lines"]["dcd"].asBool() ? "on" : "off") +
                                                    (m["lines"]["ri"].asBool() ? ", RINGING" : "") + ", last " +
                                                    m["last_result"].asString();
                                        }
                                        else if (!ch["peer"]["target"].asString().empty())
                                            line += " " + ch["peer"]["target"].asString();
                                        return line;
                                    };
                                    for (const Json::Value& slot : value["slots"])
                                        out << "\n[network] " << slot["label"].asString() << ": " << slot["card"].asString()
                                            << (slot.isMember("chip") ? " " + slot["chip"].asString() + " at " + slot["base"].asString() +
                                                                            (slot.isMember("mac") ? ", MAC " + slot["mac"].asString() : std::string())
                                                                      : std::string())
                                            << (slot.isMember("esp")
                                                    ? ", IRQ " + std::to_string(slot["irq"].asInt()) + ", UART " + std::to_string(slot["uart"]["baud"].asUInt64()) +
                                                          " baud MCR " + slot["uart"]["mcr"].asString() + "; ESP " + slot["esp"]["firmware"].asString() + " " +
                                                          slot["esp"]["state"].asString() + ", Wi-Fi " + slot["esp"]["wifi"].asString() + " " +
                                                          slot["esp"]["ip"].asString() + ", " + std::to_string(slot["esp"]["at_session"]["links"].size()) +
                                                          " links, " + std::to_string(slot["esp"]["requests"].asUInt64()) + " AT requests"
                                                    : std::string())
                                            << (slot.isMember("id_port") ? "; " + slot["summary"].asString() : std::string())
                                            << (slot["card"].asString() == "modem" ? uartLine(slot, "line") : std::string())
                                            << (slot["card"].asString() == "dual16552"
                                                    ? uartLine(slot, "COM1") + uartLine(slot["channel_b"], "COM2")
                                                    : std::string());
                                    if (value.isMember("ethernet_gateway"))
                                    {
                                        const Json::Value& gw = value["ethernet_gateway"];
                                        out << "\n[network] ethernet gateway " << gw["router_ip"].asString() << ": "
                                            << gw["tcp"].size() << " TCP, " << gw["udp"].size() << " UDP flows, frames "
                                            << gw["counters"]["frames_from_cards"].asUInt64() << " from / "
                                            << gw["counters"]["frames_to_cards"].asUInt64() << " to the cards";
                                        for (const Json::Value& port : gw["ports"])
                                            out << "; " << port["port"].asString() << " lease " << port["dhcp_lease"].asString();
                                    }
                                    for (const Json::Value& note : value["not_fitted"])
                                        out << "\n[network] " << note.asString();
                                    const Json::Value& set = value["settings"];
                                    if (set.isObject())
                                        out << "\n[network] settings: card " << set["card"].asString() << ", com_port " << set["com_port"].asString()
                                            << ", zx_wifi " << set["zx_wifi"].asString() << ", esp_chip " << set["esp_chip"].asString()
                                            << (value["machine"]["serial_port"].asString() == "evo-avr" ? ", avr_firmware " + set["avr_firmware"].asString() : std::string())
                                            << (set.isMember("kbc_firmware") ? ", kbc_firmware " + set["kbc_firmware"].asString() : std::string())
                                            << (value["machine"]["zifi"].asBool() ? ", zifi " + set["zifi"].asString() : std::string());
                                }
                            }
                            else if (aspect == "isa")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[isa] " << value["description"].asString();
                                else
                                {
                                    out << "\n[isa] latch " << value["latch"]["value"].asString()
                                        << (value["window"]["mapped"].asBool() ? ", window 3 = slot " + std::to_string(value["window"]["slot"].asInt()) + " " + value["window"]["space"].asString() : std::string(", window 3 not mapped"));
                                    for (const Json::Value& slot : value["slots"])
                                        out << "; slot " << slot["slot"].asInt() << ": " << slot["card"].asString()
                                            << (slot.isMember("not_fitted") ? " (" + slot["configured"].asString() + " not fitted: " + slot["not_fitted"].asString() + ")" : std::string());
                                    // What each card uses and how the Z80 reaches it; conflicts; the access journal's size
                                    for (const Json::Value& slot : value["slots"])
                                    {
                                        if (!slot["enabled"].asBool())
                                            continue;
                                        out << "\n[isa] slot " << slot["slot"].asInt() << " " << slot["card"].asString() << ": I/O "
                                            << slot["resources"]["io"].asString() << ", memory " << slot["resources"]["memory"].asString()
                                            << ", IRQ " << slot["resources"]["irq"].asString() << " (" << slot["resources"]["irq_route"].asString() << ")";
                                        if (slot["z80_access"].isMember("io"))
                                            out << "\n[isa]   Z80: " << slot["z80_access"]["io"].asString();
                                        if (slot["resources"].isMember("id_port"))
                                            out << "\n[isa]   ID port " << slot["resources"]["id_port"]["io"].asString() << " ("
                                                << slot["resources"]["id_port"]["z80"].asString() << "): "
                                                << slot["resources"]["id_port"]["note"].asString();
                                        // The ZX-bus adapter: the card behind it, its ports and reset (ISA phase I2)
                                        if (slot.isMember("zx_bus"))
                                        {
                                            out << "\n[isa]   " << slot["summary_line"].asString();
                                            for (const Json::Value& card : slot["zx_bus"]["cards"])
                                                out << "\n[isa]   " << card["device"].asString() << ": status "
                                                    << card["status"].asString() << ", " << card["sound"].asString() << "; "
                                                    << card["machine_reset"].asString();
                                            if (slot["zx_bus"].isMember("empty"))
                                                out << "\n[isa]   ZX-bus empty: " << slot["zx_bus"]["empty"].asString();
                                        }
                                    }
                                    if (value.isMember("irq_summary"))
                                        out << "\n[isa] irq: " << value["irq_summary"].asString();
                                    for (const Json::Value& conflict : value["conflicts"])
                                        out << "\n[isa] conflict: " << (conflict.isString() ? conflict.asString() : conflict.toStyledString());
                                    out << "\n[isa] journal: " << value["journal_entries"].asUInt64() << " accesses (GET /state/isa/journal via invoke_api)";
                                }
                            }
                            else if (aspect == "rtc")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[rtc] " << value["description"].asString();
                                else
                                    out << "\n[rtc] " << value["chip"].asString() << ", " << value["time"]["text"].asString()
                                        << " (" << value["time_mode"].asString() << " time), " << value["cells"].asInt() << " cells";
                            }
                            else if (aspect == "profi")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[profi] " << value["description"].asString();
                                else
                                    out << "\n[profi] board " << value["board"].asString() << ", ExtPorts="
                                        << value["port_map"]["ext_ports"].asString() << ", extended map "
                                        << (value["port_map"]["extended_map"].asBool() ? "open" : "closed") << ", 8255 control #"
                                        << std::hex << value["ppi8255"]["control"].asInt() << std::dec;
                            }
                            else if (aspect == "tsconf_tsu")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[tsconf_tsu] " << value["description"].asString();
                                else
                                    out << "\n[tsconf_tsu] t_config " << value["t_config"].asInt() << ", " << value["active_sprites"].asInt()
                                        << " active sprites, sprite page " << value["sprite_page"].asInt() << ", tilemap page "
                                        << value["tilemap_page"].asInt();
                            }
                            else if (aspect == "sprinter")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter] PLD " << value["pld"]["state"].asString() << " (" << value["pld"]["module"].asString()
                                        << "), map " << value["decoder"]["map"].asInt() << ", DOS " << (value["decoder"]["dos"].asBool() ? "on" : "off")
                                        << ", " << value["clock"]["mhz"].asString() << " MHz, " << value["frame"]["lines"].asInt() << " lines, "
                                        << value["video"]["picture_mode"].asString();
                                    for (const Json::Value& window : value["windows"])
                                        out << "\n  window " << window["window"].asInt() << ": " << window["kind"].asString() << " "
                                            << window["page_hex"].asString();
                                    const Json::Value& accel = value["accelerator"];
                                    if (accel["available"].asBool())
                                        out << "\n  accelerator " << (accel["enabled"].asBool() ? "enabled" : "disabled") << ", mode "
                                            << accel["mode_name"].asString() << ", length " << accel["length"].asUInt() << ", "
                                            << accel["function"].asString() << (accel["blocked"].asBool() ? ", blocked by INT" : "")
                                            << ", " << accel["operations"].asUInt64() << " operations";
                                    const Json::Value& cbl = value["sound"]["covox_blaster"];
                                    if (cbl.isObject())
                                    {
                                        out << "\n  sound: " << (cbl["mode"].asString() == "covox-blaster" ? "CBL" : "Covox");
                                        if (cbl["mode"].asString() == "covox-blaster")
                                            out << " " << cbl["bits"].asInt() << "-bit " << (cbl["stereo"].asBool() ? "stereo" : "mono") << " "
                                                << cbl["rate_hz"].asDouble() << " Hz, ring play " << cbl["play_index"].asString() << " / write "
                                                << cbl["write_index"].asString() << (cbl["int_pending"].asBool() ? ", INT pending" : "");
                                        out << ", DAC " << cbl["dac_left"].asString() << " / " << cbl["dac_right"].asString() << ", AY "
                                            << value["sound"]["ay"]["stereo"].asString();
                                    }
                                    const Json::Value& waits = value["clock"]["waits"];
                                    if (waits["active"].asBool())
                                        out << "\n  21 MHz waits on main RAM windows";
                                    if (value["clock"]["original_waits"]["active"].asBool())
                                        out << "\n  original waits (ALL_MODE bit 2 = 0): screen memory waits on the 4-T CT5 period";
                                    if (value["tape"]["time_base"].asString() == "base_clock")
                                        out << "\n  tape in real time (load in a 3.5 MHz mode)";
                                }
                            }
                            else if (aspect == "audio_mixer")
                            {
                                out << "\n[audio_mixer] master " << (value["master"]["muted"].asBool() ? "muted" : "on");
                                for (const Json::Value& d : value["devices"])
                                    out << "\n  " << d["source"].asString() << " (" << d["name"].asString() << "): "
                                        << (d["muted"].asBool() ? "muted" : "on") << (d["solo"].asBool() ? ", solo" : "") << ", volume "
                                        << d["volume"].asDouble() << ", peak " << d["peak"].asDouble() << (d["active"].asBool() ? ", active" : "");
                            }
                            else if (aspect == "video_changes")
                            {
                                out << "\n[video_changes]" << (value["running"].asBool() ? " (running: the last completed frame)" : "");
                                for (const Json::Value& frame : value["frames"])
                                {
                                    const Json::Value& tables = frame["tables"];
                                    out << "\n  frame " << frame["frame"].asUInt64() << (frame["current"].asBool() ? " (current)" : "") << ": "
                                        << frame["writes"].size() << " latch changes" << (frame["partial"].asBool() ? " (log full)" : "")
                                        << ", palette writes " << tables["palette"]["count"].asUInt() << ", mode table writes "
                                        << tables["mode_table"]["count"].asUInt();
                                    Json::ArrayIndex shown = 0;
                                    for (const Json::Value& w : frame["writes"])
                                    {
                                        if (++shown > 24)
                                        {
                                            out << "\n    ... (" << frame["writes"].size() << " in the JSON)";
                                            break;
                                        }
                                        out << "\n    T " << w["t"].asUInt() << " (line " << w["line"].asUInt() << ", T " << w["t_in_line"].asUInt()
                                            << ") PC " << w["pc"].asString() << ":";
                                        for (const std::string& key : w["changes"].getMemberNames())
                                            out << " " << key << " " << w["changes"][key].asString();
                                    }
                                }
                            }
                            else if (aspect == "memory_region")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[memory_region] " << value["description"].asString();
                                else
                                {
                                    out << "\n[memory_region] " << value["region"].asString() << " " << value["offset"].asString() << ", "
                                        << value["length"].asUInt() << " bytes";
                                    const std::string hex = value["hex"].asString();
                                    unsigned base = 0;
                                    try { base = static_cast<unsigned>(std::stoul(value["offset"].asString(), nullptr, 16)); } catch (...) {}
                                    for (size_t i = 0; i < hex.size() && i < 512; i += 32)
                                    {
                                        char at[16];
                                        std::snprintf(at, sizeof at, "%05X:", static_cast<unsigned>(base + i / 2));
                                        out << "\n  " << at;
                                        for (size_t j = i; j < i + 32 && j + 1 < hex.size(); j += 2)
                                            out << " " << hex.substr(j, 2);
                                    }
                                    if (hex.size() > 512)
                                        out << "\n  ... (" << hex.size() / 2 << " bytes in the JSON)";
                                }
                            }
                            else if (aspect == "sprinter_video")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_video] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_video] page " << value["mode_page"].asInt() << (value["displayed"].asBool() ? "" : " (not displayed)")
                                        << ", RGMOD " << value["rgmod"].asString() << ", HOLD " << value["hold"]["value"].asString() << ", "
                                        << value["frame"]["lines"].asInt() << " lines, PORT_Y " << value["port_y"].asString() << ", palettes";
                                    for (const Json::Value& k : value["palettes_used"])
                                        out << " " << k.asInt();
                                    out << "\n  " << value["legend"].asString();
                                    for (const Json::Value& line : value["map"])
                                        out << "\n  " << line.asString();
                                }
                            }
                            else if (aspect == "sprinter_palette")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_palette] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_palette] " << value["selection"].asString() << " (R G B per pen)";
                                    for (const Json::Value& p : value["palettes"])
                                        out << "\n  " << p["k"].asInt() << " " << p["role"].asString() << ": "
                                            << p["rgb_row"].asString().substr(0, 16 * 7 - 1) << " ...";
                                }
                            }
                            else if (aspect == "sprinter_bios")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_bios] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_bios] loaded " << value["loaded"].asString() << ", configured "
                                        << value["rom_file"].asString() << (value["reload_pending"].asBool() ? " (loads at the next reset)" : "")
                                        << ", fast_start " << (value["options"]["fast_start"].asBool() ? "on" : "off")
                                        << ", accel_int_suspend " << (value["options"]["accel_int_suspend"].asBool() ? "on" : "off");
                                    for (const Json::Value& image : value["images"])
                                        out << "\n  " << image["alias"].asString() << " " << image["file"].asString()
                                            << (image["present"].asBool() ? "" : " (not installed)") << (image["loaded"].asBool() ? " [loaded]" : "");
                                    for (const Json::Value& issue : value["known_issues"])
                                        out << "\n  KNOWN ISSUE: " << issue.asString();
                                }
                            }
                            else if (aspect == "sprinter_zx_mode")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_zx_mode] " << value["description"].asString();
                                else
                                {
                                    const Json::Value& best = value["config"]["best_match"];
                                    out << "\n[sprinter_zx_mode] " << value["summary"].asString() << "; best match "
                                        << best["file"].asString() << " (" << best["name"].asString() << ", " << best["launcher"].asString()
                                        << "), confidence " << best["confidence"].asString() << ": " << best["explanation"].asString();
                                    out << "\n  options " << value["config"]["option_line"].asString() << ", CNF "
                                        << value["config"]["cnf"].asString() << ", ALL_MODE " << value["config"]["all_mode"].asString();
                                    out << "\n  clock " << value["clock"]["why"].asString();
                                    out << "\n  frame " << value["frame"]["lines"].asInt() << " lines, INT "
                                        << value["frame"]["int"]["kind"].asString() << " line " << value["frame"]["int"]["line"].asInt()
                                        << " T " << value["frame"]["int"]["t_in_line"].asInt() << "; ROMs " << value["rom"]["set_name"].asString();
                                    if (value["launcher"]["mode_text_found"].asBool())
                                        out << "\n  launcher RAM: \"" << value["launcher"]["mode_name"].asString() << "\" "
                                            << value["launcher"]["option_line"].asString();
                                    for (const Json::Value& row : value["ports"]["rows"])
                                        out << "\n  OUT " << row["port"].asString() << " -> " << row["tr_dos_off"]["out"]["code"].asString() << " "
                                            << row["tr_dos_off"]["out"]["name"].asString() << ": " << row["tr_dos_off"]["out"]["effect"].asString();
                                }
                            }
                            else if (aspect == "sprinter_pld_journal")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_pld_journal] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_pld_journal] source " << value["source"].asString() << ", "
                                        << value["events"].size() << " event(s)";
                                    if (value.isMember("error"))
                                        out << ": " << value["error"].asString();
                                    for (const Json::Value& e : value["events"])
                                        out << "\n  frame " << e["frame"].asUInt64() << " T " << e["t"].asUInt() << " PC " << e["pc"].asString()
                                            << " " << e["kind"].asString() << ": " << e["text"].asString();
                                }
                            }
                            else if (aspect == "sprinter_sound_ring")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_sound_ring] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_sound_ring] " << value["mode"].asString() << ", play " << value["play_index"].asString()
                                        << ", write " << value["write_index"].asString();
                                    for (const Json::Value& row : value["rows"])
                                        out << "\n  " << row.asString();
                                }
                            }
                            else if (aspect == "sprinter_text")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_text] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_text] " << value["text_squares"].asInt() << " text squares, mode page "
                                        << value["mode_page"].asInt();
                                    for (const Json::Value& line : value["lines"])
                                        if (!line["text"].asString().empty())
                                            out << "\n  " << line["text"].asString();
                                }
                            }
                            else if (aspect == "sprinter_ports")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[sprinter_ports] " << value["description"].asString();
                                else
                                {
                                    out << "\n[sprinter_ports] map " << value["map"].asInt() << ", DOS " << (value["dos"].asBool() ? "on" : "off")
                                        << ", PN5 " << (value["pn5"].asBool() ? 1 : 0) << ": " << value["rows"].size() << " rows";
                                    for (const Json::Value& row : value["rows"])
                                        out << "\n  " << row["code"].asString() << " " << row["direction"].asString() << " " << row["pattern"].asString()
                                            << "  " << row["name"].asString();
                                }
                            }
                            else if (aspect == "tsconf")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[tsconf] " << value["description"].asString();
                                else
                                    out << "\n[tsconf] " << value["video"]["mode"].asString() << " " << value["video"]["geometry"].asString()
                                        << ", page " << value["video"]["v_page"].asInt() << ", " << value["cpu_clock"].asString()
                                        << ", DMA " << (value["dma"]["busy"].asBool() ? value["dma"]["task"].asString() : std::string("idle"))
                                        << ", sprites " << value["video"]["tsu"]["active_sprites"].asInt();
                            }
                            else if (aspect == "cdaudio")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[cdaudio] " << (value.isMember("reason") ? value["reason"] : value["description"]).asString();
                                else
                                {
                                    for (const Json::Value& drive : value["drives"])
                                    {
                                        const Json::Value& audio = drive["audio"];
                                        out << "\n[cdaudio] " << drive["slot"].asString() << ": ";
                                        if (drive["disc"].isObject())
                                        {
                                            out << drive["disc"]["format"].asString() << " tracks " << drive["disc"]["first_track"].asInt() << "-"
                                                << drive["disc"]["last_track"].asInt();
                                            if (drive["disc"]["sessions"].asInt() > 1)
                                                out << " in " << drive["disc"]["sessions"].asInt() << " sessions";
                                            out << ", ";
                                        }
                                        else
                                            out << "no disc, ";
                                        out << audio["status"].asString() << " at " << audio["msf"].asString();
                                        if (audio.isMember("track"))
                                            out << " (track " << audio["track"].asInt() << " index " << audio["index"].asInt() << ", "
                                                << audio["relative_msf"].asString() << ")";
                                        out << ", volume L" << drive["drive_volume"]["left"].asInt() << " R" << drive["drive_volume"]["right"].asInt();
                                    }
                                }
                            }
                            else if (aspect == "ide")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[ide] " << value["description"].asString();
                                else
                                {
                                    out << "\n[ide] " << value["scheme"].asString() << ", selected ";
                                    if (value.isMember("selected_channel"))
                                        out << value["selected_channel"].asString() << " ";
                                    out << value["selected"].asString();
                                    if (value["adapter"].isMember("data_latch"))
                                        out << ", data latch #" << std::hex << value["adapter"]["data_latch"].asUInt() << std::dec;
                                    for (const Json::Value& unit : value["units"])
                                    {
                                        out << "\n  " << unit["slot"].asString() << " (" << unit["kind"].asString() << "): ";
                                        if (unit["medium"].isObject())
                                            out << unit["medium"]["description"].asString();
                                        else
                                            out << "no medium";
                                        out << ", status #" << std::hex << unit["task_file"]["status"].asUInt() << std::dec
                                            << ", last " << unit["command"]["name"].asString();
                                    }
                                }
                            }
                            else if (aspect == "fdc")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[fdc] " << value["description"].asString();
                                else if (value.isMember("phase"))
                                {
                                    // +3 uPD765A
                                    const Json::Value& cmd = value["command"];
                                    out << "\n[fdc] " << value["controller"].asString() << ": " << value["phase"].asString()
                                        << " phase, last " << cmd["name"].asString();
                                    if (cmd.isMember("r"))
                                        out << " C" << cmd["c"].asInt() << " H" << cmd["h"].asInt() << " R" << cmd["r"].asInt()
                                            << " N" << cmd["n"].asInt();
                                    out << ", MSR " << value["main_status"]["value"].asUInt() << ", ST0 "
                                        << value["status"]["st0"].asUInt() << " (" << value["status"]["interrupt_code"].asString()
                                        << ")" << (value["motor_on"].asBool() ? ", motor on" : ", motor off");
                                    const Json::Value& drives = value["drives"];
                                    for (Json::ArrayIndex i = 0; i < drives.size(); ++i)
                                        if (drives[i]["present"].asBool() && drives[i]["inserted"].asBool())
                                            out << "\n  " << drives[i]["letter"].asString() << ": " << drives[i]["path"].asString()
                                                << " track " << drives[i]["track"].asInt()
                                                << (drives[i]["write_protected"].asBool() ? " wp" : "");
                                }
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
                            else if (aspect == "audio_moonsound" || aspect == "audio_opl4_fm" || aspect == "audio_opl4_pcm")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[" << aspect << "] " << value["description"].asString();
                                else if (aspect == "audio_moonsound")
                                {
                                    out << "\n[audio_moonsound] NEW " << (value["new_mode"].asBool() ? "on" : "off") << ", NEW2 "
                                        << (value["new2_mode"].asBool() ? "on" : "off") << ", FM keyed "
                                        << value["fm_keyed_channels"].size() << ", PCM keyed " << value["pcm_keyed_slots"].size()
                                        << ", wave RAM " << value["wave_memory"]["ram_bytes"].asUInt64() / 1024 << " KB";
                                }
                                else if (aspect == "audio_opl4_fm")
                                {
                                    out << "\n[audio_opl4_fm]";
                                    for (const Json::Value& ch : value["channels"])
                                        if (ch["key_on"].asBool())
                                            out << "\n  ch" << ch["channel"].asUInt() << ": " << ch["frequency_hz"].asDouble()
                                                << " Hz (fnum " << ch["fnum"].asUInt() << ", block " << ch["block"].asUInt() << ")";
                                }
                                else
                                {
                                    out << "\n[audio_opl4_pcm]";
                                    for (const Json::Value& slot : value["slots"])
                                        if (slot["key_on"].asBool())
                                            out << "\n  slot" << slot["slot"].asUInt() << ": wave " << slot["wave"].asUInt()
                                                << ", " << slot["playback_rate_hz"].asDouble() << " Hz, "
                                                << slot["envelope"]["phase"].asString();
                                }
                            }
                            else if (aspect == "audio_multisound")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[audio_multisound] " << value["description"].asString();
                                else
                                {
                                    out << "\n[audio_multisound] " << value["card"].asString() << " in " << value["slot"].asString()
                                        << ", " << value["options"]["text"].asString() << "; FM "
                                        << (value["logic"]["fm_muted"].asBool() ? "muted" : "on") << ", SAA "
                                        << (value["saa"]["sound_enabled"].asBool() ? "on" : "off") << ", GS "
                                        << (value["gs"]["firmware_ready"].asBool() ? "ready" : "booting") << ", MIDI "
                                        << value["midi"]["bytes_received"].asUInt64() << " byte(s), "
                                        << value["midi"]["active_voices"].asUInt() << " voice(s)";
                                    for (const Json::Value& device : value["shadowed_devices"])
                                        out << "\n  built-in " << device["id"].asString() << ": " << device["state"].asString();
                                }
                            }
                            else if (aspect == "audio_midi")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[audio_midi] " << value["description"].asString();
                                else
                                {
                                    out << "\n[audio_midi] bank " << value["bank"]["status"].asString() << ", voices "
                                        << value["active_voices"].asUInt() << "/" << value["polyphony_limit"].asUInt()
                                        << ", bytes " << value["counters"]["bytes_received"].asUInt64() << ", framing errors "
                                        << value["counters"]["framing_errors"].asUInt64();
                                    for (const Json::Value& part : value["parts"])
                                    {
                                        if (part["active_voices"].asUInt() == 0)
                                            continue;
                                        out << "\n  ch" << part["channel"].asUInt() << " prog " << part["program"].asUInt() << " "
                                            << part["preset"].asString() << ":";
                                        for (const Json::Value& note : part["notes"])
                                            out << " " << note.asString();
                                    }
                                }
                            }
                            else if (aspect == "slots")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[slots] " << value["description"].asString();
                                else
                                {
                                    out << "\n[slots] " << value["model"].asString() << " (" << value["board"].asString()
                                        << "), cards from " << value["source"].asString();
                                    for (const Json::Value& slot : value["slots"])
                                    {
                                        out << "\n  " << slot["slot"].asString() << " = " << slot["card"].asString();
                                        if (!slot["options"].asString().empty())
                                            out << " [" << slot["options"].asString() << "]";
                                        out << " fit " << slot["fit"].asString() << ", " << slot["state"].asString();
                                    }
                                    for (const Json::Value& builtIn : value["builtIns"])
                                        out << "\n  built-in " << builtIn["id"].asString() << ": " << builtIn["state"].asString();
                                }
                            }
                            else if (aspect == "audio_covox")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[audio_covox] " << value["description"].asString();
                                else
                                {
                                    out << "\n[audio_covox] " << value["device"].asString() << ", latches";
                                    const Json::Value& dac = value["channels"];
                                    for (Json::ArrayIndex i = 0; i < dac.size(); ++i)
                                        out << " " << dac[i]["name"].asString() << "=" << dac[i]["latch"].asUInt();
                                    if (value["shared_with_beta128"].size() > 0)
                                        out << "\n  shared with Beta-128: " << value["shared_port_rule"].asString();
                                }
                            }
                            else if (aspect == "mouse")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[mouse] " << value["description"].asString();
                                else if (value.isMember("device") && value["device"].isObject())
                                {
                                    // The machine's own mouse (Kempston, Sprinter serial, ZX-Evo PS/2)
                                    const Json::Value& device = value["device"];
                                    out << "\n[mouse] " << device["id"].asString() << " (" << device["kind"].asString()
                                        << ")" << (device["in_use"].asBool() ? ", in use" : "") << ", x "
                                        << device["x"].asInt() << " y " << device["y"].asInt()
                                        << (device["wheel"].asBool() ? ", wheel" : "");
                                    if (device.isMember("serial"))
                                        out << ", serial receiver " << device["serial"]["receiver_baud"].asDouble() << " baud"
                                            << (device["serial"]["receiver_in_tune"].asBool() ? " (in tune)" : " (out of tune)")
                                            << ", " << device["serial"]["packets_sent"].asUInt64() << " packets";
                                    if (device["id"].asString() == "kempston" && value.isMember("routing"))
                                        out << ", ports " << (value["routing"]["ports_decoded"].asBool() ? "decoded" : "shadowed")
                                            << " (" << value["routing"]["note"].asString() << ")";
                                }
                                else if (value.isMember("mouse_fitted") && !value["mouse_fitted"].asBool())
                                    out << "\n[mouse] no mouse fitted (input refused)";
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
                            else if (aspect == "video_layout")
                            {
                                out << "\n[video_layout] " << value["video_mode"].asString() << ", family "
                                    << value["family"].asString();
                                if (!value["mapped"].asBool())
                                    out << " (not mapped)";
                                const Json::Value& layers = value["layers"];
                                for (Json::ArrayIndex i = 0; i < layers.size(); ++i)
                                {
                                    const Json::Value& w = layers[i]["window"];
                                    out << "\n  " << layers[i]["id"].asString() << " " << layers[i]["surface"]["width"].asInt()
                                        << "x" << layers[i]["surface"]["height"].asInt() << ", lines " << w["first_line"].asInt()
                                        << "+" << w["line_count"].asInt() << ", T " << w["first_t"].asInt() << "+"
                                        << w["t_count"].asInt() << " at " << w["dots_per_t"].asInt() << " dots/T";
                                }
                            }
                            else if (aspect == "video_text")
                            {
                                if (value.isMember("available") && !value["available"].asBool())
                                    out << "\n[video_text] " << value["description"].asString();
                                else
                                {
                                    out << "\n[video_text] " << value["layer"].asString() << " " << value["columns"].asInt()
                                        << "x" << value["rows"].asInt();
                                    const Json::Value& lines = value["lines"];
                                    for (Json::ArrayIndex i = 0; i < lines.size(); ++i)
                                    {
                                        std::string text = lines[i]["text"].asString();
                                        const size_t end = text.find_last_not_of(" .");
                                        if (end != std::string::npos)
                                            out << "\n  " << text.substr(0, end + 1);
                                    }
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
                            else if (aspect == "ttd")
                            {
                                out << "\n[ttd] " << FormatTtdStatus(value);
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
    for (const char* action : {"type", "tap", "press", "release", "combo", "macro", "release_all", "status", "list_keys", "route"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Input operation. 'type' sends text (auto-shift); 'tap' presses a key for N frames; 'combo' presses several keys at once; "
        "'route' sets where keys go (route = auto | matrix | ps2 | both: the ZX matrix, the PS/2 keyboard controller of a ZX-Evo / "
        "ATM Turbo 2+, both); 'status' shows it.";
    schema["properties"]["route"]["type"] = "string";
    schema["properties"]["route"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* route : {"auto", "matrix", "ps2", "both"})
        schema["properties"]["route"]["enum"].append(route);
    schema["properties"]["route"]["description"] = "For 'route': where host and injected keys go";
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
    schema["properties"]["tokenized"]["description"] =
        "Type a BASIC line into the ROM editor (48K: keywords as keys; 128K: letters), every key verified on the "
        "ROM's control points; ENTER is not pressed. The reply names what happened (outcome/failure)";
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
            if (action == "route")
            {
                if (!args.isMember("route"))
                {
                    done(ToolResult::Error("route requires 'route' (auto | matrix | ps2 | both)"));
                    return;
                }
                Json::Value body;
                body["route"] = args["route"].asString();
                ResolveAndForward(args, "POST", "/keyboard/route", &body, caller, "Keyboard route set", done);
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
    for (const char* action : {"move", "glide", "press", "release", "click", "buttons", "wheel", "release_all", "status"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "The machine's own mouse (relative device): Kempston interface, the Sprinter's serial mouse, the ZX-Evo / "
        "TS-Conf PS/2 mouse; a machine without a mouse refuses with 409. 'move' shifts by dx/dy emulated pixels "
        "(-127..127); 'glide' moves up to -4096..4096 in steps the program follows (one per frame; input sent "
        "meanwhile queues behind it, so a click after a glide lands where it ended); 'click' presses a button for N "
        "frames. Input is applied immediately; call control_execution run_frames to let the program react. 'status' "
        "names the device and what it gives the guest (ports, serial line, PS/2).";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["dx"]["type"] = "integer";
    schema["properties"]["dx"]["minimum"] = -4096;
    schema["properties"]["dx"]["maximum"] = 4096;
    schema["properties"]["dx"]["description"] = "+ = right (move: -127..127; glide: -4096..4096; optional pre-move for click)";
    schema["properties"]["dy"]["type"] = "integer";
    schema["properties"]["dy"]["minimum"] = -4096;
    schema["properties"]["dy"]["maximum"] = 4096;
    schema["properties"]["dy"]["description"] = "+ = UP (move: -127..127; glide: -4096..4096; optional pre-move for click)";
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "status: report this device of the machine (kempston, sprinter, evo-ps2); default the first fitted one";
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
        "Send mouse input to the machine's own mouse (Kempston, Sprinter serial, ZX-Evo PS/2): relative move (dx/dy), "
        "glide (long moves the program can follow), press/release/click buttons, set the exact pressed set, wheel "
        "steps, release_all, status. Values are forwarded as-is; the WebAPI validates ranges.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            if (action == "status")
            {
                const std::string device = args.isMember("device") ? args["device"].asString() : std::string();
                ResolveAndForward(args, "GET", device.empty() ? "/mouse/status" : "/mouse/status?device=" + device,
                                  nullptr, caller, "Mouse status", done);
                return;
            }
            if (action == "glide")
            {
                if (!args.isMember("dx") && !args.isMember("dy"))
                {
                    done(ToolResult::Error("glide requires 'dx' or 'dy'"));
                    return;
                }
                Json::Value body;
                body["dx"] = args.isMember("dx") ? args["dx"] : Json::Value(0);
                body["dy"] = args.isMember("dy") ? args["dy"] : Json::Value(0);
                ResolveAndForward(args, "POST", "/mouse/glide", &body, caller, "Mouse glide started", done);
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

                // Pre-move then click, in order; the click is skipped if the move fails. A pre-move beyond
                // -127..127 glides: the click queues behind the glide and lands where it ended
                Json::Value moveBody;
                moveBody["dx"] = args.isMember("dx") ? args["dx"] : Json::Value(0);
                moveBody["dy"] = args.isMember("dy") ? args["dy"] : Json::Value(0);
                const auto beyondMove = [](const Json::Value& v) { return v.isInt() && (v.asInt() > 127 || v.asInt() < -127); };
                const std::string moveSuffix = beyondMove(moveBody["dx"]) || beyondMove(moveBody["dy"]) ? "/mouse/glide" : "/mouse/move";

                TargetResolver::ResolveFromArgs(
                    args, caller, [&caller, moveBody, moveSuffix, clickBody, okText, done](bool ok, const std::string& idOrError) {
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
                        steps.push_back(makeStep("move", moveSuffix, moveBody));
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

/// region <joystick_input>

namespace
{

void RegisterJoystickInput(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"press", "release", "set", "tap", "status"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Kempston joystick (reads at IN #1F on ATM3 / Scorpion / TS-Conf). 'press' holds buttons, 'release' lets them go, "
        "'set' makes exactly the given buttons (or state byte) held, 'tap' presses for N frames then releases. Input is "
        "applied immediately; call control_execution run_frames to let the program react.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    Json::Value nameEnum(Json::arrayValue);
    for (const char* button : {"up", "down", "left", "right", "fire", "b5", "b6", "b7"})
    {
        nameEnum.append(button);
    }
    schema["properties"]["buttons"]["oneOf"][0]["type"] = "string";
    schema["properties"]["buttons"]["oneOf"][1]["type"] = "array";
    schema["properties"]["buttons"]["oneOf"][1]["items"]["type"] = "string";
    schema["properties"]["buttons"]["oneOf"][1]["items"]["enum"] = nameEnum;
    schema["properties"]["buttons"]["description"] =
        "Button names: one string ('up+fire', 'up,fire') or an array. Required for press / release / tap; for 'set' it is the "
        "exact held set ([] = none)";
    schema["properties"]["state"]["type"] = "integer";
    schema["properties"]["state"]["minimum"] = 0;
    schema["properties"]["state"]["maximum"] = 255;
    schema["properties"]["state"]["description"] =
        "Raw device byte for 'set' (active high: right 1, left 2, down 4, up 8, fire 0x10, D5..D7 free)";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["minimum"] = 1;
    schema["properties"]["frames"]["default"] = 2;
    schema["properties"]["frames"]["description"] = "Hold time for 'tap'";
    schema["required"].append("action");

    registry.Register(
        "joystick_input",
        "Send Kempston joystick input to the emulator: press / release buttons, set the exact held set or state byte, tap "
        "for N frames, status (device byte, fitted / wired, host keys). Values are forwarded as-is; the WebAPI validates "
        "names and ranges and warns when the machine does not decode the joystick.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            if (action == "status")
            {
                ResolveAndForward(args, "GET", "/joystick", nullptr, caller, "Joystick status", done);
                return;
            }
            if (action == "press" || action == "release" || action == "tap")
            {
                if (!args.isMember("buttons"))
                {
                    done(ToolResult::Error(action + " requires 'buttons'"));
                    return;
                }
                Json::Value body;
                body["buttons"] = args["buttons"];
                if (action == "tap" && args.isMember("frames"))
                {
                    body["frames"] = args["frames"];
                }
                ResolveAndForward(args, "POST", "/joystick/" + action, &body, caller, "Joystick " + action, done);
                return;
            }
            if (action == "set")
            {
                if (!args.isMember("state") && !args.isMember("buttons"))
                {
                    done(ToolResult::Error("set requires 'state' or 'buttons'"));
                    return;
                }
                Json::Value body;
                if (args.isMember("state"))
                {
                    body["state"] = args["state"];
                }
                else
                {
                    body["buttons"] = args["buttons"];
                }
                ResolveAndForward(args, "POST", "/joystick/set", &body, caller, "Joystick state set", done);
                return;
            }

            done(ToolResult::Error("Unknown action '" + action + "'"));
        });
}

} // namespace

/// endregion </joystick_input>

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

/// Formats a TTD time point ({frame, tinframe}) as "frame F" or "frame F t=T".
std::string FormatTimePoint(const Json::Value& frame, const Json::Value& tinframe)
{
    std::string text = "frame " + std::to_string(frame.asUInt64());
    if (tinframe.asUInt() != 0)
    {
        text += " t=" + std::to_string(tinframe.asUInt());
    }
    return text;
}

/// Formats a replay-barrier marker ({frame, tinframe, kind, reason}).
std::string FormatMarker(const Json::Value& marker)
{
    return marker["kind"].asString() + " '" + marker["reason"].asString() + "' at " +
           FormatTimePoint(marker["frame"], marker["tinframe"]);
}

/// TD-8: " Searched frame A .. frame B." from covered_from/covered_to, or "" when absent.
std::string FormatSearchWindow(const Json::Value& b)
{
    if (!b.isMember("covered_from"))
    {
        return {};
    }
    return " Searched " + FormatTimePoint(b["covered_from"], b["covered_from_tinframe"]) + " .. " +
           FormatTimePoint(b["covered_to"], b["covered_to_tinframe"]) + ".";
}

/// Like ForwardCall, but the text summary is built from the 2xx body. A 2xx
/// body with "ok": false (dump) is a failure. A 409 (a history operation
/// while recording) gets the MCP-side remedy appended.
void CallAndSummarize(const std::string& method, const std::string& path, const Json::Value* body, IApiCaller& caller,
                      std::function<std::string(const Json::Value&)> summarize, ToolCallback done)
{
    caller.Call(method, path, body, [summarize, done](int status, Json::Value responseBody) {
        if (status >= 200 && status < 300 && responseBody.isObject() && responseBody.isMember("ok") &&
            !responseBody["ok"].asBool())
        {
            done(ToolResult::Error("Failed: " + responseBody["error"].asString()));
            return;
        }
        if (status >= 200 && status < 300)
        {
            const std::string text = summarize(responseBody);
            done(ToolResult::Ok(text, std::move(responseBody)));
            return;
        }
        if (status == 0)
        {
            done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
            return;
        }
        std::string text = "WebAPI returned HTTP " + std::to_string(status);
        const std::string details = DescribeErrorBody(responseBody);
        if (!details.empty())
        {
            text += ": " + details;
        }
        if (status == 409 && responseBody["state"].asString() == "recording")
        {
            text += " (MCP: call time_travel action 'stop' first)";
        }
        done(ToolResult::Error(text));
    });
}

/// Copies an optional numeric argument into a request body, accepting an
/// integer or a "0x.."/"$.."/decimal string. Returns false (and sets error)
/// when the value is present but not a number.
bool CopyNumber(const Json::Value& args, const char* field, Json::Value& body, std::string& error)
{
    if (!args.isMember(field))
    {
        return true;
    }
    Json::Value number = NumericOrHex(args[field]);
    if (number.isNull() || !(number.isUInt() || number.isUInt64()))
    {
        error = std::string("'") + field + "' must be a non-negative number (integer, or a \"0x..\" / \"$..\" / decimal string)";
        return false;
    }
    body[field] = number;
    return true;
}

void RegisterTimeTravel(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"status", "start", "stop", "invalidate", "position", "markers", "seek", "step_back_frame",
                               "step_forward_frame", "step_back_instruction", "step_forward_instruction", "reverse_step",
                               "reverse_continue", "find_last", "port_events", "resume", "dump", "load", "file_info",
                               "bookmark_add", "bookmark_list",
                               "bookmark_delete", "seek_bookmark", "coverage_probe", "coverage_scan", "coverage_summary",
                               "history_limit", "journal_on", "journal_off", "journal_build", "export_clip"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Session: 'status' (state, recorded frame range, checkpoints, memory), 'start' (begin recording; journal=true "
        "also records the write journal; black_box=true records the last 'minutes' and leaves turbo free), "
        "'stop' (end recording, history kept and browsable), 'invalidate' (drop all history), 'position' (current point + "
        "session end), 'markers' (replay barriers: tape control, disk writes, tool memory edits made while recording; "
        "the hardware_reset kind is reserved and never written - a reset stops the recording instead). "
        "Navigate (while recording they pause the recording: state detached, status recording_paused; 'resume' at the "
        "paused point continues it, 'stop' ends it; on backend v1 they return an error while recording): "
        "'seek' (frame + optional tinframe), "
        "'step_back_frame'/'step_forward_frame', 'step_back_instruction'/'step_forward_instruction', "
        "'reverse_step' (count instructions OR tstates back), 'reverse_continue' (run backward until PC hits one of pcs), "
        "'find_last' (latest write/read/execute/io at an address before the current point, or before before_frame). "
        "'port_events' answers 'when did the program ...' from the port journals without replay (needs a stopped or "
        "paused recording, works on a loaded file): event 'key' (saw a key down; event_arg a key name such as 'a', "
        "'enter', 'space'), 'ear' (saw the tape signal change), 'ay-read' / 'ay-write' / 'ay-select' (event_arg an AY "
        "register), 'border', 'beeper', 'in' / 'out' (narrow with port / port_mask / value / value_mask). "
        "'resume' continues recording from the current (or given) point and DISCARDS the history after it; it needs the "
        "machine positioned in history (seek or step first - it fails right after 'stop'). "
        "Files: 'dump' / 'load' a .ttd session (path on the emulator's machine; load needs the same machine model, ROM "
        "set and General Sound / TurboSound card), 'file_info' describes a .ttd file without loading it and without an "
        "emulator: frame range, sections and the recorded machine (model, ROM signature, devices, general_sound card to "
        "fit before 'load'); 'export_clip' writes frames from_frame..to_frame as a lossless clip into the directory path "
        "(final picture, plane B when zxdlss is on, frame meta; chunk = frames per zstd chunk, default 500) in one "
        "call instead of a seek and a capture per frame (not while recording). "
        "Bookmarks: 'bookmark_add'/'bookmark_list'/'bookmark_delete'/'seek_bookmark' (advisory labels, never barriers). "
        "History limit: 'history_limit' sets (history_frames / history_bytes, 0 = none, a missing one is kept) or "
        "reports the bound on the recorded history - while recording, the oldest frames are released beyond it and "
        "the session start moves forward; a file saved afterwards replays its remaining frames exactly. 'start' "
        "takes the same two fields. "
        "Coverage index: 'coverage_probe' (did frame X touch an address range), 'coverage_scan' (which frames did), "
        "'coverage_summary' (bucketed activity heatmap). "
        "Write journal (who wrote an address last, answered at once; off by default): 'journal_on' / 'journal_off' "
        "switch it at any moment, also while recording (each on-off span is a segment); 'journal_build' builds it by "
        "replaying frames from_frame..to_frame (default: the whole session; about 2-4 ms per frame; not while "
        "recording). find_last works without it too: outside the journal it replays one frame.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto' to reuse the single instance";
    schema["properties"]["journal"]["type"] = "boolean";
    schema["properties"]["journal"]["description"] =
        "start: also record the write journal (default false). find_last for writes then answers at once; without "
        "it, it replays one frame";
    schema["properties"]["black_box"]["type"] = "boolean";
    schema["properties"]["black_box"]["description"] =
        "start: record as the black box (the engine): keep the last 'minutes'; turbo and a host speed above 1x stay "
        "free (the recording stops before them, history kept). Default false: an explicit recording, which holds 1x";
    schema["properties"]["minutes"]["type"] = "integer";
    schema["properties"]["minutes"]["minimum"] = 1;
    schema["properties"]["minutes"]["maximum"] = 1440;
    schema["properties"]["minutes"]["description"] = "start with black_box: the minutes the black box keeps (default 5)";
    schema["properties"]["history_frames"]["type"] = "integer";
    schema["properties"]["history_frames"]["minimum"] = 0;
    schema["properties"]["history_frames"]["description"] =
        "start / history_limit: keep at most this many frames of history (one checkpoint each); 0 = no limit";
    schema["properties"]["history_bytes"]["type"] = "integer";
    schema["properties"]["history_bytes"]["minimum"] = 0;
    schema["properties"]["history_bytes"]["description"] =
        "start / history_limit: keep the history's checkpoint data under this many bytes; 0 = no limit";
    schema["properties"]["reason"]["type"] = "string";
    schema["properties"]["reason"]["description"] = "invalidate: free-text reason echoed back and logged";
    schema["properties"]["label"]["type"] = "string";
    schema["properties"]["label"]["description"] =
        "Bookmark label for bookmark_add / bookmark_delete / seek_bookmark (non-empty, at most 63 characters)";
    schema["properties"]["frame"]["type"] = "integer";
    schema["properties"]["frame"]["description"] =
        "Frame number: target for seek (required), optional start point for resume (default: the current point), optional position for bookmark_add "
        "(default: current position), frame to test for coverage_probe";
    schema["properties"]["tinframe"]["type"] = "integer";
    schema["properties"]["tinframe"]["description"] =
        "T-states within 'frame' for seek / resume / bookmark_add (default 0). A seek without it lands at the frame's end "
        "on the engine (the frame's final state and picture); give 0 for the frame's start";
    schema["properties"]["count"]["type"] = "integer";
    schema["properties"]["count"]["description"] = "reverse_step: number of instructions to step back (give count OR tstates)";
    schema["properties"]["tstates"]["type"] = "integer";
    schema["properties"]["tstates"]["description"] =
        "reverse_step: T-states to step back; lands on the nearest instruction start at or before the target";
    schema["properties"]["pcs"]["type"] = "array";
    schema["properties"]["pcs"]["items"]["type"] = "string";
    schema["properties"]["pcs"]["description"] =
        "reverse_continue: reverse breakpoints - PC addresses as integers or '0x8000' strings (non-empty)";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "dump / load / file_info: .ttd file path; export_clip: the clip's directory (created if missing). Resolved by "
        "the emulator process (its machine and working directory)";
    schema["properties"]["addr"]["type"] = "string";
    schema["properties"]["addr"]["description"] = "find_last: single Z80 address (integer, '0x5800', '#5800' or '$5800')";
    schema["properties"]["access"]["type"] = "string";
    schema["properties"]["access"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* access : {"write", "read", "execute", "io"})
    {
        schema["properties"]["access"]["enum"].append(access);
    }
    schema["properties"]["access"]["description"] = "find_last: access kind to search for (default 'write')";
    schema["properties"]["value"]["type"] = "string";
    schema["properties"]["value"]["description"] =
        "find_last: only accesses that moved this byte value (0..255); port_events: (value & value_mask) == value";
    schema["properties"]["event"]["type"] = "string";
    schema["properties"]["event"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* e : {"key", "ear", "ay-read", "ay-write", "ay-select", "border", "beeper", "in", "out"})
        schema["properties"]["event"]["enum"].append(e);
    schema["properties"]["event"]["description"] = "port_events: what to find (see 'action')";
    schema["properties"]["event_arg"]["type"] = "string";
    schema["properties"]["event_arg"]["description"] =
        "port_events: key name for 'key' (a named key counts only in reads of its half-row alone; none = any key), "
        "AY register 0..15 for the ay events";
    schema["properties"]["newest"]["type"] = "boolean";
    schema["properties"]["newest"]["description"] = "port_events: the last hits, newest first, instead of the first";
    schema["properties"]["port"]["type"] = "string";
    schema["properties"]["port"]["description"] = "port_events: (port & port_mask) == port; alone: an exact port";
    schema["properties"]["port_mask"]["type"] = "string";
    schema["properties"]["value_mask"]["type"] = "string";
    schema["properties"]["match"]["type"] = "string";
    schema["properties"]["match"]["description"] = "port_events value test: any | equals | any-clear | any-set";
    schema["properties"]["trigger"]["type"] = "string";
    schema["properties"]["trigger"]["description"] =
        "port_events: every | rising (the test starts passing) | change (the masked value changes), per port";
    schema["properties"]["ay_register"]["type"] = "integer";
    schema["properties"]["ay_register"]["description"] = "port_events: only while this AY register is selected";
    schema["properties"]["stream_mask"]["type"] = "string";
    schema["properties"]["stream_mask"]["description"] =
        "port_events: address bits that separate streams for rising/change (0xFFFF every port, 0x0001 the ULA)";
    schema["properties"]["file"]["type"] = "string";
    schema["properties"]["file"]["description"] =
        "port_events: a .ttd file on the emulator's machine to search instead of the current session - it is not "
        "loaded, the session is untouched (works while recording)";
    schema["properties"]["pc_from"]["type"] = "string";
    schema["properties"]["pc_from"]["description"] = "find_last: only accesses made by code with PC >= pc_from";
    schema["properties"]["pc_to"]["type"] = "string";
    schema["properties"]["pc_to"]["description"] = "find_last: only accesses made by code with PC <= pc_to";
    schema["properties"]["before_frame"]["type"] = "integer";
    schema["properties"]["before_frame"]["description"] =
        "find_last: search backward from this frame instead of the current position";
    schema["properties"]["before_tin"]["type"] = "integer";
    schema["properties"]["before_tin"]["description"] = "find_last: T-states within before_frame (default 0)";
    schema["properties"]["from_frame"]["type"] = "integer";
    schema["properties"]["from_frame"]["description"] =
        "Starting frame for coverage_scan / coverage_summary / port_events / journal_build / export_clip";
    schema["properties"]["to_frame"]["type"] = "integer";
    schema["properties"]["to_frame"]["description"] =
        "Ending frame for coverage_scan / coverage_summary / port_events / journal_build / export_clip";
    schema["properties"]["chunk"]["type"] = "integer";
    schema["properties"]["chunk"]["description"] = "export_clip: frames per zstd chunk (default 500)";
    schema["properties"]["kind"]["type"] = "string";
    schema["properties"]["kind"]["description"] = "Coverage kind: 'executed', 'written', or 'read'";
    schema["properties"]["addr_from"]["type"] = "string";
    schema["properties"]["addr_from"]["description"] =
        "Start Z80 address of a range for find_last or a coverage query (e.g. '0xBF00' or 48896)";
    schema["properties"]["addr_to"]["type"] = "string";
    schema["properties"]["addr_to"]["description"] =
        "End Z80 address (inclusive) of a range for find_last or a coverage query (e.g. '0xBFFF' or 49151)";
    schema["properties"]["phys_page"]["type"] = "integer";
    schema["properties"]["phys_page"]["description"] =
        "Optional physical RAM page (0..255) for find_last or a coverage query - picks the bank on paged machines";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["description"] =
        "Max results for coverage_scan (default 200) or port_events (default 100), or max buckets for coverage_summary "
        "(default 100)";
    schema["properties"]["bucket_size"]["type"] = "integer";
    schema["properties"]["bucket_size"]["description"] = "Frames per bucket for coverage_summary (default: auto)";
    schema["required"].append("action");

    registry.Register(
        "time_travel",
        "Time-travel debugging (TTD): record execution, then move backward and forward through it. Typical flow: "
        "'start' -> run the program (control_execution) -> 'stop' -> 'find_last' / 'reverse_continue' / 'seek' / step "
        "actions to inspect the past (inspect_state shows the machine at that point) -> 'resume' to continue live from "
        "there. While recording, the host speed is held at 1x and turbo / fast tape / fast disk are off; a tape or disk "
        "load and a ROM reload are refused while recording and wipe a stopped session's history; a snapshot load is part "
        "of the recording (the engine; v1 refuses it); seek / step / find_last while recording pause it (the engine); "
        "reset stops the recording and keeps it. Also: agent bookmarks and coverage index queries.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();

            // A file, not a session: no emulator instance to resolve
            if (action == "file_info")
            {
                const std::string path = args["path"].asString();
                if (path.empty())
                {
                    done(ToolResult::Error("Action 'file_info' requires 'path' (a .ttd file on the emulator's machine)"));
                    return;
                }
                CallAndSummarize("GET", "/api/v1/ttd/file-info?path=" + UrlEncodeSegment(path), nullptr, caller,
                                 [](const Json::Value& b) { return FormatTtdFileInfo(b); }, done);
                return;
            }

            if (action == "bookmark_add" || action == "bookmark_delete" || action == "seek_bookmark")
            {
                const std::string label = args["label"].asString();
                if (label.empty())
                {
                    done(ToolResult::Error("Action '" + action + "' requires a non-empty 'label'"));
                    return;
                }
            }

            // Request bodies for the session / navigation actions are built and
            // validated here, before target resolution, so a malformed call
            // costs no HTTP round-trip
            auto body = std::make_shared<Json::Value>(Json::objectValue);
            std::string error;
            if (action == "start")
            {
                if (args.isMember("journal"))
                {
                    if (!args["journal"].isBool())
                    {
                        done(ToolResult::Error("'journal' must be true or false"));
                        return;
                    }
                    (*body)["journal"] = args["journal"].asBool();
                }
                if (args.isMember("black_box"))
                {
                    if (!args["black_box"].isBool())
                    {
                        done(ToolResult::Error("'black_box' must be true or false"));
                        return;
                    }
                    (*body)["black_box"] = args["black_box"].asBool();
                }
                if (args.isMember("minutes"))
                    (*body)["minutes"] = args["minutes"];
                if (args.isMember("history_frames"))
                    (*body)["history_limit_frames"] = args["history_frames"];
                if (args.isMember("history_bytes"))
                    (*body)["history_limit_bytes"] = args["history_bytes"];
            }
            else if (action == "journal_on" || action == "journal_off")
            {
                (*body)["enabled"] = action == "journal_on";
            }
            else if (action == "journal_build")
            {
                if (args.isMember("from_frame"))
                    (*body)["from_frame"] = args["from_frame"];
                if (args.isMember("to_frame"))
                    (*body)["to_frame"] = args["to_frame"];
            }
            else if (action == "export_clip")
            {
                if (!args.isMember("from_frame") || !args.isMember("to_frame") || args["path"].asString().empty())
                {
                    done(ToolResult::Error("Action 'export_clip' requires 'from_frame', 'to_frame' and 'path' (a directory on "
                                           "the emulator's machine)"));
                    return;
                }
                (*body)["from"] = args["from_frame"];
                (*body)["to"] = args["to_frame"];
                (*body)["path"] = args["path"].asString();
                if (args.isMember("chunk"))
                    (*body)["chunk"] = args["chunk"];
            }
            else if (action == "history_limit")
            {
                if (args.isMember("history_frames"))
                    (*body)["frames"] = args["history_frames"];
                if (args.isMember("history_bytes"))
                    (*body)["bytes"] = args["history_bytes"];
            }
            else if (action == "invalidate")
            {
                if (args.isMember("reason"))
                {
                    (*body)["reason"] = args["reason"].asString();
                }
            }
            else if (action == "seek")
            {
                if (!args.isMember("frame"))
                {
                    done(ToolResult::Error("Action 'seek' requires 'frame' (use 'seek_bookmark' to seek by label)"));
                    return;
                }
                if (!CopyNumber(args, "frame", *body, error) || !CopyNumber(args, "tinframe", *body, error))
                {
                    done(ToolResult::Error(error));
                    return;
                }
            }
            else if (action == "resume")
            {
                if (!CopyNumber(args, "frame", *body, error) || !CopyNumber(args, "tinframe", *body, error))
                {
                    done(ToolResult::Error(error));
                    return;
                }
            }
            else if (action == "reverse_step")
            {
                if (args.isMember("count") == args.isMember("tstates"))
                {
                    done(ToolResult::Error("Action 'reverse_step' needs exactly one of 'count' (instructions) or 'tstates'"));
                    return;
                }
                if (!CopyNumber(args, "count", *body, error) || !CopyNumber(args, "tstates", *body, error))
                {
                    done(ToolResult::Error(error));
                    return;
                }
            }
            else if (action == "reverse_continue")
            {
                if (!args["pcs"].isArray() || args["pcs"].empty())
                {
                    done(ToolResult::Error("Action 'reverse_continue' requires 'pcs': a non-empty array of PC addresses"));
                    return;
                }
                Json::Value pcs(Json::arrayValue);
                for (const Json::Value& pc : args["pcs"])
                {
                    Json::Value number = NumericOrHex(pc);
                    if (number.isNull() || !number.isUInt() || number.asUInt() > 0xFFFF)
                    {
                        done(ToolResult::Error("'pcs' entries must be addresses 0..65535 (integer or '0x....' string)"));
                        return;
                    }
                    pcs.append(number);
                }
                (*body)["pcs"] = pcs;
            }
            else if (action == "find_last")
            {
                if (!args.isMember("addr") && !args.isMember("addr_from") && !args.isMember("addr_to") && !args.isMember("pc_from") &&
                    !args.isMember("pc_to") && !args.isMember("value"))
                {
                    done(ToolResult::Error("Action 'find_last' needs a search criterion: 'addr', 'addr_from'/'addr_to', "
                                           "'pc_from'/'pc_to' or 'value'"));
                    return;
                }
                // Address / value / PC fields go through verbatim: the WebAPI
                // parses numbers and "0x.."/"#.."/"$.." strings with range checks
                for (const char* field : {"addr", "addr_from", "addr_to", "value", "pc_from", "pc_to", "access"})
                {
                    if (args.isMember(field))
                    {
                        (*body)[field] = args[field];
                    }
                }
                if (!CopyNumber(args, "phys_page", *body, error) || !CopyNumber(args, "before_frame", *body, error) ||
                    !CopyNumber(args, "before_tin", *body, error))
                {
                    done(ToolResult::Error(error));
                    return;
                }
            }
            else if (action == "port_events")
            {
                if (!args["event"].isString())
                {
                    done(ToolResult::Error("Action 'port_events' requires 'event': key, ear, ay-read, ay-write, "
                                           "ay-select, border, beeper, in or out"));
                    return;
                }
                (*body)["event"] = args["event"];
                if (args.isMember("event_arg"))
                    (*body)["arg"] = args["event_arg"];
                if (args.isMember("from_frame"))
                    (*body)["from"] = args["from_frame"];
                if (args.isMember("to_frame"))
                    (*body)["to"] = args["to_frame"];
                // The rest go through verbatim: the WebAPI checks them
                for (const char* field : {"limit", "newest", "port", "port_mask", "value", "value_mask", "match",
                                          "trigger", "stream_mask", "ay_register", "file"})
                {
                    if (args.isMember(field))
                        (*body)[field] = args[field];
                }
            }
            else if (action == "dump" || action == "load")
            {
                if (args["path"].asString().empty())
                {
                    done(ToolResult::Error("Action '" + action + "' requires 'path' (a .ttd file on the emulator's machine)"));
                    return;
                }
                (*body)["path"] = args["path"].asString();
            }

            auto forward = [&caller, &args, action, body, done](const std::string& id) {
                if (action == "status")
                {
                    CallAndSummarize("GET", Endpoint(id, "/ttd/status"), nullptr, caller,
                                     [id](const Json::Value& b) { return "TTD on " + id + ": " + FormatTtdStatus(b); }, done);
                }
                else if (action == "start")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/start"), body.get(), caller, [id](const Json::Value& b) {
                        std::string text = b["already_active"].asBool() ? "TTD was already recording on " + id
                                                                        : "TTD recording started on " + id;
                        text += " (write journal ";
                        text += b["write_journal_enabled"].asBool() ? "on" : "off";
                        if (b["black_box"].asBool())
                            text += ", the black box: the last minutes are kept; turbo and host speed stay free - the "
                                    "recording stops before them, history kept). Run the program now "
                                    "(control_execution), then 'stop' to browse the history.";
                        else
                            text += "). Host speed is held at 1x and turbo / fast tape / fast disk are off until 'stop'. "
                                    "Run the program now (control_execution), then 'stop' to browse the history.";
                        return text;
                    }, done);
                }
                else if (action == "journal_on" || action == "journal_off")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/journal"), body.get(), caller, [id](const Json::Value& b) {
                        return "TTD write journal on " + id + ": " +
                               (b["write_journal_enabled"].asBool() ? "on" : "off") + ", " +
                               DescribeJournalSegments(b);
                    }, done);
                }
                else if (action == "journal_build")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/journal/build"), body.get(), caller, [id](const Json::Value& b) {
                        if (!b["ok"].asBool())
                            return "TTD write journal not built on " + id + ": " + b["error"].asString();
                        std::ostringstream text;
                        text << "TTD write journal built on " << id << " for " << b["frames_built"].asUInt64()
                             << " frame(s), " << b["records"].asUInt64() << " writes";
                        if (b["frames_covered"].asUInt64())
                            text << "; " << b["frames_covered"].asUInt64() << " already covered";
                        if (b["frames_refused"].asUInt64())
                            text << "; " << b["frames_refused"].asUInt64() << " not replayable (a marker without its data)";
                        if (b["cancelled"].asBool())
                            text << "; cancelled";
                        text << ". It " << DescribeJournalSegments(b) << ".";
                        return text.str();
                    }, done);
                }
                else if (action == "export_clip")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/export-clip"), body.get(), caller, [](const Json::Value& b) {
                        std::ostringstream text;
                        text << "Clip written to " << b["path"].asString() << ": " << b["frames"].asUInt64() << " frame(s), "
                             << b["width"].asUInt() << "x" << b["height"].asUInt() << ", " << b["bytes"].asUInt64()
                             << " bytes" << (b["planeb"].asBool() ? ", plane B included" : "") << " in "
                             << b["seconds"].asDouble() << " s";
                        return text.str();
                    }, done);
                }
                else if (action == "history_limit")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/history-limit"), body.get(), caller, [id](const Json::Value& b) {
                        std::ostringstream text;
                        text << "TTD history limit on " << id << ": ";
                        const uint64_t frames = b["history_limit_frames"].asUInt64();
                        const uint64_t bytes = b["history_limit_bytes"].asUInt64();
                        if (frames == 0 && bytes == 0)
                            text << "none";
                        if (frames != 0)
                            text << frames << " frames ";
                        if (bytes != 0)
                            text << bytes << " bytes ";
                        text << "- history frames " << b["session_start_frame"].asUInt64() << ".."
                             << b["current_end_frame"].asUInt64() << ", " << b["history_bytes"].asUInt64() << " bytes held, "
                             << b["evicted_checkpoints"].asUInt64() << " oldest checkpoint(s) released (state "
                             << b["state"].asString() << ")";
                        return text.str();
                    }, done);
                }
                else if (action == "stop")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/stop"), nullptr, caller, [id](const Json::Value& b) {
                        return b["stopped"].asBool() ? "TTD recording stopped on " + id + "; history kept (state " +
                                                           b["state"].asString() + ") - seek/step/find_last are available now"
                                                     : "TTD was not recording on " + id + " (state " + b["state"].asString() + ")";
                    }, done);
                }
                else if (action == "invalidate")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/invalidate"), body.get(), caller, [id](const Json::Value& b) {
                        return "TTD history dropped on " + id + " (reason '" + b["reason"].asString() + "')";
                    }, done);
                }
                else if (action == "position")
                {
                    CallAndSummarize("GET", Endpoint(id, "/ttd/position"), nullptr, caller, [](const Json::Value& b) {
                        return "At " + FormatTimePoint(b["current"]["frame"], b["current"]["tinframe"]) + ", session ends at " +
                               FormatTimePoint(b["session_end"]["frame"], b["session_end"]["tinframe"]) + " (state " +
                               b["state"].asString() + ")";
                    }, done);
                }
                else if (action == "markers")
                {
                    CallAndSummarize("GET", Endpoint(id, "/ttd/markers"), nullptr, caller, [](const Json::Value& b) {
                        std::ostringstream out;
                        out << b["count"].asUInt64() << " marker(s)";
                        const Json::Value& markers = b["markers"];
                        for (Json::ArrayIndex i = 0; i < markers.size() && i < 32; ++i)
                        {
                            out << "\n- " << FormatMarker(markers[i]);
                        }
                        if (markers.size() > 32)
                        {
                            out << "\n... " << (markers.size() - 32) << " more (see structuredContent)";
                        }
                        out << "\n(replay barriers: seek and reverse search stop at them)";
                        return out.str();
                    }, done);
                }
                else if (action == "seek")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/seek"), body.get(), caller, [](const Json::Value& b) {
                        std::string text = std::string(b["reached"].asBool() ? "Reached " : "Stopped at ") +
                                           FormatTimePoint(b["arrived_at"]["frame"], b["arrived_at"]["tinframe"]) + " (" +
                                           b["halt_reason"].asString() + ")";
                        if (b.isMember("blocking_marker"))
                        {
                            text += " - blocked by marker " + FormatMarker(b["blocking_marker"]);
                        }
                        return text;
                    }, done);
                }
                else if (action == "step_back_frame" || action == "step_forward_frame")
                {
                    const std::string route = action == "step_back_frame" ? "/ttd/step-back" : "/ttd/step-forward";
                    CallAndSummarize("POST", Endpoint(id, route), nullptr, caller, [](const Json::Value& b) {
                        return std::string(b["stepped"].asBool() ? "Stepped to " : "Could not step; still at ") +
                               FormatTimePoint(b["frame"], b["tinframe"]);
                    }, done);
                }
                else if (action == "step_back_instruction" || action == "step_forward_instruction")
                {
                    Json::Value stepBody;
                    stepBody["dir"] = action == "step_back_instruction" ? "back" : "forward";
                    CallAndSummarize("POST", Endpoint(id, "/ttd/step-instruction"), &stepBody, caller, [](const Json::Value& b) {
                        return std::string(b["stepped"].asBool() ? "Stepped one instruction " : "Could not step ") +
                               b["dir"].asString() + ", at " + FormatTimePoint(b["frame"], b["tinframe"]);
                    }, done);
                }
                else if (action == "reverse_step")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/reverse-step"), body.get(), caller, [](const Json::Value& b) {
                        return std::string(b["reached"].asBool() ? "Stepped back to " : "Could not step back fully; at ") +
                               FormatTimePoint(b["frame"], b["tinframe"]);
                    }, done);
                }
                else if (action == "reverse_continue")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/reverse-continue"), body.get(), caller, [](const Json::Value& b) {
                        std::string text = b["matched"].asBool()
                                               ? "Hit PC " + Hex16(b["pc"].asUInt()) + " at " + FormatTimePoint(b["frame"], b["tinframe"])
                                               : std::string("No PC match");
                        if (b.isMember("blocked_by_marker"))
                        {
                            text += " - blocked by marker " + FormatMarker(b["blocked_by_marker"]);
                        }
                        return text + "." + FormatSearchWindow(b);
                    }, done);
                }
                else if (action == "find_last")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/find-last"), body.get(), caller, [](const Json::Value& b) {
                        if (b["found"].asBool())
                        {
                            std::string text = "Last " + b["access"].asString() + " at " + FormatTimePoint(b["frame"], b["tinframe"]) +
                                               " by PC " + Hex16(b["pc"].asUInt()) + ", value " + std::to_string(b["value"].asUInt());
                            if (!b["phys_page"].isNull())
                            {
                                text += ", RAM page " + std::to_string(b["phys_page"].asUInt());
                            }
                            return text + "." + FormatSearchWindow(b) + " Seek to that frame/tinframe to inspect the machine there.";
                        }
                        if (b["blocked"].asBool())
                        {
                            Json::Value marker;
                            marker["frame"] = b["marker_frame"];
                            marker["tinframe"] = b["marker_tinframe"];
                            marker["kind"] = b["marker_kind"];
                            marker["reason"] = b["marker_reason"];
                            return "Not found after the replay barrier " + FormatMarker(marker) +
                                   "; the search cannot look past it." + FormatSearchWindow(b);
                        }
                        return "Not found in the recorded history." + FormatSearchWindow(b);
                    }, done);
                }
                else if (action == "port_events")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/port-events"), body.get(), caller, [](const Json::Value& b) {
                        std::ostringstream out;
                        out << b["count"].asUInt64() << " hit(s)" << (b["truncated"].asBool() ? " (more than the limit)" : "")
                            << " among " << b["scanned"].asUInt64() << " " << b["direction"].asString() << " record(s)";
                        const Json::Value& hits = b["hits"];
                        const Json::ArrayIndex shown = std::min<Json::ArrayIndex>(hits.size(), 40);
                        for (Json::ArrayIndex i = 0; i < shown; i++)
                        {
                            const Json::Value& h = hits[i];
                            out << "\n  " << FormatTimePoint(h["frame"], h["tinframe"]) << " PC " << Hex16(h["pc"].asUInt())
                                << " port " << Hex16(h["port"].asUInt()) << " value " << h["value"].asUInt();
                            if (h.isMember("ay_register"))
                                out << " (R" << h["ay_register"].asInt() << ")";
                        }
                        if (hits.size() > shown)
                            out << "\n  ... " << (hits.size() - shown) << " more in the structured result";
                        if (hits.size() > 0)
                            out << "\nSeek to a hit's frame/tinframe to inspect the machine there.";
                        return out.str();
                    }, done);
                }
                else if (action == "resume")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/resume"), body.get(), caller, [id](const Json::Value& b) {
                        return b["resumed"].asBool()
                                   ? "Recording resumed on " + id + " from " + FormatTimePoint(b["frame"], b["tinframe"]) +
                                         "; history after that point was discarded and the emulator is running"
                                   : "Could not resume from " + FormatTimePoint(b["frame"], b["tinframe"]) + " (state " +
                                         b["state"].asString() + "); resume needs the machine positioned in history - " +
                                         "seek or step there first";
                    }, done);
                }
                else if (action == "dump")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/dump"), body.get(), caller, [](const Json::Value& b) {
                        return "Session written to " + b["path"].asString() + " (" + std::to_string(b["bytes"].asInt64()) + " bytes)";
                    }, done);
                }
                else if (action == "load")
                {
                    CallAndSummarize("POST", Endpoint(id, "/ttd/load"), body.get(), caller, [](const Json::Value& b) {
                        return "Loaded " + b["path"].asString() + ": frames " + std::to_string(b["session_start_frame"].asUInt64()) +
                               ".." + std::to_string(b["current_end_frame"].asUInt64()) + ", " +
                               std::to_string(b["checkpoint_count"].asUInt64()) + " checkpoint(s), state " + b["state"].asString() +
                               ". Use 'seek' to position the machine inside it.";
                    }, done);
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

/// region <rzx_playback>

namespace
{

void RegisterRzxPlayback(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"].append("play");
    schema["properties"]["action"]["enum"].append("stop");
    schema["properties"]["action"]["enum"].append("status");
    schema["properties"]["action"]["enum"].append("seek");
    schema["properties"]["action"]["description"] =
        "play a recording, stop playing, report the playback, or seek to a frame";
    schema["properties"]["frame"]["type"] = "integer";
    schema["properties"]["frame"]["description"] =
        "seek: the frame boundary to move to (0 = start); back is fast (keyframes), forward plays on";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "play: the .rzx file (uploaded when it exists on the MCP host)";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["desync_mode"]["type"] = "string";
    schema["properties"]["desync_mode"]["enum"].append("strict");
    schema["properties"]["desync_mode"]["enum"].append("tolerant");
    schema["properties"]["desync_mode"]["description"] =
        "play: strict (default) stops at the first desync; tolerant counts desyncs and goes on";
    schema["properties"]["ei_short_frame_blocks_int"]["type"] = "boolean";
    schema["properties"]["ei_short_frame_blocks_int"]["description"] =
        "play: a 1-2 fetch frame after EI means the interrupt was blocked (for files that need it)";
    schema["properties"]["ld_air_parity_quirk"]["type"] = "boolean";
    schema["properties"]["ld_air_parity_quirk"]["description"] = "play: NMOS LD A,I / LD A,R parity quirk on the frame interrupt";
    schema["properties"]["ignore_later_snapshots"]["type"] = "boolean";
    schema["properties"]["ignore_later_snapshots"]["description"] = "play: skip snapshot blocks after the first";
    schema["properties"]["switch_model"]["type"] = "boolean";
    schema["properties"]["switch_model"]["default"] = true;
    schema["properties"]["switch_model"]["description"] =
        "play: switch to the recording's model (a new emulator id, reported as emulator_id) when this one differs";
    schema["required"].append("action");

    registry.Register(
        "rzx_playback",
        "Play RZX input recordings (RZX Archive game completions and the like): the start snapshot loads, then every "
        "IN and interrupt follows the recording to its end, and the machine runs live from there. Check progress and "
        "desyncs with 'status'. A recording made on another model switches the model first: the answer's emulator_id "
        "is the new target. load_software with an .rzx path does the same as 'play'.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            if (action == "stop")
            {
                ResolveAndForward(args, "POST", "/rzx/stop", nullptr, caller, "RZX playback stopped", done);
                return;
            }
            if (action == "status")
            {
                TargetResolver::ResolveFromArgs(args, caller, [&caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    caller.Call("GET", Endpoint(idOrError, "/rzx/status"), nullptr,
                                [done, idOrError](int status, Json::Value body) {
                                    if (status < 200 || status >= 300)
                                    {
                                        done(ToolResult::Error("RZX status failed (HTTP " + std::to_string(status) +
                                                               "): " + DescribeErrorBody(body)));
                                        return;
                                    }
                                    const std::string summary = body["summary"].asString();
                                    done(ToolResult::Ok("RZX on " + idOrError + ": " + summary, std::move(body)));
                                });
                });
                return;
            }
            if (action == "seek")
            {
                if (!args.isMember("frame") || !args["frame"].isIntegral() || args["frame"].asInt64() < 0)
                {
                    done(ToolResult::Error("Action 'seek' requires 'frame' (a frame number, 0 = start)"));
                    return;
                }
                Json::Value body;
                body["frame"] = args["frame"];
                ResolveAndForward(args, "POST", "/rzx/seek", &body, caller, "RZX playback moved", done);
                return;
            }
            if (action != "play")
            {
                done(ToolResult::Error("Unknown action '" + action + "': expected play, stop, status or seek"));
                return;
            }

            const std::string path = args["path"].asString();
            if (path.empty())
            {
                done(ToolResult::Error("Action 'play' requires 'path'"));
                return;
            }
            if (args.isMember("desync_mode") && args["desync_mode"].asString() != "strict" &&
                args["desync_mode"].asString() != "tolerant")
            {
                done(ToolResult::Error("'desync_mode' must be 'strict' or 'tolerant'"));
                return;
            }

            auto body = std::make_shared<Json::Value>(Json::objectValue);
            for (const char* key : {"desync_mode", "ei_short_frame_blocks_int", "ld_air_parity_quirk",
                                    "ignore_later_snapshots", "switch_model"})
            {
                if (args.isMember(key))
                    (*body)[key] = args[key];
            }

            // A file on the MCP host is uploaded with the options as query parameters
            auto content = std::make_shared<std::vector<uint8_t>>();
            const bool isLocalFile = TryReadLocalFile(path, *content);
            const std::string filename = ExtractFilename(path);
            TargetResolver::ResolveFromArgs(args, caller, [&caller, done, body, content, isLocalFile, path,
                                                           filename](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                if (isLocalFile)
                {
                    std::string query;
                    for (const std::string& key : body->getMemberNames())
                    {
                        const Json::Value& value = (*body)[key];
                        query += (query.empty() ? "?" : "&") + key + "=" +
                                 (value.isBool() ? (value.asBool() ? "true" : "false") : value.asString());
                    }
                    std::map<std::string, std::string> headers;
                    headers["X-Filename"] = filename;
                    ForwardCallRaw("POST", Endpoint(idOrError, "/rzx/play" + query), *content, headers, caller,
                                   "Playing RZX " + filename + " (uploaded)", done);
                    return;
                }
                (*body)["path"] = path;
                ForwardCall("POST", Endpoint(idOrError, "/rzx/play"), body.get(), caller, "Playing RZX " + path, done);
            });
        });
}

} // namespace

/// endregion </rzx_playback>

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
    RegisterJoystickInput(*registry);

    // TD-1 — time_travel: the full TTD surface (session, navigation, reverse
    // search, .ttd files, TD-4 bookmarks, TD-7 coverage index)
    RegisterTimeTravel(*registry);

    // rzx_playback: RZX input recordings (play / stop / status)
    RegisterRzxPlayback(*registry);

    // Phase 2 — smart tools
    RegisterManageSymbols(*registry);
    RegisterDebugCode(*registry);
    RegisterAnalyzePerformance(*registry);
    RegisterCaptureMedia(*registry);
    RegisterMediaSlots(*registry);

    // Router tools (search_api + invoke_api)
    RegisterRouterTools(*registry, std::move(caller));

    return registry;
}

/// endregion </Registry composition>

} // namespace mcp
