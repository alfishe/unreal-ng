// LuaEmulator bindings: disks, execution steps, pacing and tapes.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterDiskAndTape(sol::state& lua)
{
    lua.set_function("disk_get_path", [this](int drive) -> std::string {
        if (!effectiveEmulator() || drive < 0 || drive > 3) return "";
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx) return "";
        return ctx->coreState.diskFilePaths[drive];
    });

    lua.set_function("disk_eject", [this](int drive) -> bool {
        if (!effectiveEmulator() || drive < 0 || drive > 3) return false;
        return effectiveEmulator()->EjectDisk(static_cast<uint8_t>(drive), /*force*/ true);
    });

    // disk_load(path [, drive=0] [, autostart=false] [, basic=""]) - insert a disk image (.trd/.scl/.fdi/.udi/...)
    // into the requested drive (0-3 / A-D). autostart quick-resets into TR-DOS and runs the disk,
    // matching the Qt UI's drag-and-drop autostart and the WebAPI's "autostart" insert flag/CLI's
    // "disk insert <drive> <file> autostart" - drive A (0) only, a TR-DOS/Beta 128 hardware
    // convention: requesting it for another drive is a hard failure (result.success=false,
    // result.message explains why), not silently ignored or redirected to drive A. An .mgt on the MGT +D runs
    // its plan; basic "48" / "128" picks the BASIC of its reset ("" = the machine's own, "128" refused on a 48K)
    lua.set_function("disk_load", [this](const std::string& path, sol::optional<int> driveOpt,
                                          sol::optional<bool> autostartOpt,
                                          sol::optional<std::string> basicOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            result["success"] = false;
            result["message"] = "no emulator";
            return result;
        }

        int drive = driveOpt.value_or(0);
        bool autostart = autostartOpt.value_or(false);
        if (drive < 0 || drive > 3)
        {
            result["success"] = false;
            result["message"] = "invalid drive " + std::to_string(drive) + " (valid range: 0-3 / A-D)";
            return result;
        }

        if (autostart)
        {
            Emulator::DiskAutostartResult r =
                emulator->AutostartDisk(path, static_cast<uint8_t>(drive), basicOpt.value_or(""));
            result["success"] = r.mounted;
            result["started"] = r.started;
            result["ttd_session_ended"] = r.ttdSessionEnded;
            result["message"] = r.message;
            if (!r.plan.empty())
            {
                // An MGT disk on the +D: the steps go on after the call (state fdc -> autostart)
                result["plan"] = r.plan;
                result["command"] = r.command;
                result["dos"] = r.dos;
                result["basic"] = r.basic;
            }
        }
        else
        {
            std::string error;
            bool ok = emulator->LoadDisk(path, static_cast<uint8_t>(drive), &error);
            result["success"] = ok;
            result["started"] = false;
            result["message"] = ok ? "mounted" : error;
        }
        return result;
    });

    // disk_create(drive [, cylinders [, sides [, format]]]): format auto (plus3 on a +3, unformatted
    // elsewhere), unformatted or plus3; cylinders / sides 0 or left out = the format's geometry
    lua.set_function("disk_create", [this](int drive, sol::optional<int> cyl, sol::optional<int> sides,
                                           sol::optional<std::string> format) -> std::tuple<bool, std::string> {
        if (!effectiveEmulator()) return {false, "no emulator"};
        if (drive < 0 || drive > 3) return {false, "invalid drive (valid range: 0-3)"};
        Emulator::BlankDiskFormat parsed = Emulator::BlankDiskFormat::Auto;
        if (!Emulator::ParseBlankDiskFormat(format.value_or("auto"), parsed)) return {false, "unknown disk format"};
        const int cylinders = cyl.value_or(0);
        const int numSides = sides.value_or(0);
        if (cylinders < 0 || cylinders > 255 || numSides < 0 || numSides > 255)
            return {false, std::string("expected ") + BlankFloppySpec::GeometryHelp};
        std::string error;  // carries the TTD refusal while recording
        const bool ok = effectiveEmulator()->CreateBlankDisk(static_cast<uint8_t>(drive), parsed,
                                                             static_cast<uint8_t>(cylinders),
                                                             static_cast<uint8_t>(numSides), &error);
        return {ok, ok ? std::string() : error};
    });

    lua.set_function("disk_list", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table drives = lua_view.create_table();
        if (effectiveEmulator()) {
            auto* ctx = effectiveEmulator()->GetContext();
            if (ctx) {
                for (int i = 0; i < 4; i++) {
                    sol::table drive = lua_view.create_table();
                    drive["id"] = i;
                    drive["letter"] = std::string(1, 'A' + i);
                    drive["inserted"] = ctx->coreState.diskDrives[i] && 
                                       ctx->coreState.diskDrives[i]->isDiskInserted();
                    drive["path"] = ctx->coreState.diskFilePaths[i];
                    drives[i + 1] = drive;
                }
            }
        }
        return drives;
    });

    // Execution control
    // step / steps return how the run ended: {executed, stopped, breakpoint_id, address, access}
    // (stopped: a breakpoint ended it - an execution one before its instruction)
    auto stepOutcome = [](sol::state_view state, Emulator& emulator, unsigned executed) {
        const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
        sol::table result = state.create_table();
        result["executed"] = executed;
        result["stopped"] = stop.hit;
        if (stop.hit)
        {
            result["breakpoint_id"] = stop.breakpointId;
            result["address"] = stop.address;
            result["access"] = BreakpointHitKindName(stop.kind);
        }
        return result;
    };

    // Automation session (execution arbiter design §6a): the GUI shows it and locks its controls; when the user
    // takes control back, control calls raise an error (revoked_by_user) - stop, do not retry, ask the user
    lua.set_function("take_control", [this](sol::optional<std::string> client) {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            throw std::runtime_error("take_control: no emulator");
        std::string reason;
        if (!execution::script::TakeControl(*emulator, execution::Origin::Lua, client.value_or("lua"), reason))
            throw std::runtime_error("take_control refused: " + reason + " (do not retry)");
    });
    lua.set_function("release_control", [this]() {
        if (Emulator* emulator = effectiveEmulator())
            execution::script::ReleaseControl(*emulator, execution::Origin::Lua);
    });
    // Companion clients (docs/inprogress/2026-10-16-companion-client): devices the user paired at the GUI to act
    // as the user's hands. A script lists and revokes them; it never pairs, approves or acts as one
    lua.set_function("companions", [](sol::this_state s) -> sol::object {
        return StateNodeToLua(s, companion::CompanionRegistry::Instance().Report());
    });
    lua.set_function("companion_revoke", [](const std::string& idOrName) -> size_t {
        return companion::CompanionRegistry::Instance().Revoke(idOrName);
    });
    // The GUI control plane's methods the program registered (gui-control-plane design §4.10), as `unreal-gui`,
    // the CLI `gui`, WebAPI /api/v1/gui: gui_list(), gui_catalogue(), gui_state(target), gui_call(method, [params],
    // [target]). Each answers the plane's result {ok, body} or {ok = false, error, message}; waits up to 15 s
    lua.set_function("gui_list", [](sol::this_state s) -> sol::object {
        return StateNodeToLua(s, gui_call::CallAndWait("list", {}, StateNode::Object()));
    });
    lua.set_function("gui_catalogue", [](sol::this_state s) -> sol::object {
        StateNode catalogue;
        if (!StateNodeFromJsonText(GuiPlane::Catalogue(), catalogue))
            return StateNodeToLua(s, gui_call::Parse(GuiPlane::Failure(GuiPlane::kNoGui, "no GUI catalogue")));
        return StateNodeToLua(s, catalogue);
    });
    lua.set_function("gui_state", [](sol::this_state s, const std::string& target) -> sol::object {
        StateNode params = StateNode::Object();
        params["target"] = target;
        return StateNodeToLua(s, gui_call::CallAndWait("state", {}, params));
    });
    lua.set_function("gui_call", [](sol::this_state s, const std::string& method, sol::optional<sol::object> params,
                                    sol::optional<std::string> target) -> sol::object {
        StateNode node = params && params->get_type() == sol::type::table ? LuaToStateNode(*params) : StateNode::Object();
        if (!node.isObject())
            node = StateNode::Object();
        return StateNodeToLua(s, gui_call::CallAndWait(method, target.value_or(std::string()), node));
    });
    // step([skip_bp], [cpu]), steps(count, [skip_bp], [cpu]), stepover([cpu]), step_out([cpu]): "gs" / "ms" steps
    // the sound card CPU (CardStep; step("gs") too)
    lua.set_function("step", [this, stepOutcome](sol::object first, sol::optional<std::string> cpuName,
                                                 sol::this_state ts) -> sol::object {
        if (!effectiveEmulator()) return sol::lua_nil;
        if (first.is<std::string>() && !cpuName)
            cpuName = first.as<std::string>();
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
            return CardStep(sol::state_view(ts), cpu, "in", 1);
        const sol::optional<bool> skipBP =
            first.is<bool>() ? sol::optional<bool>(first.as<bool>()) : sol::optional<bool>();
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        Emulator& emulator = *effectiveEmulator();
        emulator.RunSingleCPUCycle(skipBP.value_or(true));
        const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
        const unsigned executed = (stop.hit && stop.kind == BreakpointHitKind::Execute) ? 0 : 1;
        return stepOutcome(sol::state_view(ts), emulator, executed);
    });

    lua.set_function("steps", [this, stepOutcome](unsigned count, sol::object second,
                                                  sol::optional<std::string> cpuName, sol::this_state ts) -> sol::object {
        if (!effectiveEmulator()) return sol::lua_nil;
        if (second.is<std::string>() && !cpuName)
            cpuName = second.as<std::string>();
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
            return CardStep(sol::state_view(ts), cpu, "in", count);
        const sol::optional<bool> skipBP =
            second.is<bool>() ? sol::optional<bool>(second.as<bool>()) : sol::optional<bool>();
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        Emulator& emulator = *effectiveEmulator();
        const unsigned executed = emulator.RunNCPUCycles(count, skipBP.value_or(false));
        return stepOutcome(sol::state_view(ts), emulator, executed);
    });

    lua.set_function("stepover", [this](sol::optional<std::string> cpuName, sol::this_state ts) -> sol::object {
        if (!effectiveEmulator()) return sol::lua_nil;
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
            return CardStep(sol::state_view(ts), cpu, "over", 1);
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        effectiveEmulator()->StepOver();
        return sol::lua_nil;
    });

    // select_cpu(cpu) / selected_cpu(): the CPU the front-ends show (CpuView::SetSelected), {cpu, follows}; a card
    // shown follows the main CPU's steps of the paused machine in tight mode
    lua.set_function("select_cpu", [this](const std::string& cpuName, sol::this_state ts) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::lua_nil;
        if (const std::string error = CpuView::SetSelected(emulator->GetContext(), cpuName); !error.empty())
            throw std::runtime_error(error);
        return StateNodeToLua(ts, CpuView::Selected(emulator->GetContext()));
    });
    // debug_targets(): the CPUs a debugger can name now {selected, targets = {{cpu, available, card}, ...}}
    lua.set_function("debug_targets", [this](sol::this_state ts) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::lua_nil;
        return StateNodeToLua(ts, CpuView::Targets(emulator->GetContext()));
    });
    lua.set_function("selected_cpu", [this](sol::this_state ts) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::lua_nil;
        return StateNodeToLua(ts, CpuView::Selected(emulator->GetContext()));
    });

    // Frame stepping methods
    lua.set_function("run_frame", [this](sol::optional<bool> skipBP) {
        if (!effectiveEmulator()) return;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        effectiveEmulator()->RunFrame(skipBP.value_or(true));
    });

    // run_frames(n [, skip_breakpoints = true]) -> {count, frames_run, tstates_run, frame, t, stop, message}: on the
    // direct-run timeline (split-independent: 30 + 70 == 100), any count
    lua.set_function("run_frames", [this](unsigned count, sol::optional<bool> skipBP, sol::this_state ts) -> sol::object {
        if (!effectiveEmulator()) return sol::lua_nil;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        Emulator& emulator = *effectiveEmulator();
        const Emulator::DirectRunResult r = emulator.RunNFrames(count, skipBP.value_or(true));
        return StateNodeToLua(ts, execution::RunOutcomeNode(
                                      emulator, execution::RunOutcomeOf("frames", count, r.frames, r.tstates, r.reached)));
    });

    // Frame watches (docs/inprogress/2026-11-03-run-until-watches): run_until{text = "PRESS", max_frames = 500}
    // -> {ok, met, outcome, frames, frame, hit = {reasons, text_line?}, text, ...}; watch_add{if = "...", then =
    // "notify"} -> {ok, id, watch}; watches() -> {count, watches}; watch_remove(id | "all")
    lua.set_function("run_until", [this](sol::this_state ts, sol::optional<sol::table> opts) {
        return RunUntil(sol::state_view(ts), opts);
    });
    lua.set_function("watch_add", [this](sol::this_state ts, sol::optional<sol::table> opts) {
        Emulator* emulator = effectiveEmulator();
        framewatch::Outcome outcome;
        if (emulator)
            outcome = framewatch::AddWatch(emulator->GetContext(), WatchOptions(opts));
        else
        {
            outcome.status = 503;
            outcome.text = "no emulator";
        }
        return WatchOutcome(sol::state_view(ts), outcome);
    });
    lua.set_function("watches", [this](sol::this_state ts) {
        Emulator* emulator = effectiveEmulator();
        framewatch::Outcome outcome = emulator ? framewatch::ListWatches(emulator->GetContext()) : framewatch::Outcome{};
        return WatchOutcome(sol::state_view(ts), outcome);
    });
    lua.set_function("watch_remove", [this](sol::this_state ts, sol::object id) {
        Emulator* emulator = effectiveEmulator();
        framewatch::Outcome outcome;
        if (emulator)
            outcome = framewatch::RemoveWatch(emulator->GetContext(), TtdOptionText(id));
        else
        {
            outcome.status = 503;
            outcome.text = "no emulator";
        }
        return WatchOutcome(sol::state_view(ts), outcome);
    });

    // Atomic stepping methods
    // run_tstates(n [, skip_breakpoints = true]) -> the run_frames answer; on the same timeline (slices end where
    // one run ends)
    lua.set_function("run_tstates", [this](uint64_t count, sol::optional<bool> skipBP, sol::this_state ts) -> sol::object {
        if (!effectiveEmulator()) return sol::lua_nil;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        Emulator& emulator = *effectiveEmulator();
        const Emulator::DirectRunResult r = emulator.RunTStates(count, skipBP.value_or(true), /*onTimeline=*/true);
        return StateNodeToLua(ts, execution::RunOutcomeNode(
                                      emulator, execution::RunOutcomeOf("tstates", count, r.frames, r.tstates, r.reached)));
    });

    lua.set_function("run_to_scanline", [this](unsigned scanline, sol::optional<bool> skipBP) {
        if (!effectiveEmulator()) return;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        effectiveEmulator()->RunUntilScanline(scanline, skipBP.value_or(true));
    });

    lua.set_function("run_scanlines", [this](unsigned count, sol::optional<bool> skipBP) {
        if (!effectiveEmulator()) return;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        effectiveEmulator()->RunNScanlines(count, skipBP.value_or(true));
    });

    lua.set_function("run_to_pixel", [this](sol::optional<bool> skipBP) {
        if (!effectiveEmulator()) return;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        effectiveEmulator()->RunUntilNextScreenPixel(skipBP.value_or(true));
    });

    // The paced run (core execution::RunPacer; the same pacer as WebAPI /run_pace, the actions run.pace_* and the
    // Qt speed control): pace() -> {preset, label, unit, interval_ms, running, stop_reason, presets}; the others
    // answer the reply as a table with ok (false: error = the message) and the pace
    auto paceReply = [](sol::this_state ts, const execution::RunReply& reply) -> sol::object {
        StateNode value = reply.body;
        value["ok"] = reply.Ok();
        if (!reply.Ok())
            value["error"] = reply.message;
        return StateNodeToLua(ts, value);
    };
    auto pacerOf = [this]() -> std::shared_ptr<execution::RunPacer> {
        Emulator* emulator = effectiveEmulator();
        return emulator ? execution::RunPacer::For(EmulatorManager::GetInstance()->GetEmulator(emulator->GetId()))
                        : nullptr;
    };
    lua.set_function("pace", [this](sol::this_state ts) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return sol::lua_nil;
        const auto pacer = execution::RunPacer::Find(emulator->GetId());
        return StateNodeToLua(ts, pacer ? pacer->StateBody(true) : execution::RunPacer::DefaultStateBody(true));
    });
    // pace_set(preset [, interval_ms]): full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line, 1op
    lua.set_function("pace_set", [this, paceReply, pacerOf](sol::this_state ts, const std::string& name,
                                                            sol::optional<int> intervalMs) -> sol::object {
        const auto pacer = pacerOf();
        if (!pacer)
            return sol::lua_nil;
        size_t index = 0;
        if (!execution::RunPacer::PresetIndex(name, index))
            throw std::runtime_error("pace_set: preset full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line or 1op");
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        return paceReply(ts, pacer->Set(execution::script::RequesterOf(*effectiveEmulator(), execution::Origin::Lua),
                                        index, intervalMs ? std::optional<int>(*intervalMs) : std::nullopt));
    });
    lua.set_function("pace_interval", [this, paceReply, pacerOf](sol::this_state ts, int intervalMs) -> sol::object {
        const auto pacer = pacerOf();
        if (!pacer)
            return sol::lua_nil;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        return paceReply(ts, pacer->Set(execution::script::RequesterOf(*effectiveEmulator(), execution::Origin::Lua),
                                        std::nullopt, intervalMs));
    });
    for (const char* name : {"pace_next", "pace_prev", "pace_start", "pace_stop", "pace_toggle", "pace_step"})
    {
        const std::string verb = std::string(name).substr(5);
        lua.set_function(name, [this, paceReply, pacerOf, verb](sol::this_state ts) -> sol::object {
            const auto pacer = pacerOf();
            if (!pacer)
                return sol::lua_nil;
            Emulator& emulator = *effectiveEmulator();
            if (verb != "stop")
                execution::script::Check(emulator, execution::Origin::Lua);
            const execution::Requester requester = execution::script::RequesterOf(emulator, execution::Origin::Lua);
            if (verb == "next" || verb == "prev")
                return paceReply(ts, pacer->Shift(requester, verb == "next" ? 1 : -1));
            if (verb == "start")
                return paceReply(ts, pacer->Start(requester));
            if (verb == "stop")
                return paceReply(ts, pacer->Stop());
            if (verb == "toggle")
                return paceReply(ts, pacer->Toggle(requester));
            return paceReply(ts, pacer->Step(requester));
        });
    }

    lua.set_function("run_to_interrupt", [this](sol::optional<bool> skipBP) {
        if (!effectiveEmulator()) return;
        execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
        effectiveEmulator()->RunUntilInterrupt(skipBP.value_or(true));
    });

    lua.set_function("run_until_condition", [this](sol::function predicate, sol::optional<unsigned> maxTStates) {
        if (!effectiveEmulator()) return;
        effectiveEmulator()->RunUntilCondition([&predicate](const Z80State& state) -> bool {
            return predicate(state.pc, state.af, state.bc, state.de, state.hl).get<bool>();
        }, maxTStates.value_or(0));
    });

    // Keyboard operations. Same `key` name space as WebAPI/CLI/Python
    // (DebugKeyboardManager::ResolveKeyName for ZX matrix keys, falling
    // back to pckey::FromName for a PC-only key with no ZX equivalent at
    // all - e.g. "f12", "rshift"; combos accept a mix of both, needed for
    // TS-Conf's BIOS Setup entry, Right Shift + F12)
    // key_route("auto"|"matrix"|"ps2"|"both") -> route in force | nil, err; key_route() queries
    lua.set_function("key_route", [this](sol::this_state s, sol::optional<std::string> route) -> sol::variadic_results {
        sol::variadic_results out;
        Emulator* emulator = effectiveEmulator();
        Keyboard* keyboard = emulator ? emulator->GetContext()->pKeyboard : nullptr;
        if (!keyboard)
        {
            out.push_back(sol::make_object(s, sol::lua_nil));
            out.push_back(sol::make_object(s, std::string("no keyboard")));
            return out;
        }
        std::string error;
        if (route && !route->empty() && !keyboard->RequestHostRoute(*route, error))
        {
            out.push_back(sol::make_object(s, sol::lua_nil));
            out.push_back(sol::make_object(s, error));
            return out;
        }
        out.push_back(sol::make_object(s, std::string(Keyboard::HostRouteName(keyboard->EffectiveHostRoute()))));
        return out;
    });

    // keyboard_controller() -> the PS/2 / XT keyboard controller's name ("PROFI-XT firmware 1.27", ...), "" when the
    // machine has none or it has no name (GET .../keyboard/status keyboard_controller)
    lua.set_function("keyboard_controller", [this]() -> std::string {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        Keyboard* keyboard = ctx ? ctx->pKeyboard : nullptr;
        return keyboard && keyboard->HasPs2Sink() ? keyboard->GetPs2Sink()->ControllerName() : std::string();
    });

    // key_tap(key [, frames [, count [, gap_frames]]]): count taps in a row (1..100), gap_frames after each release.
    // key_tap / key_press / key_release: `key` is one name, or names held together joined with '+' ("w+o")
    lua.set_function("key_tap", [this](const std::string& keyName, sol::optional<uint16_t> holdFrames,
                                       sol::optional<uint16_t> count, sol::optional<uint16_t> gapFrames) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        std::vector<std::string> chord;
        std::string error;
        if (!DebugKeyboardManager::ParseKeyChord(keyName, chord, error))
            return false;  // an unknown name fails, not a silent no-op
        const uint16_t taps = count.value_or(1);
        if (taps < 1 || taps > DebugKeyboardManager::MAX_TAP_COUNT)
            return false;
        DebugKeyboardManager* keyboard = ctx->pDebugManager->GetKeyboardManager();
        if (chord.size() == 1)
            keyboard->TapKeyRepeated(chord.front(), holdFrames.value_or(2), taps, gapFrames.value_or(0));
        else
            keyboard->TapComboRepeated(chord, holdFrames.value_or(2), taps, gapFrames.value_or(0));
        return true;
    });

    lua.set_function("key_press", [this](const std::string& keyName) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        std::vector<std::string> chord;
        std::string error;
        if (!DebugKeyboardManager::ParseKeyChord(keyName, chord, error))
            return false;  // an unknown name fails, not a silent no-op
        if (chord.size() == 1)
            ctx->pDebugManager->GetKeyboardManager()->PressKey(chord.front());
        else
            ctx->pDebugManager->GetKeyboardManager()->PressCombo(chord);
        return true;
    });

    lua.set_function("key_release", [this](const std::string& keyName) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        std::vector<std::string> chord;
        std::string error;
        if (!DebugKeyboardManager::ParseKeyChord(keyName, chord, error))
            return false;  // an unknown name fails, not a silent no-op
        if (chord.size() == 1)
            ctx->pDebugManager->GetKeyboardManager()->ReleaseKey(chord.front());
        else
            ctx->pDebugManager->GetKeyboardManager()->ReleaseCombo(chord);
        return true;
    });

    // key_set({"w", "o"}): the complete held-key state in one call - these keys held, every other injected key
    // released; key_set({}) releases all. An item may be a chord ("caps+1"). False (nothing changed) for an
    // unknown name or while a replay drives the input
    // The list by value: sol reads a Lua table into a container taken by value (a reference wants a userdata)
    lua.set_function("key_set", [this](std::vector<std::string> keyNames) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        std::string error;
        return ctx->pDebugManager->GetKeyboardManager()->SetHeldKeys(keyNames, error) ==
               DebugKeyboardManager::SetHeldKeysStatus::Ok;
    });

    // The list by value: sol fills a container from a table only when the lambda takes it by value
    lua.set_function("key_combo", [this](std::vector<std::string> keyNames, sol::optional<uint16_t> holdFrames) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        ctx->pDebugManager->GetKeyboardManager()->TapCombo(keyNames, holdFrames.value_or(2));
        return true;
    });

    lua.set_function("key_macro", [this](const std::string& macroName) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        return ctx->pDebugManager->GetKeyboardManager()->ExecuteNamedSequence(macroName);
    });

    lua.set_function("key_type", [this](const std::string& text, sol::optional<uint16_t> delayFrames) -> bool {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
            return false;
        // false: a character no key types, nothing typed
        return ctx->pDebugManager->GetKeyboardManager()->TypeText(text, delayFrames.value_or(2)).empty();
    });

    lua.set_function("key_release_all", [this]() {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (ctx && ctx->pDebugManager->GetKeyboardManager())
            ctx->pDebugManager->GetKeyboardManager()->ReleaseAllKeys();
    });

    // Tape operations
    // ok, reason: the reason is set when the insert is refused
    lua.set_function("tape_load", [this](const std::string& path) -> std::tuple<bool, std::string> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return {false, "no emulator"};
        if (std::string refusal = emulator->AskMediaChange(Emulator::MediaChange::LoadTape, 0, path); !refusal.empty())
            return {false, refusal};
        std::string reason;
        const bool loaded = emulator->LoadTape(path, &reason);
        return {loaded, reason};
    });

    lua.set_function("tape_is_inserted", [this]() -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        return ctx && ctx->pTape && !ctx->coreState.tapeFilePath.empty();
    });

    lua.set_function("tape_get_path", [this]() -> std::string {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return "";
        auto* ctx = emulator->GetContext();
        return ctx ? ctx->coreState.tapeFilePath : "";
    });

    // tape_play() -> ok, report. `ok` as before (false: no tape with loadable blocks); `report` is the deck
    // after the call (Tape::PlayWithReport: result, playing, state, reason, position, block, skipped_blocks,
    // text), nil without a machine
    lua.set_function("tape_play", [this](sol::this_state s) -> std::tuple<bool, sol::object> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return {false, sol::object(sol::lua_nil)};
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape) return {false, sol::object(sol::lua_nil)};

        EmulatorPauseBracket bracket(emulator);

        // Parse-once (idempotent); frozen in a block -> resume in place, playing -> left as it is, otherwise
        // start at the first block with a signal from the consumption cursor on - the same semantics as
        // `tape play` / POST /tape/play
        // A tape still on its way (a load while a TTD recording runs) plays once it lands
        const TapePlayReport report = ctx->pTape->PlayWithReport();
        return {report.Ok(), StateNodeToLua(s, report.ToNode())};
    });

    lua.set_function("tape_stop", [this]() -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (ctx && ctx->pTape) {
            ctx->pTape->stopTape();
            return true;
        }
        return false;
    });

    lua.set_function("tape_rewind", [this]() -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape) return false;

        EmulatorPauseBracket bracket(emulator);

        // Rewind keeps the image and catalog — unlike stop/eject
        // (same semantics as `tape rewind` / POST /tape/rewind)
        ctx->pTape->EnsureImageLoaded();
        ctx->pTape->RewindToStart();
        return true;
    });

    lua.set_function("tape_eject", [this]() -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        return emulator->EjectTape();
    });

    lua.set_function("tape_pause", [this]() -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape) return false;

        EmulatorPauseBracket bracket(emulator);

        const TapePlaybackState state = ctx->pTape->GetPlaybackState();
        if (state == TapePlaybackState::Paused)
            return true;  // idempotent, mirrors "Tape already paused"
        if (state != TapePlaybackState::Playing)
            return false;
        ctx->pTape->pausePlayback();  // play resumes in place afterwards
        return true;
    });

    lua.set_function("tape_seek", [this](int blockIndex) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator || blockIndex < 0) return false;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape) return false;

        EmulatorPauseBracket bracket(emulator);

        // A tape still on its way (loaded while a TTD recording runs) takes the seek when it lands
        return ctx->pTape->Seek(static_cast<size_t>(blockIndex)) != TapeSeekResult::OutOfRange;
    });

    lua.set_function("tape_pos", [this]() -> sol::optional<sol::table> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::nullopt;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape || !ctx->pTape->EnsureImageLoaded()) return sol::nullopt;

        EmulatorPauseBracket bracket(emulator);

        sol::state_view lua_view(*_lua);
        sol::table pos = lua_view.create_table();
        pos["state"] = getTapePlaybackStateName(ctx->pTape->GetPlaybackState());

        std::optional<TapePosition> position = ctx->pTape->GetPosition();
        if (position.has_value())
        {
            pos["block"] = position->blockIndex;
            pos["pulse"] = position->pulseIndex;
            pos["seconds_into_block"] = position->secondsIntoBlock;
            pos["block_total_seconds"] = position->blockTotalSeconds;
        }

        pos["cursor"] = ctx->pTape->GetConsumptionCursor();
        pos["block_count"] = ctx->pTape->GetBlockCatalog().size();
        return pos;
    });

    lua.set_function("tape_blocks", [this]() -> sol::optional<sol::table> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::nullopt;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape || !ctx->pTape->EnsureImageLoaded()) return sol::nullopt;

        EmulatorPauseBracket bracket(emulator);

        const std::vector<TapeBlockDescriptor>& catalog = ctx->pTape->GetBlockCatalog();
        const TapeFastLoadPlan& plan = ctx->pTape->GetFastLoadPlan();

        sol::state_view lua_view(*_lua);
        sol::table blocks = lua_view.create_table(static_cast<int>(catalog.size()));
        size_t arrayIndex = 1;
        for (const TapeBlockDescriptor& descriptor : catalog)
        {
            sol::table block = lua_view.create_table();
            block["index"] = descriptor.index;
            block["kind"] = getTapeBlockKindName(descriptor.kind);

            if (descriptor.kind == TapeBlockKindEnum::Header || descriptor.kind == TapeBlockKindEnum::Data ||
                descriptor.kind == TapeBlockKindEnum::Custom)
            {
                block["headerless"] = descriptor.headerless;
            }

            if (descriptor.headerValid)
            {
                block["name"] = descriptor.name;
                block["type"] = getTapeBlockTypeName(descriptor.headerType);
                block["declared_length"] = descriptor.declaredLength;
                block["param1"] = descriptor.param1;
                block["param2"] = descriptor.param2;
            }

            if (descriptor.pairedDataIndex != SIZE_MAX)
                block["paired_data_index"] = descriptor.pairedDataIndex;
            if (descriptor.pairedHeaderIndex != SIZE_MAX)
                block["paired_header_index"] = descriptor.pairedHeaderIndex;

            sol::table speed = lua_view.create_table();
            speed["profile"] = getTapeSpeedProfileName(descriptor.timing.profile);
            if (descriptor.baudEstimate > 0)
                speed["baud"] = descriptor.baudEstimate;
            block["speed"] = speed;

            block["checksum_valid"] = descriptor.checksumValid;
            block["checksum_applicable"] = descriptor.rawSize > 0;
            block["seconds"] = descriptor.estimatedSeconds;
            if (descriptor.rawSize > 0)
                block["raw_size"] = descriptor.rawSize;
            block["playable"] = descriptor.playable;

            if (descriptor.kind != TapeBlockKindEnum::Control && plan.perBlock.size() > descriptor.index)
            {
                block["fast_load"] = plan.perBlock[descriptor.index] == FastLoadRejectEnum::None
                                         ? "yes"
                                         : getFastLoadRejectName(plan.perBlock[descriptor.index]);
            }

            blocks[arrayIndex++] = block;
        }
        return blocks;
    });

    lua.set_function("tape_info", [this]() -> sol::optional<sol::table> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::nullopt;
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pTape) return sol::nullopt;

        EmulatorPauseBracket bracket(emulator);

        const std::string& path = ctx->coreState.tapeFilePath;
        const bool loaded = !path.empty() && ctx->pTape->EnsureImageLoaded();

        sol::state_view lua_view(*_lua);
        sol::table info = lua_view.create_table();
        info["status"] = loaded ? "loaded" : (path.empty() ? "empty" : "error");
        info["file"] = path;
        info["state"] = loaded ? getTapePlaybackStateName(ctx->pTape->GetPlaybackState()) : "idle";
        if (!loaded) return info;

        const TapeFastLoadPlan& plan = ctx->pTape->GetFastLoadPlan();
        info["format"] = ctx->pTape->GetLoadedFormatId();
        info["cursor"] = ctx->pTape->GetConsumptionCursor();
        info["block_count"] = ctx->pTape->GetBlockCatalog().size();
        info["total_seconds"] = plan.totalSeconds;

        FeatureManager* fm = emulator->GetFeatureManager();
        info["fast_tape"] = fm && fm->isEnabled(Features::kFastTape);
        info["turbo_tape"] = fm && fm->isEnabled(Features::kTurboTape);

        sol::table fastLoad = lua_view.create_table();
        fastLoad["verdict"] = getFastLoadVerdictName(plan.verdict);
        fastLoad["eligible_blocks"] = plan.eligibleBlocks;
        fastLoad["accelerated_seconds"] = plan.acceleratedSeconds;
        fastLoad["total_seconds"] = plan.totalSeconds;
        fastLoad["summary"] = plan.summary;
        info["fast_load"] = fastLoad;
        return info;
    });

    // Tape audio bridge: pure path-to-path conversions through the same
    // engine as `tape render` / POST /tape/render — no emulator state involved
    lua.set_function("tape_render",
        [this](const std::string& source, const std::string& output, sol::optional<sol::table> options) -> sol::table {
        TapeRenderRequest request;
        request.sourcePath = source;
        request.outputPath = output;
        if (options.has_value())
        {
            sol::optional<size_t> firstBlock = options.value()["first_block"];
            sol::optional<size_t> lastBlock = options.value()["last_block"];
            sol::optional<uint32_t> sampleRate = options.value()["sample_rate"];
            sol::optional<double> amplitude = options.value()["amplitude"];
            sol::optional<bool> invertLevel = options.value()["invert_level"];
            if (firstBlock.has_value()) request.firstBlock = firstBlock.value();
            if (lastBlock.has_value()) request.lastBlock = lastBlock.value();
            if (sampleRate.has_value()) request.sampleRate = sampleRate.value();
            if (amplitude.has_value()) request.amplitude = amplitude.value();
            if (invertLevel.has_value()) request.invertLevel = invertLevel.value();
        }

        TapeRenderResult result = RenderTapeToAudio(request);

        sol::state_view lua_view(*_lua);
        sol::table ret = lua_view.create_table();
        ret["ok"] = result.ok;
        ret["error"] = result.errorText;
        ret["duration_sec"] = result.durationSec;
        ret["samples"] = result.samplesWritten;
        ret["blocks"] = result.blocksRendered;
        ret["encoder"] = result.encoderUsed;
        sol::table warnings = lua_view.create_table();
        int warningIndex = 1;
        for (const std::string& warning : result.warnings)
            warnings[warningIndex++] = warning;
        ret["warnings"] = warnings;
        return ret;
    });

    // Same engine as `tape import` / POST /tape/import: decode + extract
    // + recognize, then an extension-dispatched save (.tzx exact, .tap gated)
    lua.set_function("tape_import",
        [this](const std::string& source, const std::string& output, sol::optional<double> hysteresis) -> sol::table {
        TapeImportRequest request;
        request.sourcePath = source;
        if (hysteresis.has_value())
            request.hysteresis = hysteresis.value();

        TapeImportResult imported = ImportAudioToTape(request);
        TapeSaveResult saved;
        if (imported.ok)
            saved = SaveTapeImage(imported.image, output);

        sol::state_view lua_view(*_lua);
        sol::table ret = lua_view.create_table();
        ret["ok"] = imported.ok && saved.ok;
        ret["error"] = !imported.ok ? imported.errorText : saved.errorText;
        ret["decoder"] = imported.decoderUsed;
        ret["sample_rate"] = imported.sampleRate;
        ret["samples_decoded"] = imported.samplesDecoded;
        ret["signal_edges"] = imported.signalEdges;
        ret["blocks_recognized"] = imported.blocksRecognized;
        ret["blocks_written"] = saved.blocksWritten;
        ret["output_path"] = output;
        sol::table warnings = lua_view.create_table();
        int warningIndex = 1;
        for (const std::string& warning : imported.warnings)
            warnings[warningIndex++] = warning;
        ret["warnings"] = warnings;
        return ret;
    });
}
