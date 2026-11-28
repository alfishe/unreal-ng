// LuaEmulator bindings: mouse, keyboard, joysticks and breakpoints.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterInputAndBreakpoints(sol::state& lua)
{
    // Mouse injection (Kempston Mouse, automation-interfaces §4.7)
    // Target: effectiveEmulator() (bound instance, else the selected one).
    // Success returns the state table; failure returns nil, "message".
    lua.set_function("mouse_move", [this](sol::this_state s, sol::object dxArg, sol::object dyArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        long long dx = 0;
        long long dy = 0;
        if (!mouseIntArg(dxArg, "dx", INT_MIN, INT_MAX, dx, error) ||
            !mouseIntArg(dyArg, "dy", INT_MIN, INT_MAX, dy, error))
            return mouseError(s, error);
        return mouseResult(s, *mgr, mgr->Move(static_cast<int>(dx), static_cast<int>(dy)));
    });

    // A long move (-4096..4096) in steps the program follows, one per frame; input sent meanwhile queues
    lua.set_function("mouse_glide", [this](sol::this_state s, sol::object dxArg, sol::object dyArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        long long dx = 0;
        long long dy = 0;
        if (!mouseIntArg(dxArg, "dx", INT_MIN, INT_MAX, dx, error) ||
            !mouseIntArg(dyArg, "dy", INT_MIN, INT_MAX, dy, error))
            return mouseError(s, error);
        return mouseResult(s, *mgr, mgr->Glide(static_cast<int>(dx), static_cast<int>(dy)));
    });

    // The machine's mouse devices (table of device tables; empty when the machine has none)
    lua.set_function("mouse_devices", [this](sol::this_state s) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        sol::state_view lua(s);
        sol::table devices = lua.create_table();
        const MouseStateSnapshot state = mgr->GetState();
        for (size_t i = 0; i < state.devices.size(); i++)
            devices[i + 1] = mouseDeviceTable(s, state.devices[i]);
        sol::variadic_results results;
        results.push_back(sol::make_object(s, devices));
        return results;
    });

    lua.set_function("mouse_busy", [this]() -> bool {
        DebugMouseManager* mgr = mouseManager();
        return mgr && mgr->IsBusy();
    });

    lua.set_function("mouse_press", [this](sol::this_state s, sol::object buttonArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        auto button = mouseButtonArg(buttonArg, error);
        if (!button)
            return mouseError(s, error);
        return mouseResult(s, *mgr, mgr->PressButton(*button));
    });

    lua.set_function("mouse_release", [this](sol::this_state s, sol::object buttonArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        auto button = mouseButtonArg(buttonArg, error);
        if (!button)
            return mouseError(s, error);
        return mouseResult(s, *mgr, mgr->ReleaseButton(*button));
    });

    lua.set_function("mouse_click", [this](sol::this_state s, sol::object buttonArg, sol::object framesArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        auto button = mouseButtonArg(buttonArg, error);
        if (!button)
            return mouseError(s, error);
        long long frames = DebugMouseManager::DEFAULT_CLICK_FRAMES;
        if (framesArg.valid() && framesArg.get_type() != sol::type::lua_nil &&
            !mouseIntArg(framesArg, "frames", LLONG_MIN, LLONG_MAX, frames, error))
            return mouseError(s, error);
        if (frames < 0 || frames > static_cast<long long>(UINT32_MAX))
            return mouseError(s, "frames=" + std::to_string(frames) + " out of range 1.." +
                                     std::to_string(DebugMouseManager::MAX_CLICK_FRAMES));
        return mouseResult(s, *mgr, mgr->Click(*button, static_cast<uint32_t>(frames)));
    });

    lua.set_function("mouse_buttons", [this](sol::this_state s, sol::object pressedArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        uint8_t bits = 0;
        if (pressedArg.get_type() != sol::type::lua_nil)
        {
            if (pressedArg.get_type() != sol::type::table)
                return mouseError(s, "pressed must be a table of button names ({} = none)");
            sol::table pressed = pressedArg.as<sol::table>();
            std::string error;
            for (size_t i = 1; i <= pressed.size(); ++i)
            {
                sol::object item = pressed[i];
                auto button = mouseButtonArg(item, error);
                if (!button)
                    return mouseError(s, error);
                bits |= static_cast<uint8_t>(*button);
            }
        }
        return mouseResult(s, *mgr, mgr->SetPressedButtons(bits));
    });

    lua.set_function("mouse_wheel", [this](sol::this_state s, sol::object stepsArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        long long steps = 0;
        if (!mouseIntArg(stepsArg, "steps", INT_MIN, INT_MAX, steps, error))
            return mouseError(s, error);
        return mouseResult(s, *mgr, mgr->Wheel(static_cast<int>(steps)));
    });

    lua.set_function("mouse_release_all", [this](sol::this_state s) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        return mouseResult(s, *mgr, mgr->ReleaseAllButtons());
    });

    lua.set_function("mouse_set_counters", [this](sol::this_state s, sol::object xArg, sol::object yArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        std::string error;
        long long x = 0;
        long long y = 0;
        if (!mouseIntArg(xArg, "x", INT_MIN, INT_MAX, x, error) ||
            !mouseIntArg(yArg, "y", INT_MIN, INT_MAX, y, error))
            return mouseError(s, error);
        return mouseResult(s, *mgr, mgr->SetCounters(static_cast<int>(x), static_cast<int>(y)));
    });

    lua.set_function("mouse_status", [this](sol::this_state s, sol::object deviceArg) {
        DebugMouseManager* mgr = mouseManager();
        if (!mgr)
            return mouseError(s, "mouse manager not available");
        // mouse_status([device]): the machine's mouse, or the device named ("kempston", "sprinter", "evo-ps2")
        std::string deviceId;
        if (deviceArg.valid() && deviceArg.get_type() != sol::type::lua_nil)
        {
            if (deviceArg.get_type() != sol::type::string)
                return mouseError(s, "device must be a string (kempston, sprinter, evo-ps2)");
            deviceId = deviceArg.as<std::string>();
        }
        if (const MouseInjectResult check = mgr->CheckDevice(deviceId); !check.ok())
            return mouseError(s, check.message);
        const MouseStateSnapshot state = mgr->GetState(deviceId);
        if (!state.available)
            return mouseError(s, "Mouse device not available");
        sol::variadic_results results;
        sol::table table = mouseStateTable(s, state);
        // Routing mirrors GET /mouse/status: `present` alone cannot distinguish
        // "not fitted" from "fitted but shadowed" (mouse design Q4 / gap D-1)
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
        if (context && context->pPortDecoder)
        {
            bool decoded = false;
            std::string note;
            context->pPortDecoder->GetMouseRoutingState(decoded, note);
            sol::state_view lua(s);
            sol::table routing = lua.create_table();
            routing["ports_decoded"] = decoded;
            routing["note"] = note;
            table["routing"] = routing;
        }
        results.push_back(sol::make_object(s, table));
        return results;
    });

    lua.set_function("mouse_click_pending", [this]() -> bool {
        DebugMouseManager* mgr = mouseManager();
        return mgr && mgr->IsClickPending();
    });

    lua.set_function("mouse_button_names", []() -> sol::as_table_t<std::vector<std::string>> {
        return sol::as_table(DebugMouseManager::GetAllButtonNames());
    });

    // Joystick injection (Kempston joystick, joystick TDD §5). Same contract as the mouse functions:
    // success returns the state table (same keys as GET /joystick), failure returns nil, "message".
    lua.set_function("joystick_press", [this](sol::this_state s, sol::object buttonsArg) {
        DebugJoystickManager* mgr = joystickManager();
        if (!mgr)
            return mouseError(s, "joystick manager not available");
        std::string names;
        std::string error;
        if (!joystickNamesArg(buttonsArg, names, error))
            return mouseError(s, error);
        return joystickResult(s, *mgr, mgr->Press(names));
    });

    lua.set_function("joystick_release", [this](sol::this_state s, sol::object buttonsArg) {
        DebugJoystickManager* mgr = joystickManager();
        if (!mgr)
            return mouseError(s, "joystick manager not available");
        std::string names;
        std::string error;
        if (!joystickNamesArg(buttonsArg, names, error))
            return mouseError(s, error);
        return joystickResult(s, *mgr, mgr->Release(names));
    });

    // joystick_set(0xE5) sets the raw byte; joystick_set({"up", "fire"}) / joystick_set("up+fire") the held set; {} releases all
    lua.set_function("joystick_set", [this](sol::this_state s, sol::object stateArg) {
        DebugJoystickManager* mgr = joystickManager();
        if (!mgr)
            return mouseError(s, "joystick manager not available");
        std::string error;
        if (stateArg.get_type() == sol::type::number)
        {
            long long state = 0;
            if (!mouseIntArg(stateArg, "state", LLONG_MIN, LLONG_MAX, state, error))
                return mouseError(s, error);
            return joystickResult(s, *mgr, mgr->SetStateChecked(state));
        }
        std::string names;
        if (!joystickNamesArg(stateArg, names, error))
            return mouseError(s, "state must be an integer, a string or a table of button names");
        if (names.empty())
            return joystickResult(s, *mgr, mgr->ReleaseAll());
        return joystickResult(s, *mgr, mgr->SetNames(names));
    });

    lua.set_function("joystick_tap", [this](sol::this_state s, sol::object buttonsArg, sol::object framesArg) {
        DebugJoystickManager* mgr = joystickManager();
        if (!mgr)
            return mouseError(s, "joystick manager not available");
        std::string names;
        std::string error;
        if (!joystickNamesArg(buttonsArg, names, error))
            return mouseError(s, error);
        long long frames = DebugJoystickManager::DEFAULT_TAP_FRAMES;
        if (framesArg.valid() && framesArg.get_type() != sol::type::lua_nil &&
            !mouseIntArg(framesArg, "frames", LLONG_MIN, LLONG_MAX, frames, error))
            return mouseError(s, error);
        return joystickResult(s, *mgr, mgr->TapChecked(names, frames));
    });

    lua.set_function("joystick_state", [this](sol::this_state s) {
        DebugJoystickManager* mgr = joystickManager();
        if (!mgr)
            return mouseError(s, "joystick manager not available");
        const JoystickStateSnapshot state = mgr->GetState();
        if (!state.available)
            return mouseError(s, "Joystick device not available");
        sol::variadic_results results;
        results.push_back(sol::make_object(s, joystickStateTable(s, state)));
        return results;
    });

    lua.set_function("joystick_tap_pending", [this]() -> bool {
        DebugJoystickManager* mgr = joystickManager();
        return mgr && mgr->IsTapPending();
    });

    lua.set_function("joystick_button_names", []() -> sol::as_table_t<std::vector<std::string>> {
        return sol::as_table(DebugJoystickManager::GetAllButtonNames());
    });

    // Snapshot operations
    // snapshot_load(path [, commit | {commit, switch_model}]) -> ok, reason, emulator_id: the reason is set when
    // TTD refuses the load (recording), the snapshot pipeline refuses it (the machine's policy, an unknown commit
    // name, a machine that cannot hold the file) or the load fails. `commit` chooses who writes the machine:
    // omitted = the plan decides (the machine's own policy, else the legacy commit), "legacy" = the legacy commit
    // always, or a registered policy name. A file for another machine (an SPG: TS-Conf; an SZX of another model,
    // a 128K .z80 / .sna on a 48K) switches the model first when this Lua is not bound to one machine (as
    // rzx_play): `switch_model` true / false, omitted = the launcher's rule (an SPG switches, a snapshot of
    // another machine only with [SNAPSHOT] SwitchModel=1); emulator_id is then the new instance
    lua.set_function("snapshot_load", [this](const std::string& path, sol::optional<sol::object> arg)
                                          -> std::tuple<bool, std::string, std::string> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return {false, "no emulator", ""};
        SnapshotLoadRequest request;
        request.emulatorId = emulator->GetId();
        request.path = path;
        if (arg && arg->is<std::string>())
            request.commit = arg->as<std::string>();
        else if (arg && arg->is<sol::table>())
        {
            const sol::table options = arg->as<sol::table>();
            request.commit = options.get_or<std::string>("commit", "");
            if (const sol::optional<bool> switchModel = options.get<sol::optional<bool>>("switch_model"))
                request.switchModel = *switchModel;
        }
        if (_emulator != nullptr)
            request.switchModel = false;   // a script bound to a machine never replaces it; unbound: as asked
        const SnapshotLoadResult result = SnapshotLauncher::Load(request);
        return {result.ok, result.message, result.emulator ? result.emulator->GetId() : std::string()};
    });

    // snapshot_inspect(path [, commit]) -> table | nil, error: what loading the file would do here, nothing
    // written. {path, image = {format, format_version, machine_hint, memory_model, banks = {{bank, size, hash}},
    // cpu, paging, border, ay, extensions, warnings}, plan = {commit, refused, reason, needs, verdicts,
    // warnings}, would_load, would_commit}
    lua.set_function("snapshot_inspect", [this](sol::this_state state, const std::string& path,
                                                sol::optional<std::string> commit)
                                                 -> std::tuple<sol::object, std::string> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return {sol::lua_nil, "no emulator"};
        StateNode result;
        std::string error;
        if (!SnapshotLauncher::Inspect(emulator->GetId(), path, commit.value_or(""), result, error))
            return {sol::lua_nil, error};
        return {StateNodeToLua(state, result), std::string()};
    });

    // snapshot_report() -> table | nil: what the snapshot pipeline did with the last load {format, machine_hint,
    // commit, refused, reason, needs, verdicts, items = {{item, outcome, note}}, warnings}
    lua.set_function("snapshot_report", [this](sol::this_state state) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator || emulator->LastSnapshotReport().format.empty())
            return sol::lua_nil;
        return StateNodeToLua(state, emulator->LastSnapshotReport().ToStateNode());
    });

    // transfer_state{to = id | model = name [, ram_size = KB, symbolic_id = "..."] [, check = true]
    //                [, keep_frame_position = false] [, start = true | false]} -> report | nil, error
    // This machine's running state into another instance, in memory (StateTransferControl, as WebAPI POST
    // /snapshot/transfer, MCP transfer_state, CLI snapshot transfer): an existing one (`to`; `check` only says what
    // would move) or a new one of a model (started when this one runs, unless `start` says otherwise). The report
    // is the WebAPI's answer: {ok, mode = "clone" | "cross-model", reason, items = {{name, status, detail}},
    // copied, dropped, refused, summary, source_id, target_id, created, state, model, ram_kb}; ok = false: refused,
    // nothing changed. nil, error when there is no report (no such target, the coordinator refuses, a bad request)
    lua.set_function("transfer_state", [this](sol::this_state s, sol::table opts) -> std::tuple<sol::object, sol::object> {
        Emulator* source = effectiveEmulator();
        if (!source)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
        StateTransferRequest request;
        request.sourceId = source->GetId();
        request.to = opts.get_or<std::string>("to", "");
        request.model = opts.get_or<std::string>("model", "");
        request.ramSizeKB = opts.get_or<uint32_t>("ram_size", 0);
        request.symbolicId = opts.get_or<std::string>("symbolic_id", "");
        request.check = opts.get_or("check", false);
        if (const sol::optional<bool> keep = opts.get<sol::optional<bool>>("keep_frame_position"))
            request.keepFramePosition = *keep;
        if (const sol::optional<bool> start = opts.get<sol::optional<bool>>("start"))
            request.start = *start;
        // The automation gate as every changing call of a script (a check only reads)
        execution::script::Check(*source, execution::Origin::Lua, !request.check);
        const StateTransferReply reply = StateTransferControl::Execute(request);
        if (!reply.hasReport)
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, reply.error + ": " + reply.message)};
        return {StateNodeToLua(s, reply.body), sol::make_object(s, sol::lua_nil)};
    });

    // RZX input recordings. rzx_play(path [, options]) -> table {ok, message,
    // emulator_id, model_switched, required_model, summary}; options:
    // desync_mode ("strict" / "tolerant"), ei_short_frame_blocks_int,
    // ld_air_parity_quirk, ignore_later_snapshots, switch_model. The model
    // switches only when this interpreter follows the selected machine (a
    // bound one reports the mismatch: its machine must not be replaced)
    // The files opened lately (RecentFiles; open one: action("media.open_recent", nil, {index = 0, kind = "tape"}))
    lua.set_function("recent_files", [](sol::this_state ts, sol::optional<std::string> kind) -> sol::object {
        const std::string k = kind.value_or("all");
        if (k != "all" && !RecentFiles::IsKind(k))
            throw sol::error("kind '" + k + "': all, snapshot, tape, disk, rzx, image or cd");
        return StateNodeToLua(ts, RecentFiles::Instance().ToValue(k));
    });
    lua.set_function("recent_clear", [](sol::optional<std::string> kind) {
        const std::string k = kind.value_or("all");
        if (k != "all" && !RecentFiles::IsKind(k))
            throw sol::error("kind '" + k + "': all, snapshot, tape, disk, rzx, image or cd");
        RecentFiles::Instance().Clear(k);
    });
    // The core's recording presets (video_record("start", {preset = "gameplay"}))
    lua.set_function("recording_presets", [](sol::this_state ts) -> sol::object {
        return StateNodeToLua(ts, capture::RecordingPresetsValue());
    });

    lua.set_function("rzx_play", [this](const std::string& path, sol::optional<sol::table> options) -> sol::table {
        sol::state_view view(*_lua);
        sol::table out = view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            out["ok"] = false;
            out["message"] = "no emulator";
            return out;
        }
        rzx::LaunchRequest request;
        request.emulatorId = emulator->GetId();
        request.path = path;
        request.switchModel = _emulator == nullptr;
        if (options)
        {
            const sol::table& o = *options;
            sol::optional<std::string> mode = o["desync_mode"];
            if (mode && !rzx::RzxLauncher::ParseDesyncMode(*mode, request.options.desyncMode))
            {
                out["ok"] = false;
                out["message"] = "desync_mode '" + *mode + "': expected strict or tolerant";
                return out;
            }
            request.options.eiShortFrameBlocksInt = o.get_or("ei_short_frame_blocks_int", false);
            request.options.ldAirParityQuirk = o.get_or("ld_air_parity_quirk", false);
            request.options.ignoreLaterSnapshots = o.get_or("ignore_later_snapshots", false);
            request.switchModel = request.switchModel && o.get_or("switch_model", true);
        }
        const rzx::LaunchResult result = rzx::RzxLauncher::Play(request);
        out["ok"] = result.play.Ok();
        out["error"] = std::string(rzx::PlayErrorName(result.play.error));
        out["message"] = result.play.message;
        out["emulator_id"] = result.emulator ? result.emulator->GetId() : std::string();
        out["model_switched"] = result.modelSwitched;
        out["required_model"] = result.play.requiredModel;
        out["model"] = result.switchedToModel;
        if (result.emulator)
            out["summary"] = rzx::RzxLauncher::StatusLine(result.emulator->GetRzxStatus());
        return out;
    });

    // rzx_seek(frame) -> ok, reason: to the boundary after `frame` frames
    lua.set_function("rzx_seek", [this](uint64_t frame) -> std::tuple<bool, std::string> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {false, "no emulator"};
        std::string error;
        const bool ok = emulator->SeekRzx(frame, &error);
        return {ok, error};
    });

    lua.set_function("rzx_stop", [this]() -> bool {
        Emulator* emulator = effectiveEmulator();
        return emulator && emulator->StopRzx();
    });

    lua.set_function("rzx_status", [this]() -> sol::table {
        sol::state_view view(*_lua);
        sol::table t = view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            t["loaded"] = false;
            return t;
        }
        const rzx::SessionStatus status = emulator->GetRzxStatus();
        t["loaded"] = status.loaded;
        t["active"] = status.active;
        t["summary"] = rzx::RzxLauncher::StatusLine(status);
        if (!status.loaded)
            return t;
        const rzx::PlayerStatus& player = status.player;
        t["path"] = status.path;
        t["creator"] = status.creator;
        t["state"] = std::string(rzx::StateName(player.state));
        t["frame"] = player.frame;
        t["total_frames"] = player.totalFrames;
        t["block"] = player.block + 1;
        t["blocks"] = player.blocks;
        t["interrupts"] = player.interrupts;
        t["desyncs"] = player.desyncs;
        t["drift"] = player.drift;
        t["max_drift"] = player.maxDrift;
        t["snapshots_applied"] = player.snapshotsApplied;
        t["keyframes"] = player.keyframes;
        t["keyframe_bytes"] = player.keyframeBytes;
        t["reason"] = player.stopReason;
        if (player.desyncs > 0)
        {
            sol::table first = view.create_table();
            first["kind"] = std::string(rzx::DesyncName(player.firstDesync.kind));
            first["frame"] = player.firstDesync.frame;
            first["expected"] = player.firstDesync.expected;
            first["actual"] = player.firstDesync.actual;
            first["pc"] = player.firstDesync.pc;
            t["first_desync"] = first;
        }
        return t;
    });

    // snapshot_save(path) -> bool; false = refused or failed, snapshot_save_report() says why
    lua.set_function("snapshot_save", [this](const std::string& path) -> bool {
        if (!effectiveEmulator()) return false;
        return effectiveEmulator()->SaveSnapshot(path);
    });

    // snapshot_save_report() -> table | nil: the last save {ok, message, format, path, machine, reason, needs, warnings}
    lua.set_function("snapshot_save_report", [this](sol::this_state state) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator || emulator->LastSaveResult().path.empty())
            return sol::lua_nil;
        return StateNodeToLua(state, emulator->LastSaveResult().ToStateNode());
    });

    // snapshot_formats() -> table: which formats this machine can be saved in right now {view_available, machine, view,
    // formats = {{format, available, reason, needs, note}}}
    lua.set_function("snapshot_formats", [this](sol::this_state state) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return sol::lua_nil;
        return StateNodeToLua(state, emulator->SnapshotSaveFormats().ToStateNode());
    });

    // Breakpoint management
    // Options: a page string ("ram32": physical, through any slot that shows it; the Next's "bank8k33", "divmmc3",
    // "divmmcrom", "mfrom", "mfram"), or a table
    // {page=, to=, slot_only=, hits=, condition=, dialect=} (to = range end; hits "5" | ">=5" | "%5"; condition "A == 7").
    // -1 and the reason when refused
    lua.set_function("bp", [this](uint16_t addr, sol::optional<sol::object> options) {
        return AddScriptBreakpoint(BRK_MEMORY, BRK_MEM_EXECUTE, addr, options);
    });

    // Options: a page string ("ram32": physical, through any slot that shows it), or a table
    // {page=, to=, slot_only=, hits=, condition=, dialect=} (to = range end; hits "5" | ">=5" | "%5"; condition "A == 7").
    // -1 and the reason when refused
    lua.set_function("bp_read", [this](uint16_t addr, sol::optional<sol::object> options) {
        return AddScriptBreakpoint(BRK_MEMORY, BRK_MEM_READ, addr, options);
    });

    // Options: a page string ("ram32": physical, through any slot that shows it), or a table
    // {page=, to=, slot_only=, hits=, condition=, dialect=} (to = range end; hits "5" | ">=5" | "%5"; condition "A == 7").
    // -1 and the reason when refused
    lua.set_function("bp_write", [this](uint16_t addr, sol::optional<sol::object> options) {
        return AddScriptBreakpoint(BRK_MEMORY, BRK_MEM_WRITE, addr, options);
    });

    // Options: a table {mask=, hits=, condition=, dialect=} (mask: (port & mask) == (port & mask) matches)
    lua.set_function("bp_port_in", [this](uint16_t port, sol::optional<sol::object> options) {
        return AddScriptBreakpoint(BRK_IO, BRK_IO_IN, port, options);
    });

    // Options: a table {mask=, hits=, condition=, dialect=} (mask: (port & mask) == (port & mask) matches)
    lua.set_function("bp_port_out", [this](uint16_t port, sol::optional<sol::object> options) {
        return AddScriptBreakpoint(BRK_IO, BRK_IO_OUT, port, options);
    });

    // ZX Next register breakpoints. Options: a table {to=, doors=, value=, vmask=, hits=} (to = register range end; doors
    // "nextreg,port,copper" a write may come through; value = stop only on it, vmask = the compared bits). -1 when refused
    lua.set_function("bp_nextreg_write", [this](uint16_t reg, sol::optional<sol::table> options) -> int {
        return AddScriptNextRegBreakpoint(BRK_IO_OUT, reg, options);
    });
    lua.set_function("bp_nextreg_read", [this](uint16_t reg, sol::optional<sol::table> options) -> int {
        return AddScriptNextRegBreakpoint(BRK_IO_IN, reg, options);
    });
    // bp_beam(line, {hc=}) stops when the beam passes the place (the copper's counters); bp_copper(index, {to=}) when the copper
    // completes that list instruction. -1 when refused
    lua.set_function("bp_beam", [this](uint16_t line, sol::optional<sol::table> options) -> int {
        return AddScriptPositionBreakpoint(BRK_BEAM, line, options);
    });
    lua.set_function("bp_copper", [this](uint16_t index, sol::optional<sol::table> options) -> int {
        return AddScriptPositionBreakpoint(BRK_COPPER, index, options);
    });
    // bp_magic("ed_ff" | "dd_01", {hits=}) stops after the CPU ran the magic opcode (a NOP / LD BC,nn on the hardware). -1 when refused
    lua.set_function("bp_magic", [this](const std::string& opcode, sol::optional<sol::table> options) -> int {
        if (opcode != "ed_ff" && opcode != "dd_01")
            return -1;
        return AddScriptPositionBreakpoint(BRK_MAGIC, opcode == "ed_ff" ? BRK_MAGIC_ED_FF : BRK_MAGIC_DD_01, options);
    });
    // bp_event(kind, {value=, vmask=, hits=, cpu=}) stops on a General Sound / NeoGS protocol event: the card's (int, nmi,
    // reset, page, card_command, card_data, card_reply, dac_fetch, clock_change, dma_start, dma_done, gscfg0) on the gs
    // CPU, the host's (host_command, host_data, host_reply) and the machine's (cpu_int, cpu_reti, cpu_nmi, cpu_retn, machine_reset, divmmc_map, divmmc_unmap, esxdos_call, zxndma_start, zxndma_done, tsdma_start, tsdma_done, pld_read, pld_write, pld_event, fdc_command, fdc_done, int_accept, int_return)
    // on the main one; cpu defaults to the kind's side. The id; -1 and the reason when refused
    lua.set_function("bp_event", [this](const std::string& kindName, sol::optional<sol::table> options)
                                     -> std::tuple<int, sol::optional<std::string>> {
        GSCardEvent kind = GSCardEvent::Count;
        if (!ParseGSCardEvent(kindName.c_str(), kind))
            return {-1, std::string("unknown event '") + kindName + "'"};
        std::string cpu = GSCardEventIsHostSide(kind) ? "main" : "gs";
        BreakpointSpec spec;
        spec.type = BRK_EVENT;
        spec.access = 0;
        spec.address = static_cast<uint16_t>(kind);
        std::string hits;
        if (options)
        {
            const sol::table& t = *options;
            cpu = t.get<sol::optional<std::string>>("cpu").value_or(cpu);
            if (auto value = t.get<sol::optional<int32_t>>("value"))
            {
                spec.nrValue = static_cast<uint8_t>(*value);
                spec.nrMask = 0xFF;
            }
            if (auto vmask = t.get<sol::optional<int32_t>>("vmask"))
                spec.nrMask = static_cast<uint8_t>(*vmask);
            hits = t.get<sol::optional<std::string>>("hits").value_or("");
            if (hits.empty())
                if (auto n = t.get<sol::optional<int64_t>>("hits"))
                    hits = std::to_string(*n);
        }
        BreakpointManager* bpm = ScriptBreakpoints(cpu);
        if (!bpm)
            return {-1, std::string(effectiveEmulator() ? "cpu must be 'main', 'gs' or 'ms'" : "no emulator")};
        std::string error;
        if (!BreakpointManager::ParseHitSpec(hits, spec.hitMode, spec.hitTarget, error))
            return {-1, error};
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        if (id == BRK_INVALID)
            return {-1, error};
        return {static_cast<int>(id), sol::nullopt};
    });
    // bp_chipreg(chip, reg, {to=, unit=, value=, vmask=, hits=}) stops the main CPU after its OUT wrote a sound chip's
    // register: chip ay, fm, saa, opl4, opl4wave, dac or cbl (chipregister.h), to= the last register of a range, unit= one
    // chip of the kind. The id; -1 and the reason when refused
    lua.set_function("bp_chipreg", [this](const std::string& chipName, int32_t reg, sol::optional<sol::table> options)
                                       -> std::tuple<int, sol::optional<std::string>> {
        SoundChipKind chip = SoundChipKind::Count;
        if (!ParseSoundChipKind(chipName.c_str(), chip))
            return {-1, std::string("unknown chip '") + chipName + "': ay, fm, saa, opl4, opl4wave, dac or cbl"};
        if (reg < 0 || reg > 0xFFFF)
            return {-1, std::string("a register is 0..") + std::to_string(SoundChipLastRegister(chip))};
        BreakpointSpec spec;
        spec.type = BRK_CHIPREG;
        spec.access = 0;
        spec.chip = static_cast<uint8_t>(chip);
        spec.address = static_cast<uint16_t>(reg);
        std::string hits;
        if (options)
        {
            const sol::table& t = *options;
            if (auto to = t.get<sol::optional<int32_t>>("to"))
            {
                spec.hasEnd = true;
                spec.addressEnd = static_cast<uint16_t>(*to);
            }
            if (auto unit = t.get<sol::optional<int32_t>>("unit"))
                spec.chipUnit = static_cast<uint8_t>(*unit);
            if (auto value = t.get<sol::optional<int32_t>>("value"))
            {
                spec.nrValue = static_cast<uint8_t>(*value);
                spec.nrMask = 0xFF;
            }
            if (auto vmask = t.get<sol::optional<int32_t>>("vmask"))
                spec.nrMask = static_cast<uint8_t>(*vmask);
            hits = t.get<sol::optional<std::string>>("hits").value_or("");
            if (hits.empty())
                if (auto n = t.get<sol::optional<int64_t>>("hits"))
                    hits = std::to_string(*n);
        }
        BreakpointManager* bpm = ScriptBreakpoints("main");
        if (!bpm)
            return {-1, std::string("no emulator")};
        std::string error;
        if (!BreakpointManager::ParseHitSpec(hits, spec.hitMode, spec.hitTarget, error))
            return {-1, error};
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        if (id == BRK_INVALID)
            return {-1, error};
        return {static_cast<int>(id), sol::nullopt};
    });
    lua.set_function("bp_nextreg", [this](uint16_t reg, sol::optional<sol::table> options) -> int {
        return AddScriptNextRegBreakpoint(BRK_IO_IN | BRK_IO_OUT, reg, options);
    });

    lua.set_function("bp_remove", [this](uint16_t id, sol::optional<std::string> cpu) -> bool {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        return bpm ? bpm->RemoveBreakpointByID(id) : false;
    });

    lua.set_function("bp_clear", [this](sol::optional<std::string> cpu) {
        if (BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or("")))
            bpm->ClearUserBreakpoints();
    });

    lua.set_function("bp_enable", [this](uint16_t id, sol::optional<std::string> cpu) -> bool {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        return bpm ? bpm->ActivateBreakpoint(id) : false;
    });

    lua.set_function("bp_disable", [this](uint16_t id, sol::optional<std::string> cpu) -> bool {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        return bpm ? bpm->DeactivateBreakpoint(id) : false;
    });

    // bp_note / bp_group / bp_select: `cpu` last ("gs" / "ms": the sound card CPU's breakpoints; a script error for a
    // CPU that is not there)
    lua.set_function("bp_note", [this](uint16_t id, const std::string& note, sol::optional<std::string> cpu) -> bool {
        if (!effectiveEmulator()) return false;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pDebugManager) return false;
        BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager(ScriptCpu(cpu));
        return bpm ? bpm->SetBreakpointNote(id, note) : false;
    });

    // The condition of a memory or port breakpoint ("" clears it): true; false and the reason when it does not
    // compile (the old condition stays) or there is no such breakpoint. `cpu` "gs": the sound card CPU's
    lua.set_function("bp_condition", [this](uint16_t id, const std::string& condition, sol::optional<std::string> dialect,
                                            sol::optional<std::string> cpu)
                                         -> std::tuple<bool, sol::optional<std::string>> {
        if (!effectiveEmulator())
            return {false, std::string("no emulator")};
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        if (!bpm)
            return {false, std::string("cpu must be 'main', 'gs' or 'ms'")};
        BreakpointConditionDialect d = BreakpointConditionDialect::Unreal;
        if (!BreakpointConditionProgram::ParseDialect(dialect.value_or(""), d))
            return {false, std::string("dialect must be 'unreal' or 'c'")};
        std::string error;
        if (!bpm->SetBreakpointCondition(id, condition, error, d))
            return {false, error};
        return {true, sol::nullopt};
    });

    lua.set_function("bp_group", [this](uint16_t id, const std::string& group, sol::optional<std::string> cpu) -> bool {
        if (!effectiveEmulator()) return false;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pDebugManager) return false;
        BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager(ScriptCpu(cpu));
        return bpm ? bpm->SetBreakpointGroup(id, group) : false;
    });

    // Breakpoints by selection (BreakpointSelection: all, an id, a kind, group:<name>): bp_select(select, op) with op
    // enable | disable | toggle | remove answers {select, selected, changed, active}; a refusal: ok = false, error
    lua.set_function("bp_select", [this](sol::this_state ts, const std::string& select, const std::string& op,
                                         sol::optional<std::string> cpu) -> sol::object {
        StateNode out = StateNode::Object();
        std::string error;
        const char* code = DebugError::kInvalidArgument;
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        BreakpointManager* bpm =
            ctx && ctx->pDebugManager ? ctx->pDebugManager->GetBreakpointsManager(ScriptCpu(cpu)) : nullptr;
        if (!bpm)
        {
            error = "no emulator";
            code = DebugError::kNotFound;
        }
        else if (BreakpointSelection::Run(*bpm, select, op, out, error))
            return StateNodeToLua(ts, out);
        out["ok"] = false;
        out["error"] = error;
        out["code"] = std::string(code);
        return StateNodeToLua(ts, out);
    });
    // bp_key(key, {on = "press" | "release" | "both", hits =, group =, note =}): stops on the first keyboard read
    // that sees the key ("any": any key) go down / up. The id, or nil and the reason
    lua.set_function("bp_key", [this](sol::this_state ts, const std::string& key, sol::optional<sol::table> opts)
                                   -> std::tuple<sol::object, sol::object> {
        auto fail = [&ts](const std::string& why) {
            return std::make_tuple(sol::make_object(ts, sol::lua_nil), sol::make_object(ts, why));
        };
        BreakpointManager* bpm = effectiveEmulator() ? effectiveEmulator()->GetBreakpointManager() : nullptr;
        if (!bpm)
            return fail("no emulator");
        BreakpointSpec spec;
        std::string error;
        const std::string on = opts ? opts->get_or<std::string>("on", "press") : std::string("press");
        if (!BreakpointManager::ParseKeyboardInto(key, on, spec, error) ||
            (opts && !BreakpointManager::ParseHitSpec(opts->get_or<std::string>("hits", ""), spec.hitMode,
                                                      spec.hitTarget, error)))
            return fail(error);
        if (opts)
        {
            spec.group = opts->get_or<std::string>("group", "");
            spec.note = opts->get_or<std::string>("note", "");
        }
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        if (id == BRK_INVALID)
            return fail(error);
        return std::make_tuple(sol::make_object(ts, id), sol::make_object(ts, sol::lua_nil));
    });

    // bp_reset_hits([id][, cpu]): hit counters back to 0 and per-hit records emptied (one breakpoint, or all)
    lua.set_function("bp_reset_hits", [this](sol::optional<uint16_t> id, sol::optional<std::string> cpu) -> bool {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        if (!bpm) return false;
        if (id)
            return bpm->ResetHitCount(*id);
        bpm->ResetAllHitCounts();
        return true;
    });

    lua.set_function("bp_count", [this](sol::optional<std::string> cpu) -> size_t {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        return bpm ? bpm->GetBreakpointsCount() : 0;
    });

    // bp_groups([cpu]) -> {ok, instance, cpu, groups = {{name, total, active, ids}}}: as GET .../breakpoints/groups
    lua.set_function("bp_groups", [this](sol::this_state s, sol::optional<std::string> cpu) {
        StateNode args = StateNode::Object();
        if (Emulator* emulator = effectiveEmulator())
            args["instance"] = emulator->GetId();
        args["cpu"] = cpu.value_or("");
        return StateNodeToLua(s, monitoring::Call("breakpoint_groups", args).body);
    });

    // bp_hit_log(id[, {cpu=, from=, limit=}]) -> {ok, instance, cpu, id, stop, hit_count, log, hits = {{index, hit,
    // frame, t, pc, address, value, kind, cpu_time}}, first_index, more, next_from}: as GET .../breakpoints/{bp}/hits;
    // a refusal: ok = false, error, message
    lua.set_function("bp_hit_log", [this](sol::this_state s, int64_t id, sol::optional<sol::table> opts) {
        StateNode args = StateNode::Object();
        if (Emulator* emulator = effectiveEmulator())
            args["instance"] = emulator->GetId();
        args["id"] = std::to_string(id);
        if (opts)
        {
            args["cpu"] = opts->get_or<std::string>("cpu", "");
            if (auto from = opts->get<sol::optional<int64_t>>("from"))
                args["from"] = std::to_string(*from);
            if (auto limit = opts->get<sol::optional<int64_t>>("limit"))
                args["limit"] = std::to_string(*limit);
        }
        return StateNodeToLua(s, monitoring::Call("breakpoint_hits", args).body);
    });

    lua.set_function("bp_list", [this](sol::optional<std::string> cpu) -> std::string {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        return bpm ? bpm->GetBreakpointListAsString() : "";
    });

    // bp_hits(id[, cpu]): the breakpoint's hit count (a count-only one, stop = false, counts and never stops); -1
    // for an unknown id
    lua.set_function("bp_hits", [this](uint16_t id, sol::optional<std::string> cpu) -> int64_t {
        BreakpointManager* bpm = ScriptBreakpoints(cpu.value_or(""));
        const BreakpointSnapshot bp = bpm ? bpm->GetBreakpointById(id) : BreakpointSnapshot();
        return bp ? static_cast<int64_t>(bp->hitCount) : -1;
    });

    lua.set_function("bp_status", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        if (!effectiveEmulator()) {
            result["valid"] = false;
            return result;
        }
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pDebugManager) {
            result["valid"] = false;
            return result;
        }
        BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
        if (!bpm) {
            result["valid"] = false;
            return result;
        }
        auto info = bpm->GetLastTriggeredBreakpointInfo();
        result["valid"] = info.valid;
        if (info.valid) {
            result["id"] = info.id;
            result["type"] = info.type;
            result["address"] = info.address;
            result["access"] = info.access;
            result["active"] = info.active;
            result["note"] = info.note;
            result["group"] = info.group;
            if (!info.pageKind.empty())
                result["page"] = lua_view.create_table_with("kind", info.pageKind, "page", info.pageNumber);
            result["hit_count"] = info.hitCount;
            if (!info.condition.empty())
            {
                result["condition"] = info.condition;
                result["match_count"] = info.matchCount;
            }
            if (!info.conditionError.empty())
                result["condition_error"] = info.conditionError;
        }
        return result;
    });

    lua.set_function("bp_clear_last", [this]() {
        if (!effectiveEmulator()) return;
        auto* ctx = effectiveEmulator()->GetContext();
        if (ctx && ctx->pDebugManager) {
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            if (bpm) bpm->ClearLastTriggeredBreakpoint();
        }
    });
}
