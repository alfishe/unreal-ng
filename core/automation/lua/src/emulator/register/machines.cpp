// LuaEmulator bindings: instances, machines, ZX-Poly, memory and pages.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterMachines(sol::state& lua)
{
    // Register the Emulator class with Sol2 - extended bindings
    lua.new_usertype<Emulator>(
        "Emulator",
        // Lifecycle control
        "start", &Emulator::Start,
        "stop", &Emulator::Stop,
        "pause", [](Emulator& self) -> std::string {
            execution::script::Check(self, execution::Origin::Lua, false);   // a pause opens no session
            return execution::script::PauseStatus(
                self.Pause(execution::script::RequesterOf(self, execution::Origin::Lua), "lua"));
        },
        "resume", [](Emulator& self) {
            execution::script::Check(self, execution::Origin::Lua);
            self.Resume(execution::script::RequesterOf(self, execution::Origin::Lua));
        },
        // emu:reset() is soft (the reset button); emu:reset(true) or emu:reset{hard = true} is a power cycle
        "reset", [](Emulator& self, sol::variadic_args va) {
            bool hard = false;
            if (va.size() > 0)
            {
                const sol::object arg = va.get<sol::object>(0);
                if (arg.get_type() == sol::type::boolean)
                    hard = arg.as<bool>();
                else if (arg.get_type() == sol::type::table)
                    hard = arg.as<sol::table>().get_or("hard", false);
            }
            self.Reset(hard);
        },
        "request_nmi", &Emulator::RequestNMI,
        "request_mni", &Emulator::RequestMNI,
        // Front-panel switches (Profi "turbo"): get -> true/false, nil when absent; set -> true when done
        "get_switch", [](Emulator& self, const std::string& name) -> sol::optional<bool> {
            FrontPanelSwitch sw;
            if (!ParseFrontPanelSwitch(name, sw))
                return sol::nullopt;
            const int value = self.GetFrontPanelSwitch(sw);
            if (value < 0)
                return sol::nullopt;
            return value != 0;
        },
        "set_switch", [](Emulator& self, const std::string& name, bool on) {
            FrontPanelSwitch sw;
            return ParseFrontPanelSwitch(name, sw) && self.SetFrontPanelSwitch(sw, on);
        },
        
        // State queries
        "is_running", &Emulator::IsRunning,
        "is_paused", &Emulator::IsPaused,
        "get_id", &Emulator::GetId,
        "get_state", [](Emulator& emu) -> std::string {
            switch (emu.GetState()) {
                case StateRun: return "running";
                case StatePaused: return "paused";
                case StateStopped: return "stopped";
                case StateInitialized: return "initialized";
                case StateResumed: return "resumed";
                case StateRetired: return "retired";
                default: return "unknown";
            }
        },
        // RAM contents the machine was created with: "random" | "zero"
        "ram_power_on", [](Emulator& emu) -> std::string {
            EmulatorContext* context = emu.GetContext();
            return context ? Config::RamPowerOnName(context->config.ramPowerOn) : "";
        }
    );

    // EmulatorManager bindings for multi-instance support
    lua.set_function("emu_list", []() -> sol::as_table_t<std::vector<std::string>> {
        auto* mgr = EmulatorManager::GetInstance();
        return sol::as_table(mgr->GetEmulatorIds());
    });

    lua.set_function("emu_count", []() -> int {
        auto* mgr = EmulatorManager::GetInstance();
        return static_cast<int>(mgr->GetEmulatorIds().size());
    });

    // The machines grouped family -> machine -> RAM size (MachineCatalog, as GET /emulator/machines):
    // machines([all], [id]) -> {families = {...}, current = {...}?}
    lua.set_function("machines", [](sol::optional<bool> all, sol::optional<std::string> id, sol::this_state state) {
        std::string current;
        uint32_t ramKb = 0;
        if (id && !id->empty())
            if (auto emulator = EmulatorManager::GetInstance()->GetEmulator(*id))
            {
                current = MachineCatalog::NameOf(*emulator);
                ramKb = EmulatorManager::GetMachineIdentity(*emulator).RamKb;
            }
        return StateNodeToLua(state, all.value_or(false) ? MachineCatalog::ToState(MachineCatalog::All(), current, ramKb)
                                                         : MachineCatalog::ToState(MachineCatalog::Creatable(), current, ramKb));
    });

    // ZX-Poly machines (EmulatorManager::CreateZXPolyMachine - the entry point
    // every surface uses): four synchronized instances of one model.
    // zxpoly_start([model], [file], [ram_power_on]) -> master id, or nil + error.
    // ram_power_on: "random" | "zero" (RAM contents of all four modules;
    // default: the model's unreal.ini)
    lua.set_function("zxpoly_start", [](sol::optional<std::string> model, sol::optional<std::string> file,
                                        sol::optional<std::string> ramPowerOn,
                                        sol::this_state state) -> sol::variadic_results {
        sol::variadic_results results;
        auto* mgr = EmulatorManager::GetInstance();
        std::function<void(CONFIG&)> configOverride;
        if (ramPowerOn)
        {
            RamPowerOn mode = RamPowerOn::Random;
            if (!Config::ParseRamPowerOn(*ramPowerOn, mode))
            {
                results.push_back(sol::make_object(state, sol::lua_nil));
                results.push_back(sol::make_object(state, "ram_power_on must be random or zero"));
                return results;
            }
            configOverride = Config::RamPowerOnOverride(mode);
        }
        std::string error;
        auto master = mgr->CreateZXPolyMachine("", model.value_or("PENTAGON"), file.value_or(""), &error,
                                               configOverride);
        if (!master)
        {
            results.push_back(sol::make_object(state, sol::lua_nil));
            results.push_back(sol::make_object(state, "cannot start ZX-Poly: " + error));
            return results;
        }
        mgr->StartEmulatorAsync(master->GetId());
        mgr->SetSelectedEmulatorId(master->GetId());
        results.push_back(sol::make_object(state, master->GetId()));
        return results;
    });

    // A preset machine in one call (MachinePreset, core/src/emulator/preset/machinepreset.h):
    // machine_preset{model = "48K", cards = {"edge.1=kempston"}, features = {kempstonmouse = false}} -> the answer
    // table (id, model, ram, cards, features, muted, paused, fast_tape, control, position, text), or nil + the
    // refusal. Defaults: ram zero, muted, paused at power-on, fast_tape off, take_control (this
    // script's session, as take_control()), replace off. The new machine becomes the selected one
    lua.set_function("machine_preset", [](sol::table argsTable, sol::this_state state) -> sol::variadic_results {
        MachinePresetCaller caller;
        caller.surface = "lua";
        caller.takeControl = [](Emulator& emulator, const std::string& client, uint32_t, std::string&,
                                std::string& reason) {
            return execution::script::TakeControl(emulator, execution::Origin::Lua, client.empty() ? "lua" : client,
                                                  reason);
        };
        caller.holdsControl = [](Emulator& emulator) {
            const unreal::UUID owner = execution::script::detail::Owner(emulator, execution::Origin::Lua);
            return !owner.isNil() && emulator.GetContext() && emulator.GetContext()->HasRunControl(owner);
        };
        StateNode args = LuaToStateNode(argsTable);
        if (!args.isObject())
            args = StateNode::Object();   // an empty table reads as neither: the verb answers "model: required"
        const MachinePresetReply reply = MachinePreset::Execute(args, caller);
        sol::variadic_results results;
        if (!reply.Ok())
        {
            results.push_back(sol::make_object(state, sol::lua_nil));
            results.push_back(sol::make_object(state, reply.message));
            return results;
        }
        results.push_back(StateNodeToLua(state, reply.ToValue()));
        return results;
    });

    // zxpoly_status(id) -> table (nil if not a ZX-Poly machine)
    lua.set_function("zxpoly_status", [](const std::string& id, sol::this_state state) -> sol::object {
        ZXPolyGroup* group = EmulatorManager::GetInstance()->GetZXPolyGroup(id);
        if (!group)
            return sol::make_object(state, sol::lua_nil);
        const ZXPolyGroup::Status status = group->GetStatus();
        sol::state_view view(state);
        sol::table out = view.create_table();
        out["master_id"] = status.memberIds[0];
        out["locked"] = status.locked;
        out["slaves_running"] = status.slavesRunning;
        out["parallel_slaves"] = status.parallelSlaves;
        out["pipelined_slaves"] = status.pipelinedSlaves;
        out["port_3d00"] = status.port3D00;
        out["video_mode"] = status.videoMode;
        sol::table modules = view.create_table();
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        {
            sol::table module = view.create_table();
            module["module"] = m;
            module["id"] = status.memberIds[m];
            sol::table registers = view.create_table();
            for (size_t r = 0; r < 4; r++)
                registers[r + 1] = status.registers[m][r];
            module["registers"] = registers;
            modules[m + 1] = module;
        }
        out["modules"] = modules;
        out["diverged"] = status.divergence.diverged;
        out["divergence"] = status.divergence.what;
        return out;
    });

    lua.set_function("emu_get", [](const std::string& id) -> Emulator* {
        auto* mgr = EmulatorManager::GetInstance();
        auto emu = mgr->GetEmulator(id);
        return emu.get();
    });

    lua.set_function("emu_get_selected", []() -> Emulator* {
        auto* mgr = EmulatorManager::GetInstance();
        if (mgr) {
            std::string id = mgr->GetSelectedEmulatorId();
            if (!id.empty()) {
                return mgr->GetEmulator(id).get();
            }
        }
        return nullptr;
    });

    // Register access - requires emulator instance
    lua.set_function("get_pc", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->pc : 0;
    });

    lua.set_function("get_sp", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->sp : 0;
    });

    lua.set_function("get_af", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->af : 0;
    });

    lua.set_function("get_bc", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->bc : 0;
    });

    lua.set_function("get_de", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->de : 0;
    });

    lua.set_function("get_hl", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->hl : 0;
    });

    lua.set_function("get_ix", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->ix : 0;
    });

    lua.set_function("get_iy", [this]() -> uint16_t {
        if (!effectiveEmulator()) return 0;
        Z80State* z80 = effectiveEmulator()->GetZ80State();
        return z80 ? z80->iy : 0;
    });

    // get_registers([cpu]): "gs" / "ms" the sound card CPU's, the same keys with card_time for t (cpuview.h)
    lua.set_function("get_registers", [this](sol::this_state s, sol::optional<std::string> cpuName) -> sol::table {
        // Use the calling Lua state directly (safe even if _lua was never wired)
        sol::state_view lua_view(s);
        sol::table regs = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            for (const auto& [key, value] : CpuView::FlatRegisters(emulator->GetContext(), cpu).members)
                regs[key] = StateNodeToLua(s, value);
            return regs;
        }
        if (emulator) {
            Z80State* z80 = emulator->GetZ80State();
            if (z80) {
                regs["pc"] = z80->pc;
                regs["sp"] = z80->sp;
                regs["af"] = z80->af;
                regs["bc"] = z80->bc;
                regs["de"] = z80->de;
                regs["hl"] = z80->hl;
                regs["ix"] = z80->ix;
                regs["iy"] = z80->iy;
                regs["af_"] = z80->alt.af;
                regs["bc_"] = z80->alt.bc;
                regs["de_"] = z80->alt.de;
                regs["hl_"] = z80->alt.hl;
                regs["i"] = z80->i;
                regs["r"] = Z80::RegisterR(z80);
                regs["memptr"] = z80->memptr;
                regs["im"] = z80->im;
                regs["iff1"] = z80->iff1 != 0;
                regs["iff2"] = z80->iff2 != 0;
                regs["halted"] = z80->halted != 0;
                regs["q"] = z80->q;
                regs["boundary"] = Z80::BoundaryName(z80->boundary);
                regs["t"] = static_cast<uint32_t>(z80->t);  // CPU T-states since the frame's start
            }
        }
        return regs;
    });

    lua.set_function("get_register", [this](sol::this_state s, const std::string& name,
                                            sol::optional<std::string> cpuName) -> sol::object {
        sol::state_view lua_view(s);
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return sol::make_object(lua_view, sol::lua_nil);
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            uint16_t value = 0;
            return CpuView::GetRegister(emulator->GetContext(), cpu, name, value) ? sol::make_object(lua_view, value)
                                                                                 : sol::make_object(lua_view, sol::lua_nil);
        }
        Z80State* z80 = emulator->GetZ80State();
        if (!z80)
            return sol::make_object(lua_view, sol::lua_nil);
        uint16_t value = 0;
        bool is16bit = false;
        if (!Z80::GetRegisterValue(z80, name, value, is16bit))
            return sol::make_object(lua_view, sol::lua_nil);
        return sol::make_object(lua_view, value);
    });

    lua.set_function("set_register", [this](const std::string& name, uint16_t value,
                                            sol::optional<std::string> cpuName) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return false;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            // An unknown register or a value out of range is false, as for the main CPU; the past refuses
            const std::string refused = CpuView::SetRegister(emulator, cpu, "lua register", name, value);
            if (refused.rfind("unknown register", 0) == 0)
                return false;
            if (!refused.empty())
                throw std::runtime_error(refused);
            return true;
        }
        // A tool edit (TTD records it; in the recorded past it forks a branch); a refusal is a script error
        std::string refused;
        const bool set = emulator->SetRegisterFromTool("lua register", name, value, &refused);
        if (!refused.empty())
            throw std::runtime_error(refused);
        return set;
    });

    // Memory access: direct (non-mutating) reads so inspecting memory
    // never drives the ProfROM quadrant machine
    // mem_read(addr [, cpu]) / mem_write(addr, value [, cpu]): "gs" / "ms" the sound card CPU's view (cpuview.h)
    lua.set_function("mem_read", [this](uint16_t addr, sol::optional<std::string> cpuName) -> uint8_t {
        if (!effectiveEmulator()) return 0;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            std::vector<uint8_t> byte;
            CpuView::Read(effectiveEmulator()->GetContext(), cpu, addr, 1, byte);
            return byte.empty() ? 0xFF : byte[0];
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        return mem ? mem->DirectReadFromZ80Memory(addr) : 0;
    });

    lua.set_function("mem_write", [this](uint16_t addr, uint8_t value, sol::optional<std::string> cpuName) {
        if (!effectiveEmulator()) return;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            const std::string refused = CpuView::Write(effectiveEmulator(), cpu, "Lua memory write", addr, {value});
            if (!refused.empty())
                throw std::runtime_error(refused);
            return;
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return;
        effectiveEmulator()->EditMemoryFromToolOrThrow(Emulator::ToolEdit::Of("Lua memory write", "memory", addr, 1),
                                                       [&] { mem->ToolWriteToZ80Memory(addr, value); });
    });

    // mem_read_word / mem_write_word / mem_read_block / mem_hexdump / mem_write_block: `cpu` last ("gs" / "ms": the
    // sound card CPU's view, cpuview.h)
    lua.set_function("mem_read_word", [this](uint16_t addr, sol::optional<std::string> cpuName) -> uint16_t {
        if (!effectiveEmulator()) return 0;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            std::vector<uint8_t> bytes;
            CpuView::Read(effectiveEmulator()->GetContext(), cpu, addr, 2, bytes);
            return bytes.size() == 2 ? static_cast<uint16_t>(bytes[0] | (bytes[1] << 8)) : 0;
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return 0;
        return mem->DirectReadFromZ80Memory(addr) | (mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + 1)) << 8);
    });

    lua.set_function("mem_write_word", [this](uint16_t addr, uint16_t value, sol::optional<std::string> cpuName) {
        if (!effectiveEmulator()) return;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            const std::string refused =
                CpuView::Write(effectiveEmulator(), cpu, "Lua memory write", addr,
                               {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)});
            if (!refused.empty())
                throw std::runtime_error(refused);
            return;
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return;
        effectiveEmulator()->EditMemoryFromToolOrThrow(Emulator::ToolEdit::Of("Lua memory write", "memory", addr, 2), [&] {
            mem->ToolWriteToZ80Memory(addr, value & 0xFF);
            mem->ToolWriteToZ80Memory(static_cast<uint16_t>(addr + 1), (value >> 8) & 0xFF);
        });
    });

    // debug_snapshot{disasm = 21, stack = 8, memory = {"cpu:0x8000:256", "ram5:0:6912"}} - one coherent debugger
    // snapshot (core DebugSnapshot, the WebAPI GET /debug/snapshot): memory windows carry their bytes as Lua strings
    // (field bytes); nil, error when refused
    // Long-poll (debugger additions tdd §6): blocks until the snapshot's seq moves past `since` (default: the
    // current one) or timeout_ms (default 10000, at most 60000) -> {seq, changed, state, pause} | nil, error
    lua.set_function("debug_wait", [this](sol::this_state s, sol::optional<double> since, sol::optional<uint32_t> timeoutMs)
                                       -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        const uint64_t from = since ? static_cast<uint64_t>(*since) : emulator->DebugSeq();
        const uint32_t timeout = std::min<uint32_t>(timeoutMs.value_or(10000), 60000);
        return {StateNodeToLua(s, DebugSnapshot::Wait(emulator, from, timeout)), sol::make_object(s, sol::lua_nil)};
    });

    // PC history (debugger additions tdd §7): pc_history([depth]) -> {armed, started_now, total, capacity, entries
    // [{address, kind, page}]} | nil, error (the first call starts recording); pc_history_arm(on) -> true | nil, error
    lua.set_function("pc_history", [this](sol::this_state s, sol::optional<unsigned> depth) -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        const PcHistory::Result result = PcHistory::Report(emulator, depth.value_or(32));
        if (!result.error.empty())
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
        return {StateNodeToLua(s, result.report), sol::make_object(s, sol::lua_nil)};
    });
    lua.set_function("pc_history_arm", [this](sol::this_state s, bool on) -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        const std::string error = emulator ? PcHistory::SetArmed(emulator, on) : std::string("no emulator");
        if (!error.empty())
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, error)};
        return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
    });

    // The GS debugger's twin timeline (gs-debugger design §5.4): timeline([{cpu=, span=, recent=, lease_ms=}]) ->
    // {state, armed, cpu, lanes, now, position, window, recent, next, markers, density, text} | nil, error (takes
    // or renews the script's lease, 0 = until timeline_arm(false)); timeline_arm(on[, lease_ms]) -> {armed, ...}
    lua.set_function("timeline", [this](sol::this_state s, sol::optional<sol::table> opts) -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        timeline::Query query;
        query.client = "lua";
        query.leaseMs = 0;
        if (opts)
        {
            query.cpu = opts->get_or("cpu", std::string());
            query.spanT = opts->get_or("span", timeline::kDefaultSpanT);
            query.recent = opts->get_or("recent", 64u);
            query.leaseMs = opts->get_or("lease_ms", 0u);
        }
        const timeline::Reply reply = timeline::Read(emulator, query);
        if (reply.status != 200)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, reply.error)};
        return {StateNodeToLua(s, reply.body), sol::make_object(s, sol::lua_nil)};
    });
    lua.set_function("timeline_arm", [this](sol::this_state s, bool on, sol::optional<unsigned> leaseMs) -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        const timeline::Reply reply = timeline::SetArmed(emulator, "lua", on, leaseMs.value_or(0));
        if (reply.status != 200)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, reply.error)};
        return {StateNodeToLua(s, reply.body), sol::make_object(s, sol::lua_nil)};
    });

    // debug_snapshot(opts) -> snapshot | nil, error, code (debugerror.h)
    lua.set_function("debug_snapshot", [this](sol::this_state s, sol::optional<sol::table> opts)
                                           -> std::tuple<sol::object, sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator")),
                    sol::make_object(s, std::string(DebugError::kNotFound))};
        DebugSnapshot::Options options;
        options.rawBytes = true;
        if (opts)
        {
            // cpu = "gs" / "ms": the sound card CPU's snapshot (debug_cpu names it)
            options.cpu = ScriptCpu(opts->get<sol::optional<std::string>>("cpu"));
            options.disasm = std::min<unsigned>(opts->get_or("disasm", 0u), 100u);
            options.stack = opts->get_or("stack", 8u);
            options.pchist = opts->get_or("pchist", 0u);
            if (sol::optional<sol::table> windows = opts->get<sol::optional<sol::table>>("memory"))
                for (auto& pair : *windows)
                    if (pair.second.is<std::string>())
                        options.memory.push_back(pair.second.as<std::string>());
        }
        const DebugSnapshot::Result result = DebugSnapshot::Build(emulator, options);
        if (!result.error.empty())
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error),
                    sol::make_object(s, std::string(result.code))};
        return {StateNodeToLua(s, result.snapshot), sol::make_object(s, sol::lua_nil), sol::make_object(s, sol::lua_nil)};
    });

    // mem_read_bytes(addr, len [, space]) -> the bytes as a Lua string (MemoryRead): the CPU view (wraps at #FFFF,
    // up to 65536), a page "ram5" / "rom2" / "cache0" or "ram" (every RAM page back to back); nil, error on refusal
    lua.set_function("mem_read_bytes", [this](sol::this_state s, uint32_t addr, uint32_t len,
                                              sol::optional<std::string> space) -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        const MemoryRead::Result read = MemoryRead::Bytes(emulator->GetContext(), space.value_or(""), addr, len);
        if (!read.error.empty())
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, read.error)};
        return {sol::make_object(s, std::string(read.bytes.begin(), read.bytes.end())), sol::make_object(s, sol::lua_nil)};
    });

    // memory_counters{space=, start=, length=, addresses=, view=, kinds=, min=, limit=} -> {space, session,
    // allocated, reads, writes, executes, matched, truncated, cells = {{address, r, w, x}, ...}} | nil, error
    // (MemoryCounters: space cpu / phys / ram / ram5; view top (default) / sparse / dense; addresses a table or
    // "0x5C00,0x8000-0x80FF")
    lua.set_function("memory_counters", [this](sol::this_state s, sol::optional<sol::table> options)
                                            -> std::tuple<sol::object, sol::object> {
        sol::state_view view(s);
        auto fail = [&s](const std::string& error) -> std::tuple<sol::object, sol::object> {
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, error)};
        };
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return fail("no emulator");
        MemoryCounters::Request request;
        request.mode = MemoryCounters::Mode::Top;
        std::string error;
        if (options)
        {
            const sol::table& o = *options;
            request.space = o.get_or<std::string>("space", "");
            request.start = o.get_or<uint32_t>("start", 0);
            request.length = o.get_or<uint32_t>("length", 0);
            request.min = o.get_or<uint32_t>("min", 1);
            request.limit = o.get_or<uint32_t>("limit", 4096);
            if (!MemoryCounters::ParseMode(o.get_or<std::string>("view", "top"), request.mode, error) ||
                !MemoryCounters::ParseKinds(o.get_or<std::string>("kinds", "rwx"), request.kinds, error))
                return fail(error);
            const sol::object addresses = o["addresses"];
            if (addresses.is<std::string>())
            {
                if (!MemoryCounters::ParseAddresses(addresses.as<std::string>(), request.addresses, error))
                    return fail(error);
            }
            else if (addresses.is<sol::table>())
            {
                for (const auto& [key, value] : addresses.as<sol::table>())
                    request.addresses.push_back(value.as<uint32_t>());
            }
        }
        else
            request.limit = 4096;
        const MemoryCounters::Result result = MemoryCounters::Read(emulator->GetContext(), request);
        if (!result.error.empty())
            return fail(result.error);
        sol::table table = view.create_table();
        table["space"] = result.space;
        table["space_size"] = result.spaceSize;
        table["session"] = result.session;
        table["allocated"] = result.allocated;
        table["start"] = result.start;
        table["length"] = result.length;
        table["reads"] = result.reads;
        table["writes"] = result.writes;
        table["executes"] = result.executes;
        table["matched"] = result.matched;
        table["truncated"] = result.truncated;
        sol::table cells = view.create_table(static_cast<int>(result.cells.size()), 0);
        for (size_t i = 0; i < result.cells.size(); i++)
        {
            const MemoryCounters::Cell& cell = result.cells[i];
            cells[i + 1] = view.create_table_with(1, cell.address, 2, cell.reads, 3, cell.writes, 4, cell.executes);
        }
        table["cells"] = cells;
        return {sol::make_object(s, table), sol::make_object(s, sol::lua_nil)};
    });

    // memory_counter(address [, space]) -> reads, writes, executes | nil, error
    lua.set_function("memory_counter", [this](sol::this_state s, uint32_t address, sol::optional<std::string> space)
                                           -> std::tuple<sol::object, sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator")), sol::make_object(s, sol::lua_nil)};
        const MemoryCounters::Result result = MemoryCounters::ReadCell(emulator->GetContext(), space.value_or(""), address);
        if (!result.error.empty())
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error), sol::make_object(s, sol::lua_nil)};
        const MemoryCounters::Cell& cell = result.cells[0];
        return {sol::make_object(s, cell.reads), sol::make_object(s, cell.writes), sol::make_object(s, cell.executes)};
    });

    // Memory watches (MemoryWatch): memory_watch_add(name, start, length [, {callers=, values=}]),
    // memory_watch_port(name, port [, mask [, {callers=, values=}]]), memory_watch_remove(name), memory_watch_clear()
    // -> true | nil, error; memory_watches() -> {{name, kind, start, length, port, mask, reads, writes, executes,
    // callers = {[pc] = count}, values = {[value] = count}}, ...} | nil, error
    auto watchOptions = [](const sol::optional<sol::table>& table) {
        MemoryWatch::Options options;
        if (table)
        {
            options.callers = table->get_or("callers", false);
            options.values = table->get_or("values", false);
        }
        return options;
    };
    auto watchReply = [](sol::this_state s, const MemoryWatch::Result& result) -> std::tuple<sol::object, sol::object> {
        if (!result.ok)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
        return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
    };
    lua.set_function("memory_watch_add", [this, watchOptions, watchReply](sol::this_state s, const std::string& name, uint32_t start,
                                                                         uint32_t length, sol::optional<sol::table> options) {
        return watchReply(s, MemoryWatch::AddRegion(effectiveEmulator(), name, start, length, watchOptions(options)));
    });
    lua.set_function("memory_watch_port", [this, watchOptions, watchReply](sol::this_state s, const std::string& name, uint32_t port,
                                                                          sol::optional<uint32_t> mask, sol::optional<sol::table> options) {
        return watchReply(s, MemoryWatch::AddPort(effectiveEmulator(), name, port, mask.value_or(0xFFFF), watchOptions(options)));
    });
    lua.set_function("memory_watch_remove", [this, watchReply](sol::this_state s, const std::string& name) {
        return watchReply(s, MemoryWatch::Remove(effectiveEmulator(), name));
    });
    lua.set_function("memory_watch_clear", [this, watchReply](sol::this_state s) {
        return watchReply(s, MemoryWatch::Clear(effectiveEmulator()));
    });
    lua.set_function("memory_watches", [this](sol::this_state s) -> std::tuple<sol::object, sol::object> {
        const MemoryWatch::Result result = MemoryWatch::List(effectiveEmulator());
        if (!result.ok)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
        sol::state_view view(s);
        sol::table list = view.create_table();
        int index = 1;
        for (const MemoryWatch::Watch& watch : result.watches)
        {
            sol::table entry = view.create_table();
            entry["name"] = watch.name;
            entry["kind"] = watch.port ? "port" : "region";
            if (watch.port)
            {
                entry["port"] = watch.start;
                entry["mask"] = watch.mask;
            }
            else
            {
                entry["start"] = watch.start;
                entry["length"] = watch.length;
                entry["executes"] = watch.executes;
            }
            entry["reads"] = watch.reads;
            entry["writes"] = watch.writes;
            sol::table callers = view.create_table();
            for (const auto& [pc, count] : watch.callers)
                callers[pc] = count;
            sol::table values = view.create_table();
            for (const auto& [value, count] : watch.values)
                values[static_cast<int>(value)] = count;
            entry["callers"] = callers;
            entry["values"] = values;
            list[index++] = entry;
        }
        return {sol::make_object(s, list), sol::make_object(s, sol::lua_nil)};
    });

    // A debugger's port write through the decoder (PortWrite): port_out(0x13AF, 0x20) -> true | nil, error
    lua.set_function("port_out", [this](sol::this_state s, int64_t port, int64_t value) -> std::tuple<sol::object, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        uint16_t p = 0;
        uint8_t v = 0;
        std::string error;
        if (port < 0 || value < 0 || !PortWrite::Parse(std::to_string(port), std::to_string(value), p, v, error))
            return {sol::make_object(s, sol::lua_nil),
                    sol::make_object(s, error.empty() ? std::string("port and value must not be negative") : error)};
        const PortWrite::Result result = PortWrite::Write(emulator, p, v, "lua");
        if (!result.ok)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
        return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
    });

    lua.set_function("mem_read_block", [this](uint16_t addr, uint16_t len, sol::optional<std::string> cpuName) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table data = lua_view.create_table();
        if (!effectiveEmulator()) return data;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            std::vector<uint8_t> bytes;
            CpuView::Read(effectiveEmulator()->GetContext(), cpu, addr, len, bytes);
            for (size_t i = 0; i < bytes.size(); i++)
                data[i + 1] = bytes[i];
            return data;
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return data;
        for (uint16_t i = 0; i < len; i++) {
            data[i + 1] = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i));
        }
        return data;
    });

    // TD-3 Phase 1: sparse non-zero block overview (same core source as
    // GET /memory/map and MCP inspect_state 'memory_map').
    // memory_map() | memory_map("ram") | memory_map("address", 64, 48)
    lua.set_function("memory_map", [this](sol::this_state s, sol::optional<std::string> viewName, sol::optional<int> minRunOpt,
                                          sol::optional<int> maxBlocksOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        if (!effectiveEmulator()) return result;
        // memory_map("gs") / ("ms"): the sound card CPU's four windows {cpu, windows = {{start, size, kind, page,
        // offset}, ...}} (cpuview.h)
        if (viewName && (*viewName == "gs" || *viewName == "ms"))
        {
            const DebugCpuId cpu = ScriptCpu(viewName);
            return StateNodeToLua(s, CpuView::Windows(effectiveEmulator()->GetContext(), cpu)).as<sol::table>();
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        EmulatorContext* ctx = effectiveEmulator()->GetContext();
        if (!mem || !ctx) return result;

        const MemoryMapView view = (viewName && (*viewName == "ram" || *viewName == "pages"))
                                       ? MemoryMapView::RamPages
                                       : MemoryMapView::AddressSpace;
        const uint32_t minRun = (minRunOpt && *minRunOpt > 0) ? static_cast<uint32_t>(*minRunOpt) : kMemoryMapDefaultMinRun;
        const uint32_t maxBlocks = (maxBlocksOpt && *maxBlocksOpt > 0) ? static_cast<uint32_t>(*maxBlocksOpt)
                                                                       : kMemoryMapDefaultMaxBlocks;

        const MemoryMapReport report = BuildMemoryMap(*mem, ctx->config, view, minRun, maxBlocks);
        result["model"] = report.model;
        result["view"] = report.ramView ? "ram" : "address";
        result["total_size"] = report.totalSize;
        result["non_zero_bytes"] = report.nonZeroBytes;
        result["min_run"] = report.minRun;
        result["truncated"] = report.truncated;

        sol::table blocks = lua_view.create_table();
        for (size_t i = 0; i < report.blocks.size(); i++) {
            const MemoryMapBlock& block = report.blocks[i];
            sol::table item = lua_view.create_table();
            item["address"] = block.address;
            item["size"] = block.size;
            item["type"] = block.typeName;
            item["bank"] = block.bank == 0xFF ? -1 : static_cast<int>(block.bank);
            item["page"] = block.page;
            item["rom"] = block.isRom;
            item["status"] = block.IsZeroFill() ? "zeros" : "data";
            item["non_zero"] = block.nonZero;
            if (!block.IsZeroFill())
                item["hash"] = block.hash;  // FNV-1a 64 fingerprint (lua_Integer)
            blocks[i + 1] = item;
        }
        result["blocks"] = blocks;
        return result;
    });

    // TD-3 compact read format: classic 16B/line hexdump + ASCII sidebar
    lua.set_function("mem_hexdump", [this](uint16_t addr, sol::optional<int> lenOpt,
                                           sol::optional<std::string> cpuName) -> std::string {
        if (!effectiveEmulator()) return "";
        const DebugCpuId cpu = ScriptCpu(cpuName);
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return "";
        const size_t len = lenOpt ? static_cast<size_t>(*lenOpt) : 64;
        if (len < 1 || len > 4096) return "";
        if (IsCardCpu(cpu))
        {
            std::vector<uint8_t> bytes;
            CpuView::Read(effectiveEmulator()->GetContext(), cpu, addr, static_cast<uint32_t>(len), bytes);
            return FormatHexDump(bytes.data(), bytes.size(), addr);
        }
        std::vector<uint8_t> buffer(len);
        for (size_t i = 0; i < len; i++)
            buffer[i] = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i));
        return FormatHexDump(buffer.data(), buffer.size(), addr);
    });

    lua.set_function("mem_write_block", [this](uint16_t addr, sol::table data, sol::optional<std::string> cpuName) {
        if (!effectiveEmulator()) return;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            std::vector<uint8_t> bytes;
            for (auto& pair : data)
                bytes.push_back(pair.second.as<uint8_t>());
            const std::string refused = CpuView::Write(effectiveEmulator(), cpu, "Lua memory write", addr, bytes);
            if (!refused.empty())
                throw std::runtime_error(refused);
            return;
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return;
        effectiveEmulator()->EditMemoryFromToolOrThrow(Emulator::ToolEdit::Of("Lua memory write", "memory", addr, data.size()), [&] {
            for (auto& pair : data) {
                int idx = pair.first.as<int>() - 1;  // Lua tables start at 1
                uint8_t val = pair.second.as<uint8_t>();
                mem->ToolWriteToZ80Memory(static_cast<uint16_t>((addr + idx) & 0xFFFF), val);
            }
        });
    });

    // Physical page access (ram/rom/cache/misc); `cpu` last: "gs" / "ms" read the sound card CPU's own pages (rom,
    // ram: CpuView::PageRead), a card page is not written (the card CPU's view is: mem_write)
    lua.set_function("page_read", [this](const std::string& type, int page, int offset,
                                         sol::optional<std::string> cpuName) -> int {
        if (!effectiveEmulator()) return 0;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
            return CardPageBytes(cpu, type, page, offset, 1).at(0);
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return 0;
        ValidatePageIndex("page_read", type, page, offset);
        uint8_t* pagePtr = nullptr;
        if (type == "ram")
            pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
        else if (type == "rom")
            pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
        else if (type == "cache")
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
        else
            pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
        return pagePtr[offset];
    });

    lua.set_function("page_write", [this](const std::string& type, int page, int offset, uint8_t value,
                                          sol::optional<std::string> cpuName) {
        if (!effectiveEmulator()) return;
        if (IsCardCpu(ScriptCpu(cpuName)))
            throw std::runtime_error(DebugError::WithCode(CpuView::kCardPageWriteRefusal, DebugError::kNotSupported));
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return;
        ValidatePageIndex("page_write", type, page, offset);
        uint8_t* pagePtr = nullptr;
        if (type == "ram")
            pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
        else if (type == "rom")
            pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
        else if (type == "cache")
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
        else
            pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
        Emulator::ToolEdit what = Emulator::ToolEdit::Of("Lua page write", "page", static_cast<int32_t>(offset), 1);
        what.page = type + std::to_string(page);
        effectiveEmulator()->EditMemoryFromToolOrThrow(what, [&] {
            pagePtr[offset] = value;
            if (type == "ram")
                mem->MarkRamPageEdited(static_cast<uint16_t>(page));
        });
    });

    lua.set_function("page_read_block", [this](const std::string& type, int page, int offset, int len,
                                               sol::optional<std::string> cpuName) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table data = lua_view.create_table();
        if (!effectiveEmulator()) return data;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
        {
            const std::vector<uint8_t> bytes = CardPageBytes(cpu, type, page, offset, std::max(len, 0));
            for (size_t i = 0; i < bytes.size(); i++)
                data[i + 1] = bytes[i];
            return data;
        }
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return data;
        ValidatePageIndex("page_read_block", type, page, offset);
        if (len < 0)
            throw std::invalid_argument("page_read_block: len must be >= 0");
        uint8_t* pagePtr = nullptr;
        if (type == "ram")
            pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
        else if (type == "rom")
            pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
        else if (type == "cache")
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
        else
            pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
        if (len > PAGE_SIZE - offset) len = PAGE_SIZE - offset;
        for (int i = 0; i < len; i++) {
            data[i + 1] = pagePtr[offset + i];
        }
        return data;
    });

    lua.set_function("page_write_block", [this](const std::string& type, int page, int offset, sol::table data,
                                                sol::optional<std::string> cpuName) {
        if (!effectiveEmulator()) return;
        if (IsCardCpu(ScriptCpu(cpuName)))
            throw std::runtime_error(DebugError::WithCode(CpuView::kCardPageWriteRefusal, DebugError::kNotSupported));
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return;
        ValidatePageIndex("page_write_block", type, page, offset);
        uint8_t* pagePtr = nullptr;
        if (type == "ram")
            pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
        else if (type == "rom")
            pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
        else if (type == "cache")
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
        else
            pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
        int maxLen = PAGE_SIZE - offset;
        Emulator::ToolEdit what = Emulator::ToolEdit::Of("Lua page write", "page", static_cast<int32_t>(offset), data.size());
        what.page = type + std::to_string(page);
        effectiveEmulator()->EditMemoryFromToolOrThrow(what, [&] {
            int idx = 0;
            for (auto& pair : data) {
                if (idx >= maxLen) break;
                uint8_t val = pair.second.as<uint8_t>();
                pagePtr[offset + idx] = val;
                idx++;
            }
            if (type == "ram")
                mem->MarkRamPageEdited(static_cast<uint16_t>(page));
        });
    });

    // Device memory regions (emulator/memory/devicememory.h): the Sprinter's video RAM "vram".
    // memory_regions() -> {available, regions = {...}}; region_read(name, offset, len) -> table of bytes
    // (nil, error on a bad range); region_write(name, offset, {bytes} | "hex") -> true | nil, error;
    // region_save(name, path [, offset, len]) / region_load(name, path [, offset]) -> true | nil, error
    // framebuffer([format]): the picture as raw pixels (FramebufferExport): {width, height, format, encoding,
    // data = a string of bytes}; "rgba" (default) or "index" (the Sprinter's pens); nil, error otherwise
    lua.set_function("framebuffer", [this](sol::this_state s, sol::optional<std::string> format) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        FramebufferExport::Frame frame;
        std::string error;
        if (!FramebufferExport::Capture(emulator->GetContext(), format.value_or("rgba"), frame, error))
            return mouseError(s, error);
        sol::state_view view(s);
        sol::table t = view.create_table();
        t["width"] = frame.width;
        t["height"] = frame.height;
        t["format"] = frame.format;
        t["encoding"] = frame.encoding;
        t["data"] = std::string(frame.bytes.begin(), frame.bytes.end());
        sol::variadic_results out;
        out.push_back(t);
        return out;
    });
}
