// LuaEmulator bindings: labels, symbols and the boards (Profi, Sprinter, buses).
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterLabelsAndBoards(sol::state& lua)
{
    // Labels/Symbols
    // Every label function takes the CPU last: "gs" / "ms" for the sound card CPU's own labels
    lua.set_function("label_get", [this](const std::string& name, sol::optional<std::string> cpu) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return result;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return result;
        LabelManager* lm = ScriptLabels(cpu);
        auto label = lm ? lm->GetLabelByName(name) : nullptr;
        if (!label) return result;
        result["name"] = label->name;
        result["address"] = label->address;
        if (label->bank != UINT16_MAX) {
            result["bank"] = label->bank;
            result["bankType"] = label->isROM() ? "rom" : "ram";
        }
        result["type"] = label->type;
        result["module"] = label->module;
        result["comment"] = label->comment;
        result["active"] = label->active;
        return result;
    });

    lua.set_function("label_at", [this](uint16_t address, sol::optional<std::string> cpu) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return result;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return result;
        LabelManager* lm = ScriptLabels(cpu);
        auto label = lm ? lm->GetLabelByZ80Address(address) : nullptr;
        if (!label) return result;
        result["name"] = label->name;
        result["address"] = label->address;
        if (label->bank != UINT16_MAX) {
            result["bank"] = label->bank;
            result["bankType"] = label->isROM() ? "rom" : "ram";
        }
        result["type"] = label->type;
        result["module"] = label->module;
        result["active"] = label->active;
        return result;
    });

    lua.set_function("label_add", [this](const std::string& name, uint16_t address,
                                         sol::optional<std::string> type,
                                         sol::optional<std::string> module,
                                         sol::optional<std::string> comment,
                                         sol::optional<std::string> cpu) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return false;
        LabelManager* lm = ScriptLabels(cpu);
        return lm && lm->AddLabel(name, address, UINT16_MAX, UINT16_MAX,
                                  type.value_or(""), module.value_or(""), comment.value_or(""));
    });

    lua.set_function("label_remove", [this](const std::string& name, sol::optional<std::string> cpu) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return false;
        LabelManager* lm = ScriptLabels(cpu);
        return lm && lm->RemoveLabel(name);
    });

    lua.set_function("label_count", [this](sol::optional<std::string> cpu) -> int {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return 0;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return 0;
        LabelManager* lm = ScriptLabels(cpu);
        return lm ? static_cast<int>(lm->GetLabelCount()) : 0;
    });

    lua.set_function("labels_list", [this](sol::optional<std::string> module,
                                           sol::optional<std::string> type,
                                           sol::optional<std::string> cpu) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return result;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return result;
        LabelManager* lm = ScriptLabels(cpu);
        if (!lm) return result;

        LabelManager::LabelFilter filter;
        if (module.has_value()) filter.module = module.value();
        if (type.has_value()) filter.type = type.value();

        auto labels = lm->GetLabels(filter);
        int idx = 1;
        for (const auto& label : labels) {
            sol::table lbl = lua_view.create_table();
            lbl["name"] = label->name;
            lbl["address"] = label->address;
            if (label->bank != UINT16_MAX) lbl["bank"] = label->bank;
            lbl["type"] = label->type;
            lbl["module"] = label->module;
            lbl["active"] = label->active;
            result[idx++] = lbl;
        }
        return result;
    });

    lua.set_function("labels_clear", [this](sol::optional<std::string> cpu) {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return;
        LabelManager* lm = ScriptLabels(cpu);
        if (lm) lm->ClearAllLabels();
    });

    lua.set_function("symbols_load", [this](const std::string& path, sol::optional<std::string> cpu) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return false;
        LabelManager* lm = ScriptLabels(cpu);
        return lm && lm->LoadLabels(path);
    });

    lua.set_function("symbols_save", [this](const std::string& path, sol::optional<std::string> cpu) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) return false;
        LabelManager* lm = ScriptLabels(cpu);
        return lm && lm->SaveLabels(path);
    });

    // Symbol files and sets through SymbolControl (the same verbs, checks and fields as the WebAPI, CLI, MCP and
    // Python): symbols_import("f.sym") or symbols_import{path=..., format=, set=, space=, base=, policy=},
    // symbols_export{path=..., format=, sets={...}, pages=}, symbols_sets(), symbols_set(id, {enabled=, priority=}),
    // symbols_drop(id), symbols_formats(), symbols_detect(path), symbols_scan(), symbols_import_live{scanner=, page=,
    // offset=, set=, policy=}. A refusal: the table has ok = false, error = why
    lua.set_function("symbols_formats", [this](sol::this_state ts) -> sol::object { return symbolsRun(ts, "formats", {}); });
    lua.set_function("symbols_sets", [this](sol::this_state ts, sol::optional<std::string> cpu) -> sol::object {
        return symbolsRun(ts, "sets", cpu ? std::map<std::string, std::string>{{"cpu", *cpu}} : std::map<std::string, std::string>{});
    });
    lua.set_function("symbols_detect", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "detect", arg, "path"); });
    lua.set_function("symbols_import", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "import", arg, "path"); });
    lua.set_function("symbols_export", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "export", arg, "path"); });
    lua.set_function("symbols_drop", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "drop", arg, "id"); });
    // Assembler sources through AsmControl (the same verbs and fields as the WebAPI, CLI, MCP, Python and the Qt disk
    // browser): asm_formats(), asm_dialects(), asm_files("A"), asm_detect(path), asm_decode(path) or asm_decode{path=,
    // codec=, version=, output=}, asm_encode{text= | input=, codec=, version=, output=}, asm_convert{path=, to=, ...}.
    // A path is a host file or "disk:A/NAME.T". A refusal: ok = false, error = why
    lua.set_function("asm_formats", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "formats", {}); });
    lua.set_function("asm_dialects", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "dialects", {}); });
    lua.set_function("asm_files", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "files", SymbolOptions(arg, "drive")); });
    lua.set_function("asm_detect", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "detect", SymbolOptions(arg, "path")); });
    lua.set_function("asm_decode", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "decode", SymbolOptions(arg, "path")); });
    lua.set_function("asm_encode", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "encode", SymbolOptions(arg, "text")); });
    lua.set_function("asm_convert", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "convert", SymbolOptions(arg, "path")); });
    // The source an assembler holds in RAM (asm-synchronizer): asm_sync_status(), asm_sync_probe(), asm_sync_extract{as=, to=,
    // output=}, asm_sync_watch{interval=, quiet=, as=, to=, output=} (live labels in live:sync:<assembler>), asm_sync_unwatch(),
    // asm_sync_hints(), asm_sync_inject{text= | path=, from=, title=, force=, backup=} (a host source as the assembler's text);
    // snapshot= (a .szx / .sna / .z80 file) makes status / probe / extract / inject work on that file (inject writes output=);
    // asm_sync_push_watch{path=, from=, title=, force=, interval=, quiet=} injects each save of the host file, asm_sync_push_unwatch()
    lua.set_function("asm_sync_status", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-status", SymbolOptions(arg, "assembler")); });
    lua.set_function("asm_sync_probe", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-probe", SymbolOptions(arg, "snapshot")); });
    lua.set_function("asm_sync_extract", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-extract", SymbolOptions(arg, "as")); });
    lua.set_function("asm_sync_watch", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-watch", SymbolOptions(arg, "assembler")); });
    lua.set_function("asm_sync_unwatch", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "sync-unwatch", {}); });
    lua.set_function("asm_sync_hints", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "sync-hints", {}); });
    lua.set_function("asm_sync_inject", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-inject", SymbolOptions(arg, "path")); });
    lua.set_function("asm_sync_push_watch", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-push-watch", SymbolOptions(arg, "path")); });
    lua.set_function("asm_sync_push_unwatch", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "sync-push-unwatch", {}); });
    lua.set_function("symbols_scan", [this](sol::this_state ts) -> sol::object { return symbolsRun(ts, "scan", {}); });
    lua.set_function("symbols_import_source", [this](sol::this_state ts, sol::object arg) -> sol::object {
        return symbolsCall(ts, "import-source", arg, "path");
    });
    lua.set_function("symbols_import_live", [this](sol::this_state ts, sol::optional<sol::table> opts) -> sol::object {
        return symbolsRun(ts, "import-live", opts ? SymbolOptions(sol::make_object(ts, *opts), "") : std::map<std::string, std::string>{});
    });
    lua.set_function("symbols_set", [this](sol::this_state ts, sol::object id, sol::optional<sol::table> opts) -> sol::object {
        std::map<std::string, std::string> options;
        if (opts)
            options = SymbolOptions(sol::make_object(ts, *opts), "");
        if (id.is<std::string>())
            options["id"] = id.as<std::string>();
        else
            for (auto& [key, value] : SymbolOptions(id, "id"))
                options[key] = value;
        return symbolsRun(ts, "set", std::move(options));
    });

    // Disassembly
    // disasm([address [, count [, cpu] [, before]]]): "gs" / "ms" the sound card CPU's code, from its PC
    // (cpuview.h); before: rows above the address first (also as the third argument: disasm(a, n, before))
    lua.set_function("disasm", [this](sol::this_state s, sol::optional<int> address, sol::optional<int> count,
                                      sol::object third, sol::optional<int> fourth) -> sol::table {
        sol::table result = _lua->create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return result;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) return result;
        sol::optional<std::string> cpuName;
        int before = fourth.value_or(0);
        if (third.is<std::string>())
            cpuName = third.as<std::string>();
        else if (third.is<int>())
            before = third.as<int>();
        const DebugCpuId cpu = ScriptCpu(cpuName);
        uint16_t at = ctx->pCore->GetZ80()->pc;
        if (IsCardCpu(cpu))
            CpuView::GetRegister(ctx, cpu, "pc", at);
        if (address && *address >= 0)
            at = static_cast<uint16_t>(*address);
        const auto lines = static_cast<size_t>(std::clamp(count.value_or(10) < 1 ? 10 : count.value_or(10), 1, 100));
        const auto rowsBefore = static_cast<size_t>(std::clamp(before, 0, 100));
        // One builder with GET /disasm: the virtual window (anchors, the markup's data ranges as db rows)
        const StateNode listing = IsCardCpu(cpu) ? CpuView::Disasm(ctx, cpu, at, lines, rowsBefore)
                                                 : DebugSnapshot::Disasm(ctx, at, lines, rowsBefore);
        int index = 1;
        if (const StateNode* instructions = listing.find("instructions"))
            for (const StateNode& line : instructions->items)
                result[index++] = StateNodeToLua(s, line);
        return result;
    });

    // Physical page disassembly
    lua.set_function("disasm_page", [this](const std::string& type, int page, sol::optional<int> offset, sol::optional<int> count) -> sol::table {
        sol::table result = _lua->create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return result;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) return result;
        
        Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
        Memory* memory = ctx->pMemory;
        LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
        
        bool isROM = (type == "rom");
        if (!isROM && type != "ram")
            throw std::invalid_argument("disasm_page: unknown page type '" + type + "' (use ram, rom)");
        ValidatePageIndex("disasm_page", type, page, 0);
        uint8_t* pageBase = isROM ? memory->ROMPageHostAddress(static_cast<uint8_t>(page))
                                  : memory->RAMPageAddress(static_cast<uint16_t>(page));
        if (!pageBase) return result;
        
        int off = offset.value_or(0);
        int cnt = count.value_or(10);
        if (off < 0) off = 0;
        if (off >= PAGE_SIZE) off = PAGE_SIZE - 1;
        if (cnt < 1) cnt = 10;
        if (cnt > 100) cnt = 100;
        
        uint16_t currentOffset = static_cast<uint16_t>(off);
        int idx = 1;
        for (int i = 0; i < cnt && currentOffset < PAGE_SIZE; ++i) {
            std::vector<uint8_t> buffer(4, 0);
            for (int j = 0; j < 4 && (currentOffset + j) < PAGE_SIZE; ++j) {
                buffer[j] = pageBase[currentOffset + j];
            }
            
            uint8_t cmdLen = 0;
            DecodedInstruction decoded;
            std::string mnemonic = disasm->disassembleSingleCommand(buffer, currentOffset, &cmdLen, &decoded);
            if (cmdLen == 0) cmdLen = 1;
            
            sol::table instr = _lua->create_table();
            instr["offset"] = currentOffset;
            std::string hexBytes;
            for (uint8_t j = 0; j < cmdLen; ++j) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X", buffer[j]);
                hexBytes += buf;
            }
            instr["bytes"] = hexBytes;
            instr["mnemonic"] = mnemonic;
            instr["size"] = cmdLen;
            
            // Label at the instruction offset itself (e.g. jump destination marker)
            if (labelMgr) {
                auto label = labelMgr->GetLabelByZ80Address(currentOffset);
                if (label && !label->name.empty())
                    instr["label"] = label->name;
            }
            
            // Target address for jumps/calls. Static view has no runtime registers, so indirect
            // targets (JP (HL), JP (IX)) can not be resolved and the field is omitted
            if (decoded.hasJump || decoded.hasRelativeJump) {
                uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
                if (!decoded.hasIndirect) {
                    instr["target"] = target;
                    
                    if (labelMgr) {
                        auto targetLabel = labelMgr->GetLabelByZ80Address(target);
                        if (targetLabel && !targetLabel->name.empty())
                            instr["targetLabel"] = targetLabel->name;
                    }
                }
            }
            
            result[idx++] = instr;
            currentOffset += cmdLen;
        }
        return result;
    });

    // Screen state
    lua.set_function("screen_get_mode", [this]() -> std::string {
        if (!effectiveEmulator()) return "";
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pScreen) return "";
        return Screen::GetVideoModeName(ctx->pScreen->GetVideoMode());
    });

    lua.set_function("screen_get_border", [this]() -> int {
        if (!effectiveEmulator()) return 0;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pScreen) return 0;
        return ctx->pScreen->GetBorderColor();
    });

    lua.set_function("screen_get_flash", [this]() -> int {
        if (!effectiveEmulator()) return 0;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pScreen) return 0;
        return ctx->pScreen->_vid.flash;
    });

    lua.set_function("screen_get_active", [this]() -> int {
        if (!effectiveEmulator()) return 0;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pScreen) return 0;
        return ctx->pScreen->GetActiveScreen();
    });

    // Screen reports: the same core reports every automation module returns
    lua.set_function("screen_state", [this](sol::this_state s, sol::optional<bool> verbose) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Screen(ctx, verbose.value_or(false)));
    });
    lua.set_function("screen_mode", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::ScreenMode(ctx));
    });
    lua.set_function("screen_flash", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::ScreenFlash(ctx));
    });
    // Per-cell ink/paper/bright/flash decoded from screen attribute memory
    lua.set_function("screen_attributes", [this](sol::this_state s, sol::optional<int> screen) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::ScreenAttributes(ctx, screen.value_or(-1)));
    });
    // Former name of screen_mode, kept for existing scripts
    lua.set_function("screen_video_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::ScreenMode(ctx));
    });

    // Device state reports (core DeviceState: the same trees the WebAPI,
    // Python, CLI and MCP return). Optional chip index -> the chip's
    // full report, no index -> the overview
    lua.set_function("audio_ay_state", [this](sol::this_state s, sol::optional<int> chip) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, chip ? DeviceState::AyChip(ctx, *chip) : DeviceState::Ay(ctx));
    });
    lua.set_function("audio_fm_state", [this](sol::this_state s, sol::optional<int> chip) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, chip ? DeviceState::FmChip(ctx, *chip) : DeviceState::Fm(ctx));
    });
    lua.set_function("ide_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Ide(emulator->GetContext()));
    });

    // CD audio of the ATAPI CD drives (CdAudioControl, PLAN #83): cdaudio_state() reports every
    // drive; cdaudio(verb, drive, {options}) runs status / play / pause / resume / stop / volume / mixer
    lua.set_function("cdaudio_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, CdAudioControl::State(emulator->GetContext()));
    });
    lua.set_function("cdaudio", [this](sol::this_state s, const std::string& verb, sol::optional<std::string> drive,
                                       sol::optional<sol::table> opts) -> sol::object {
        CdAudioRequest request;
        request.verb = verb;
        request.drive = drive.value_or("");
        if (opts)
        {
            for (const auto& [key, value] : *opts)
            {
                std::string text;
                if (value.is<bool>())
                    text = value.as<bool>() ? "true" : "false";
                else if (value.get_type() == sol::type::number)
                {
                    const double number = value.as<double>();
                    text = number == static_cast<double>(static_cast<long long>(number))
                               ? std::to_string(static_cast<long long>(number))
                               : std::to_string(number);
                }
                else if (value.is<std::string>())
                    text = value.as<std::string>();
                request.options[key.as<std::string>()] = text;
            }
        }
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            StateNode none = StateNode::Object();
            none["ok"] = false;
            none["error"] = "unknown-emulator";
            none["message"] = "no emulator selected";
            return StateNodeToLua(s, none);
        }
        return StateNodeToLua(s, CdAudioControl(emulator->GetContext()).Execute(request).ToValue());
    });

    lua.set_function("tsconf_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::TsConf(emulator->GetContext()));
    });
    lua.set_function("tsconf_tsu", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::TsConfTsu(emulator->GetContext()));
    });

    // Sprinter Sp2000: the same reports every interface uses (DeviceState::Sprinter,
    // SprinterPortTable, SprinterPortLookup). Options {map=0-3, dos=0|1, pn5=0|1, rw="r"|"w"|"rw"},
    // omitted = the machine's current map / DOS / PN5 and both directions
    auto sprinterQuery = [](sol::optional<sol::table> options, DeviceState::SprinterPortQuery& query, std::string& error) {
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::boolean)
                return value.as<bool>() ? "1" : "0";
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        return DeviceState::SprinterPortQueryFromStrings(text("map"), text("dos"), text("pn5"), text("rw"), query, error);
    };
    lua.set_function("sprinter_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Sprinter(emulator->GetContext()));
    });
    lua.set_function("sprinter_text", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::SprinterText(emulator->GetContext()));
    });
    // sprinter_video{page=0|1, all=true, squares=false}: the mode table per square (DeviceState::SprinterVideo)
    lua.set_function("sprinter_video", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::boolean)
                return value.as<bool>() ? "1" : "0";
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        DeviceState::SprinterVideoQuery query;
        std::string error;
        if (!DeviceState::SprinterVideoQueryFromStrings(text("page"), text("all"), text("squares"), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::SprinterVideo(emulator->GetContext(), query)));
        return out;
    });
    // sprinter_palette([k]): k = 0-7, "all" or "used" (default) (DeviceState::SprinterPalette)
    lua.set_function("sprinter_palette", [this](sol::this_state s, sol::object k) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string text;
        if (k.get_type() == sol::type::number)
            text = std::to_string(k.as<long long>());
        else if (k.get_type() == sol::type::string)
            text = k.as<std::string>();
        int palette = DeviceState::kSprinterPalettesUsed;
        std::string error;
        if (!DeviceState::SprinterPaletteFromString(text, palette, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::SprinterPalette(emulator->GetContext(), palette)));
        return out;
    });
    lua.set_function("sprinter_bios", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::SprinterBios(emulator->GetContext()));
    });
    // sprinter_bios_select{bios="3.06", fast_start=false, accel_int_suspend=false, reset=true}: the image loads at
    // the reset (now unless reset=false) -> the BIOS report, or nil, error (SprinterBios, DeviceState::SprinterBiosSelect)
    lua.set_function("sprinter_bios_select", [this](sol::this_state s, sol::table options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            sol::object value = options[key];
            if (value.get_type() == sol::type::boolean)
                return value.as<bool>() ? "1" : "0";
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        SprinterBios::Options parsed;
        std::string error;
        if (!SprinterBios::OptionsFromStrings(text("bios"), text("fast_start"), text("accel_int_suspend"), text("reset"),
                                              parsed, error))
            return mouseError(s, error);
        const StateNode report = DeviceState::SprinterBiosSelect(emulator->GetContext(), parsed);
        const StateNode* available = report.find("available");
        if (available && !available->b)
            return mouseError(s, report.find("description") ? report.find("description")->s : std::string("failed"));
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, report));
        return out;
    });
    // sprinter_zx_mode([deep]): the ZX (Spectrum) mode report (DeviceState::SprinterZxMode); deep = false skips
    // the whole-RAM search for the launcher's option table
    lua.set_function("sprinter_zx_mode", [this](sol::this_state s, sol::optional<bool> deep) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::SprinterZxMode(emulator->GetContext(), deep.value_or(true)));
    });
    // next_state() / next_regs() / next_mmu(): the ZX Spectrum Next reports (DeviceState::Next / NextRegs / NextMmu;
    // available=false on other machines)
    lua.set_function("next_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Next(emulator->GetContext()));
    });
    lua.set_function("next_regs", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextRegs(emulator->GetContext()));
    });
    lua.set_function("next_mmu", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextMmu(emulator->GetContext()));
    });
    // next_reg_journal([{regs="07,02", sources="nextreg,copper", since=N, from=F, to=F, limit=N}]): who wrote which NextREG,
    // when (frame, T, PC) and through which door (DeviceState::NextRegJournalReport); nil + error on a bad option.
    // Off by default: next_reg_journal_control({enabled=true}) first
    lua.set_function("next_reg_journal", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        NextRegJournalQuery query;
        std::string error;
        if (!NextRegJournalQueryFromStrings(text("regs"), text("sources"), text("since"), text("from"), text("to"), text("limit"),
                                            query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextRegJournalReport(emulator->GetContext(), query)));
        return out;
    });
    // next_reg_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the NextREG journal
    lua.set_function("next_reg_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        bool clear = false;
        size_t capacity = 0;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
            sol::object c = (*options)["clear"];
            if (c.get_type() == sol::type::boolean)
                clear = c.as<bool>();
            sol::object n = (*options)["capacity"];
            if (n.get_type() == sol::type::number)
                capacity = static_cast<size_t>(n.as<long long>());
        }
        return StateNodeToLua(s, DeviceState::NextRegJournalControl(emulator->GetContext(), enable, clear, capacity));
    });
    // next_sd([{kinds="command,read,write", card=0|1, since=N, from=F, to=F, limit=N}]): the Next's SD cards, the SPI engine and the
    // SD journal (DeviceState::NextSdReport); nil + error on a bad option. The journal is off by default: next_sd_journal_control({enabled=true}) first
    lua.set_function("next_sd", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        NextSdJournalQuery query;
        std::string error;
        if (!NextSdJournalQueryFromStrings(text("kinds"), text("card"), text("since"), text("from"), text("to"), text("limit"), query,
                                           error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextSdReport(emulator->GetContext(), query)));
        return out;
    });
    // next_sd_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the SD journal
    lua.set_function("next_sd_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        bool clear = false;
        size_t capacity = 0;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
            sol::object c = (*options)["clear"];
            if (c.get_type() == sol::type::boolean)
                clear = c.as<bool>();
            sol::object n = (*options)["capacity"];
            if (n.get_type() == sol::type::number)
                capacity = static_cast<size_t>(n.as<long long>());
        }
        return StateNodeToLua(s, DeviceState::NextSdJournalControl(emulator->GetContext(), enable, clear, capacity));
    });
    // Bus analyzer (docs/inprogress/2026-10-11-bus-analyzer): bus_list(), bus_events(bus, {kinds=, slot=, since=, from=, to=,
    // limit=}), bus_findings(bus, {rules=, since=, limit=}), bus_control(bus|"all", {enabled=, checks=, clear=, capacity=, protocol_only=}),
    // bus_sd_timing(bus, {profile="ideal|realistic|slow", seed=N, slot=N}), bus_export(bus, {format="vcd|jsonl", ...}) -> text.
    // nil + error on a bad option or an unknown bus
    auto busOption = [](const sol::optional<sol::table>& options, const char* key) -> std::string {
        if (!options)
            return std::string();
        sol::object value = (*options)[key];
        if (value.get_type() == sol::type::number)
            return std::to_string(value.as<long long>());
        if (value.get_type() == sol::type::string)
            return value.as<std::string>();
        return std::string();
    };
    auto busSwitch = [](const sol::optional<sol::table>& options, const char* key) -> int {
        if (!options)
            return -1;
        sol::object value = (*options)[key];
        return value.get_type() == sol::type::boolean ? (value.as<bool>() ? 1 : 0) : -1;
    };
    auto busReply = [](sol::this_state s, const StateNode& node) -> sol::variadic_results {
        const StateNode* available = node.find("available");
        if (available && available->kind == StateNode::Kind::Bool && !available->b)
        {
            const StateNode* description = node.find("description");
            return mouseError(s, description ? description->s : "bus not available");
        }
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, node));
        return out;
    };
    lua.set_function("bus_list", [this, busReply](sol::this_state s) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        return busReply(s, DeviceState::BusList(emulator->GetContext()));
    });
    lua.set_function("bus_events", [this, busOption, busReply](sol::this_state s, const std::string& bus,
                                                               sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        BusEventQuery query;
        std::string error;
        if (!BusEventQueryFromStrings(busOption(options, "kinds"), busOption(options, "slot"), busOption(options, "since"),
                                      busOption(options, "from"), busOption(options, "to"), busOption(options, "limit"), query, error))
            return mouseError(s, error);
        return busReply(s, DeviceState::BusEvents(emulator->GetContext(), bus, query));
    });
    lua.set_function("bus_findings", [this, busOption, busReply](sol::this_state s, const std::string& bus,
                                                                 sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        BusFindingQuery query;
        std::string error;
        if (!BusFindingQueryFromStrings(busOption(options, "rules"), busOption(options, "since"), busOption(options, "limit"), query, error))
            return mouseError(s, error);
        return busReply(s, DeviceState::BusFindings(emulator->GetContext(), bus, query));
    });
    lua.set_function("bus_control", [this, busOption, busSwitch, busReply](sol::this_state s, const std::string& bus,
                                                                           sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        const std::string capacity = busOption(options, "capacity");
        return busReply(s, DeviceState::BusControl(emulator->GetContext(), bus, busSwitch(options, "enabled"), busSwitch(options, "checks"),
                                                   busSwitch(options, "clear") == 1,
                                                   capacity.empty() ? 0 : static_cast<size_t>(std::stoull(capacity)),
                                                   busSwitch(options, "protocol_only")));
    });
    lua.set_function("bus_sd_timing", [this, busOption, busReply](sol::this_state s, const std::string& bus,
                                                                  sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        const std::string slot = busOption(options, "slot");
        const std::string seed = busOption(options, "seed");
        return busReply(s, DeviceState::BusSdTiming(emulator->GetContext(), bus, slot.empty() ? -1 : std::stoi(slot),
                                                    busOption(options, "profile"), seed.empty() ? 0 : std::stoll(seed)));
    });
    lua.set_function("bus_export", [this, busOption](sol::this_state s, const std::string& bus,
                                                     sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        BusEventQuery query;
        std::string error;
        const std::string limit = busOption(options, "limit");
        if (!BusEventQueryFromStrings(busOption(options, "kinds"), busOption(options, "slot"), busOption(options, "since"),
                                      busOption(options, "from"), busOption(options, "to"), limit.empty() ? "0" : limit, query, error))
            return mouseError(s, error);
        const std::string format = busOption(options, "format");
        const std::string text = DeviceState::BusExport(emulator->GetContext(), bus, format.empty() ? "vcd" : format, query, error);
        if (!error.empty())
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(sol::make_object(s, text));
        return out;
    });
    // next_divmmc([{kinds="map_in,esx_call", since=N, from=F, to=F, limit=N}]): the Next's DivMMC, its automap entries and the
    // DivMMC journal (DeviceState::NextDivMmcReport); nil + error on a bad option. The journal is off by default: next_divmmc_journal_control({enabled=true}) first
    lua.set_function("next_divmmc", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        NextDivMmcJournalQuery query;
        std::string error;
        if (!NextDivMmcJournalQueryFromStrings(text("kinds"), text("since"), text("from"), text("to"), text("limit"), query,
                                           error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextDivMmcReport(emulator->GetContext(), query)));
        return out;
    });
    // next_divmmc_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the DivMMC journal
    lua.set_function("next_divmmc_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        bool clear = false;
        size_t capacity = 0;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
            sol::object c = (*options)["clear"];
            if (c.get_type() == sol::type::boolean)
                clear = c.as<bool>();
            sol::object n = (*options)["capacity"];
            if (n.get_type() == sol::type::number)
                capacity = static_cast<size_t>(n.as<long long>());
        }
        return StateNodeToLua(s, DeviceState::NextDivMmcJournalControl(emulator->GetContext(), enable, clear, capacity));
    });
    // next_audio() / next_input(): the Next's sound as configured (chips, pan, registers, DAC) and its joystick modes with the Kempston port
    // (DeviceState::NextAudioReport / NextInputReport; no side effect on the machine)
    lua.set_function("next_audio", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextAudioReport(emulator->GetContext()));
    });
    lua.set_function("next_input", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextInputReport(emulator->GetContext()));
    });
    // next_uart(): the Next's two UARTs (ESP, Pi) - baud, frame, FIFOs, status, interrupts, peers (DeviceState::NextUartReport; no side effect)
    lua.set_function("next_uart", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextUartReport(emulator->GetContext()));
    });
    // next_nmi([{kinds="int_ack,nmi_request", since=N, from=F, to=F, limit=N}]): the Next's NMI sources, Multiface, interrupts, reset and the
    // interrupt / NMI / reset journal (DeviceState::NextNmiReport); nil + error on a bad option. The journal is off by default:
    // next_nmi_journal_control({enabled=true}) first
    lua.set_function("next_nmi", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        NextIntJournalQuery query;
        std::string error;
        if (!NextIntJournalQueryFromStrings(text("kinds"), text("since"), text("from"), text("to"), text("limit"), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextNmiReport(emulator->GetContext(), query)));
        return out;
    });
    // next_nmi_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the interrupt / NMI / reset journal
    lua.set_function("next_nmi_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        bool clear = false;
        size_t capacity = 0;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
            sol::object c = (*options)["clear"];
            if (c.get_type() == sol::type::boolean)
                clear = c.as<bool>();
            sol::object n = (*options)["capacity"];
            if (n.get_type() == sol::type::number)
                capacity = static_cast<size_t>(n.as<long long>());
        }
        return StateNodeToLua(s, DeviceState::NextNmiJournalControl(emulator->GetContext(), enable, clear, capacity));
    });
    // next_magic_port([{since=N, limit=N}]): the magic debug port - its setting and the text a program wrote to it (DeviceState::NextMagicPortReport)
    lua.set_function("next_magic_port", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        uint64_t since = 0;
        size_t limit = 200;
        if (options)
        {
            sol::object a = (*options)["since"];
            if (a.get_type() == sol::type::number)
                since = static_cast<uint64_t>(a.as<long long>());
            sol::object b = (*options)["limit"];
            if (b.get_type() == sol::type::number)
                limit = static_cast<size_t>(b.as<long long>());
        }
        return StateNodeToLua(s, DeviceState::NextMagicPortReport(emulator->GetContext(), since, limit));
    });
    // next_magic_port_control({enabled=true|false, port=N, mode="hex|dec|ascii|line", clear=true}): switch the magic port, choose its port and mode
    lua.set_function("next_magic_port_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        int port = -1;
        bool clear = false;
        std::string mode;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
            sol::object p = (*options)["port"];
            if (p.get_type() == sol::type::number)
                port = static_cast<int>(p.as<long long>() & 0xFFFF);
            sol::object m = (*options)["mode"];
            if (m.get_type() == sol::type::string)
                mode = m.as<std::string>();
            sol::object c = (*options)["clear"];
            if (c.get_type() == sol::type::boolean)
                clear = c.as<bool>();
        }
        return StateNodeToLua(s, DeviceState::NextMagicPortControl(emulator->GetContext(), enable, port, mode, clear));
    });
    // next_dma() / next_video(): the Next's DMA and video layers (DeviceState::NextDma / NextVideo; no side effect on the machine)
    lua.set_function("next_dma", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextDma(emulator->GetContext()));
    });
    lua.set_function("next_video", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::NextVideo(emulator->GetContext()));
    });
    // next_palette([{palette="sprites_1"|0-7|"all", range="0-15"}]): the 9-bit palettes (DeviceState::NextPalette); nil + error on a bad option
    lua.set_function("next_palette", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options) return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number) return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string) return value.as<std::string>();
            return std::string();
        };
        NextPaletteQuery query;
        std::string error;
        if (!NextPaletteQueryFromStrings(text("palette"), text("range"), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextPalette(emulator->GetContext(), query)));
        return out;
    });
    // next_ports([port [, "r"|"w"]]): the internal port enable word and which device answers `port` (a number is the port, a string is hex text)
    lua.set_function("next_ports", [this](sol::this_state s, sol::optional<sol::object> port, sol::optional<std::string> access) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string portText;
        if (port && port->get_type() == sol::type::number)
            portText = StringHelper::Format("%llx", static_cast<unsigned long long>(port->as<long long>()));
        else if (port && port->get_type() == sol::type::string)
            portText = port->as<std::string>();
        NextPortsQuery query;
        std::string error;
        if (!NextPortsQueryFromStrings(portText, access.value_or(""), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextPorts(emulator->GetContext(), query)));
        return out;
    });
    // next_nextreg([reg [, changed]]): one NextREG with decoded bits, or all of them, without side effects (a number is the register)
    lua.set_function("next_nextreg", [this](sol::this_state s, sol::optional<sol::object> reg, sol::optional<bool> changed) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string regText;
        if (reg && reg->get_type() == sol::type::number)
            regText = StringHelper::Format("%llx", static_cast<unsigned long long>(reg->as<long long>()));
        else if (reg && reg->get_type() == sol::type::string)
            regText = reg->as<std::string>();
        NextRegReadQuery query;
        std::string error;
        if (!NextRegReadQueryFromStrings(regText, changed.value_or(false) ? "true" : "", query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextRegRead(emulator->GetContext(), query)));
        return out;
    });
    // next_copper([{from=0, count=64, raw=true}]): the copper list as a disassembly with the control mode, write address and pc
    // (DeviceState::NextCopper); next_sprites([{from=0, count=128, all=true}]): the 128 sprite attributes decoded, switches, flags,
    // pattern memory summary (DeviceState::NextSprites); nil + error on a bad option
    lua.set_function("next_copper", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options) return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number) return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string) return value.as<std::string>();
            if (value.get_type() == sol::type::boolean) return value.as<bool>() ? "true" : "false";
            return std::string();
        };
        NextCopperQuery query;
        std::string error;
        if (!NextCopperQueryFromStrings(text("from"), text("count"), text("raw"), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextCopper(emulator->GetContext(), query)));
        return out;
    });
    lua.set_function("next_sprites", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options) return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number) return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string) return value.as<std::string>();
            if (value.get_type() == sol::type::boolean) return value.as<bool>() ? "true" : "false";
            return std::string();
        };
        NextSpritesQuery query;
        std::string error;
        if (!NextSpritesQueryFromStrings(text("from"), text("count"), text("all"), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::NextSprites(emulator->GetContext(), query)));
        return out;
    });
    // next_pixel(x, y [, sx]): the pixel inspector of the last captured Next frame (DeviceState::NextPixel; design-layer-capture.md): x 0-319,
    // y 0-255, sx the sub-pixel 0 | 1. Returns {frame, pixel, winner = {layer, id}, colour = {rgb9, rgb}, layer_order, layers = {ula, layer2,
    // tilemap, sprites: {opaque, rgb9, wins, why, source}}}; nil + error on a bad coordinate, on another machine, with the capture off or
    // before the first frame
    lua.set_function("next_pixel", [this](sol::this_state s, int64_t x, int64_t y, sol::optional<int64_t> sx) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        NextPixelQuery query;
        std::string error;
        if (!NextPixelQueryFromStrings(std::to_string(x), std::to_string(y), sx ? std::to_string(*sx) : std::string(), query, error))
            return mouseError(s, error);
        const StateNode report = DeviceState::NextPixel(emulator->GetContext(), query);
        const StateNode* available = report.find("available");
        if (available && available->kind == StateNode::Kind::Bool && !available->b)
            return mouseError(s, "This machine is not a ZX Spectrum Next");
        const StateNode* ok = report.find("ok");
        if (ok && ok->kind == StateNode::Kind::Bool && !ok->b)
            return mouseError(s, report.find("message") ? report.find("message")->s : std::string("no frame"));
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, report));
        return out;
    });
    // next_nextreg_write(reg, value [, door]): write a NextREG through the board's write choke point (NextRegWriteControl); door = "nextreg"
    // (default, the NEXTREG instruction), "port" or "internal"; returns the reply table, or nil + error
    lua.set_function("next_nextreg_write", [this](sol::this_state s, int64_t reg, int64_t value, sol::optional<std::string> door) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        if (reg < 0 || reg > 255 || value < 0 || value > 255)
            return mouseError(s, "reg and value must be 0..255");
        NextRegWriteControl::Door which = NextRegWriteControl::Door::NextReg;
        if (!NextRegWriteControl::ParseDoor(door.value_or(""), which))
            return mouseError(s, "door must be nextreg, port or internal");
        const NextRegWriteControl::Result result =
            NextRegWriteControl::Write(emulator, static_cast<uint8_t>(reg), static_cast<uint8_t>(value), which, "lua");
        if (!result.ok)
            return mouseError(s, result.error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, NextRegWriteControl::ToState(result)));
        return out;
    });
    // sprinter_pld_journal([{kinds="cnf,port_1ffd", since=N, from=F, to=F, limit=N, source="live"|"ttd"}]): who
    // changed the PLD setup, when (DeviceState::SprinterJournal); nil + error on a bad option
    lua.set_function("sprinter_pld_journal", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        DeviceState::SprinterJournalQuery query;
        std::string error;
        if (!DeviceState::SprinterJournalQueryFromStrings(text("kinds"), text("since"), text("from"), text("to"), text("limit"),
                                                          text("source"), query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::SprinterJournal(emulator->GetContext(), query)));
        return out;
    });
    // sprinter_pld_journal_control({enabled=true|false, clear=true}): switch / clear the PLD journal
    lua.set_function("sprinter_pld_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        bool clear = false;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
            sol::object c = (*options)["clear"];
            if (c.get_type() == sol::type::boolean)
                clear = c.as<bool>();
        }
        return StateNodeToLua(s, DeviceState::SprinterJournalControl(emulator->GetContext(), enable, clear));
    });
    lua.set_function("sprinter_sound_ring", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::SprinterSoundRing(emulator->GetContext()));
    });
    lua.set_function("sprinter_ports", [this, sprinterQuery](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        DeviceState::SprinterPortQuery query;
        std::string error;
        if (!sprinterQuery(options, query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::SprinterPortTable(emulator->GetContext(), query)));
        return out;
    });
    // sprinter_port(0x21BC [, {rw="w"}]) or sprinter_port("21BC", ...)
    lua.set_function("sprinter_port", [this, sprinterQuery](sol::this_state s, sol::object portArg, sol::optional<sol::table> options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        uint16_t port = 0;
        if (portArg.get_type() == sol::type::number)
        {
            const long long value = portArg.as<long long>();
            if (value < 0 || value > 0xFFFF)
                return mouseError(s, "port must be 0-0xFFFF");
            port = static_cast<uint16_t>(value);
        }
        else if (portArg.get_type() != sol::type::string || !DeviceState::SprinterPortFromString(portArg.as<std::string>(), port))
            return mouseError(s, "port: a number or a hex string (\"21BC\", \"#21BC\")");
        DeviceState::SprinterPortQuery query;
        std::string error;
        if (!sprinterQuery(options, query, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::SprinterPortLookup(emulator->GetContext(), port, query)));
        return out;
    });
}
