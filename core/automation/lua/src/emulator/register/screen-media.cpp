// LuaEmulator bindings: screen captures, memory regions, slots and media.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterScreenAndMedia(sol::state& lua)
{
    // screenshot([{area = "full"|"screen", format = "png"|"gif", source = "presented"|"live"|"window", path = "file"}]): a
    // screenshot (core Screenshotter): the whole frame (default) or the working picture, PNG (default) or GIF,
    // of the presented frame (default) or the live one as drawn now (a paused machine adds frame.partial and
    // frame.beam = {line, tstate}), or the picture as the unreal-qt window draws it ("window": GUI only, area full;
    // adds window = {viewport, picture, device_pixel_ratio, backend}).
    // Returns {format, area, width, height, size, crop = {x,y,width,height}, screen_window = {...}, frame = {width,
    // height, mode, source, frame_number}, data = the encoded image as a string of bytes} or, with a path, `file`
    // instead of `data`; nil, error on a bad word, a missing emulator or no frame
    lua.set_function("screenshot", [this](sol::this_state s, sol::optional<sol::table> opts) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        EmulatorContext* context = emulator->GetContext();
        if (!context || !context->pScreen) return mouseError(s, "The emulator has no screen");

        ScreenshotOptions options;  // the whole frame, PNG
        if (opts)
        {
            const std::string area = opts->get_or<std::string>("area", "");
            const std::string format = opts->get_or<std::string>("format", "");
            const std::string source = opts->get_or<std::string>("source", "");
            if (!area.empty() && !Screenshotter::ParseArea(area, options.area))
                return mouseError(s, "Unknown area '" + area + "': use full or screen");
            if (!source.empty() && !Screenshotter::ParseSource(source, options.source))
                return mouseError(s, "Unknown source '" + source + "': use presented, live or window");
            if (!format.empty() && !Screenshotter::ParseFormat(format, options.format))
                return mouseError(s, "Unknown format '" + format + "': use png or gif");
            options.saveTo = opts->get_or<std::string>("path", "");
        }

        const ScreenshotResult shot = Screenshotter::TakeFrom(*context->pScreen, options, emulator->IsEmulationParked());
        if (!shot.ok) return mouseError(s, shot.errorMessage);

        sol::state_view view(s);
        auto rect = [&view](const PictureRect& r) {
            sol::table t = view.create_table();
            t["x"] = r.x;
            t["y"] = r.y;
            t["width"] = r.width;
            t["height"] = r.height;
            return t;
        };
        sol::table t = view.create_table();
        t["format"] = Screenshotter::FormatName(shot.format);
        t["area"] = Screenshotter::AreaName(options.area);
        t["source"] = Screenshotter::RequestSourceName(options.source);
        t["width"] = shot.width;
        t["height"] = shot.height;
        t["size"] = shot.encodedSize;
        t["crop"] = rect(shot.crop);
        t["screen_window"] = rect(shot.frame.screenWindow);
        sol::table frame = view.create_table();
        frame["width"] = shot.frame.width;
        frame["height"] = shot.frame.height;
        frame["mode"] = shot.frame.source == FrameSource::External ? std::string("external")
                                                                   : Screen::GetVideoModeName(shot.frame.videoMode);
        frame["source"] = Screenshotter::SourceName(shot.frame.source);
        frame["frame_number"] = shot.frame.frameNumber;
        if (shot.frame.beamLine >= 0)
        {
            frame["partial"] = shot.frame.partial;
            sol::table beam = view.create_table();
            beam["line"] = shot.frame.beamLine;
            beam["tstate"] = shot.frame.beamTstate;
            frame["beam"] = beam;
        }
        t["frame"] = frame;
        if (shot.fromWindow)
        {
            sol::table window = view.create_table();
            window["viewport"] = rect(shot.window.viewport);
            window["picture"] = rect(shot.window.picture);
            window["device_pixel_ratio"] = shot.window.devicePixelRatio;
            window["backend"] = shot.window.backend;
            t["window"] = window;
        }
        if (!shot.savedFile.empty())
            t["file"] = shot.savedFile;
        else
            t["data"] = std::string(shot.bytes.begin(), shot.bytes.end());
        sol::variadic_results out;
        out.push_back(t);
        return out;
    });
    // capture_screen_layer(layer [, {format = "png"|"rgba", alpha = true|false, path = "file"}]): ONE LAYER of the last completed frame of a ZX
    // Spectrum Next (design-layer-capture.md): "ula" (with the border and LoRes), "layer2", "tilemap", "sprites" - alpha 0 where the layer
    // does not paint - or "ids" (every pixel coloured by the layer that won it). 640 x 512, the grid of screenshot()'s 720 x 576 frame at (32, 32). Returns
    // {layer, format, alpha, width, height, size, frame = {sequence, frame_number}, data = the PNG (or the raw RGBA bytes) as a string,
    // or `file`, legend = {{id, name, colour}...} for ids}; nil, error when the machine is not a Next, the capture is off
    // (next_layer_capture_control{enabled = true}) or no frame was completed yet
    lua.set_function("capture_screen_layer", [this](sol::this_state s, const std::string& layer, sol::optional<sol::table> opts) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string format, alpha, path, size;
        if (opts)
        {
            format = opts->get_or<std::string>("format", "");
            size = opts->get_or<std::string>("size", "");
            path = opts->get_or<std::string>("path", "");
            sol::object a = (*opts)["alpha"];
            if (a.get_type() == sol::type::boolean)
                alpha = a.as<bool>() ? "true" : "false";
        }
        const NextLayer::Result shot = NextLayer::Request(emulator->GetContext(), layer, format, alpha, path, size);
        if (shot.status != NextLayer::Status::Ok) return mouseError(s, shot.message);
        sol::state_view view(s);
        sol::table t = view.create_table();
        t["layer"] = NextLayer::KindName(shot.kind);
        t["format"] = shot.rgba ? "rgba" : "png";
        t["alpha"] = shot.alpha;
        t["size_name"] = NextLayer::ScaleName(shot.scale);
        t["width"] = shot.width;
        t["height"] = shot.height;
        t["size"] = shot.size;
        sol::table frame = view.create_table();
        frame["sequence"] = shot.sequence;
        frame["frame_number"] = shot.frameCounter;
        t["frame"] = frame;
        if (shot.kind == NextLayer::Kind::Ids)
        {
            sol::table legend = view.create_table();
            for (unsigned id = 0; id < NextLayer::kIdCount; id++)
            {
                sol::table entry = view.create_table();
                const uint32_t c = NextLayer::IdColour(id);
                char colour[8];
                std::snprintf(colour, sizeof colour, "#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
                entry["id"] = id;
                entry["name"] = NextLayer::IdName(id);
                entry["colour"] = std::string(colour);
                legend[id + 1] = entry;
            }
            t["legend"] = legend;
        }
        if (!shot.savedFile.empty())
            t["file"] = shot.savedFile;
        else
            t["data"] = std::string(shot.bytes.begin(), shot.bytes.end());
        sol::variadic_results out;
        out.push_back(t);
        return out;
    });
    // next_layer_capture_control([{enabled = true|false}]): switch the per-layer record of the Next screen (on by default; takes effect at
    // the next frame start); returns {available, enabled, frames, last_frame, bytes}. No argument: only report
    lua.set_function("next_layer_capture_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        int enable = -1;
        if (options)
        {
            sol::object e = (*options)["enabled"];
            if (e.get_type() == sol::type::boolean)
                enable = e.as<bool>() ? 1 : 0;
        }
        return StateNodeToLua(s, DeviceState::NextLayerCaptureControl(emulator->GetContext(), enable));
    });
    lua.set_function("memory_regions", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::MemoryRegions(emulator->GetContext()));
    });
    lua.set_function("region_read", [this](sol::this_state s, const std::string& name, uint32_t offset,
                                            sol::optional<uint32_t> length) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::vector<uint8_t> bytes;
        std::string error;
        if (!DeviceMemory::Read(emulator->GetContext(), name, offset, length.value_or(256), bytes, error))
            return mouseError(s, error);
        sol::state_view view(s);
        sol::table data = view.create_table(static_cast<int>(bytes.size()), 0);
        for (size_t i = 0; i < bytes.size(); i++)
            data[i + 1] = bytes[i];
        sol::variadic_results out;
        out.push_back(data);
        return out;
    });
    lua.set_function("region_write", [this](sol::this_state s, const std::string& name, uint32_t offset,
                                             sol::object data) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::vector<uint8_t> bytes;
        if (data.get_type() == sol::type::string)
        {
            if (!DeviceMemory::ParseHexBytes(data.as<std::string>(), bytes))
                return mouseError(s, "data: a table of bytes or a hex string (\"0000A8\")");
        }
        else if (data.get_type() == sol::type::table)
        {
            sol::table t = data.as<sol::table>();
            for (size_t i = 1; i <= t.size(); i++)
                bytes.push_back(static_cast<uint8_t>(t.get<int>(i) & 0xFF));
        }
        else
            return mouseError(s, "data: a table of bytes or a hex string");
        std::string error;
        if (!DeviceMemory::Write(emulator->GetContext(), name, offset, bytes, "Lua region write", error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(sol::make_object(s, true));
        return out;
    });
    lua.set_function("region_save", [this](sol::this_state s, const std::string& name, const std::string& path,
                                            sol::optional<uint32_t> offset, sol::optional<uint32_t> length) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string error;
        if (!DeviceMemory::Save(emulator->GetContext(), name, path, offset.value_or(0), length.value_or(0), error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(sol::make_object(s, true));
        return out;
    });
    lua.set_function("region_load", [this](sol::this_state s, const std::string& name, const std::string& path,
                                            sol::optional<uint32_t> offset) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string error;
        size_t written = 0;
        if (!DeviceMemory::Load(emulator->GetContext(), name, path, offset.value_or(0), written, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(sol::make_object(s, static_cast<double>(written)));
        return out;
    });

    lua.set_function("memory_info", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table info = lua_view.create_table();
        if (!effectiveEmulator()) return info;
        Memory* mem = effectiveEmulator()->GetMemory();
        if (!mem) return info;
        
        sol::table pages = lua_view.create_table();
        pages["ram_count"] = MAX_RAM_PAGES;
        pages["rom_count"] = MAX_ROM_PAGES;
        pages["cache_count"] = MAX_CACHE_PAGES;
        pages["misc_count"] = MAX_MISC_PAGES;
        info["pages"] = pages;
        
        sol::table banks = lua_view.create_table();
        for (int bank = 0; bank < 4; bank++) {
            sol::table bankInfo = lua_view.create_table();
            bankInfo["bank"] = bank;
            bankInfo["start"] = bank * 0x4000;
            bankInfo["end"] = (bank + 1) * 0x4000 - 1;
            bankInfo["mapping"] = mem->GetCurrentBankName(bank);
            banks[bank + 1] = bankInfo;
        }
        info["z80_banks"] = banks;
        return info;
    });

    // Feature management (using correct FeatureManager API)
    // Same listFeatures() enumeration the CLI `feature` table and the
    // WebAPI /features endpoint use — keyed by feature id, so scripts
    // keep working as new features (fasttape, turbotape, …) register
    lua.set_function("feature_list", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table features = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (emulator) {
            FeatureManager* fm = emulator->GetFeatureManager();
            if (fm) {
                for (const FeatureManager::FeatureInfo& feature : fm->listFeatures())
                    features[feature.id] = feature.enabled;
            }
        }
        return features;
    });

    lua.set_function("feature_get", [this](const std::string& name) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        FeatureManager* fm = emulator->GetFeatureManager();
        return fm ? fm->isEnabled(name) : false;
    });

    // ok, reason: the reason says why a known feature was refused (TTD holds it)
    lua.set_function("feature_set", [this](const std::string& name, bool enabled) -> std::tuple<bool, std::string> {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return {false, "no emulator"};
        FeatureManager* fm = emulator->GetFeatureManager();
        if (!fm) return {false, "FeatureManager not available"};
        if (fm->setFeature(name, enabled)) return {true, ""};
        return {false, fm->hasFeature(name) ? fm->refusalReason(name, enabled) : "unknown feature: " + name};
    });

    // Disk inspection functions
    lua.set_function("disk_is_inserted", [this](int drive) -> bool {
        if (!effectiveEmulator() || drive < 0 || drive > 3) return false;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
        return ctx->coreState.diskDrives[drive]->isDiskInserted();
    });

    // region <ZX-bus slots through SlotControl (ZX-bus slots architecture.md §9)>
    // slots_state(), slots_catalog([slot]), slots_matrix([table]),
    // slots_plug(slot, card [, opts]), slots_remove(slot [, opts]), slots_set(slot, opts), slots_gs(card [, opts]).
    // slot: zxbus.1, zxbus.next, ay-socket ("" or "auto" for plug: the planner's choice).
    // opts: {options = "dip=ym,saa" | {dip = "ym,saa"}, replace = true, dry_run = true, media = "save" | "discard",
    //        adapter = "..."}; any other key is a card option (slots_set(slot, {dip = "ym,gs"})).
    // Each returns the reply table every surface returns: ok, status, message and the verb's fields (the report,
    // cards, tables; for a change the plan, restart = {restarted, emulatorId, ...}, media). A change restarts
    // the machine: a script bound to the old instance follows it to the new one
    lua.set_function("slots_state", [this](sol::this_state s) {
        SlotControlRequest request;
        request.verb = "list";
        return StateNodeToLua(s, slotsCall(request, sol::nullopt, false).ToValue());
    });
    lua.set_function("slots_catalog", [this](sol::this_state s, sol::optional<std::string> slot) {
        SlotControlRequest request;
        request.verb = "catalog";
        request.slot = slot.value_or("");  // every card planned into that slot
        return StateNodeToLua(s, slotsCall(request, sol::nullopt, false).ToValue());
    });
    lua.set_function("slots_matrix", [this](sol::this_state s, sol::optional<std::string> table) {
        SlotControlRequest request;
        request.verb = "matrix";
        request.table = table.value_or("");
        return StateNodeToLua(s, slotsCall(request, sol::nullopt, false).ToValue());
    });
    lua.set_function("slots_plug", [this](sol::this_state s, const std::string& slot, const std::string& card,
                                          sol::optional<sol::table> opts) {
        SlotControlRequest request;
        request.verb = "plug";
        request.slot = slot == "auto" ? std::string() : slot;
        request.card = card;
        return StateNodeToLua(s, slotsCall(request, opts, true).ToValue());
    });
    lua.set_function("slots_remove", [this](sol::this_state s, const std::string& slot, sol::optional<sol::table> opts) {
        SlotControlRequest request;
        request.verb = "remove";
        request.slot = slot;
        return StateNodeToLua(s, slotsCall(request, opts, false).ToValue());
    });
    lua.set_function("slots_set", [this](sol::this_state s, const std::string& slot, sol::optional<sol::table> opts) {
        SlotControlRequest request;
        request.verb = "set";
        request.slot = slot;
        return StateNodeToLua(s, slotsCall(request, opts, true).ToValue());
    });
    lua.set_function("slots_gs", [this](sol::this_state s, const std::string& card, sol::optional<sol::table> opts) {
        SlotControlRequest request;
        request.verb = "gs";
        request.card = card;
        return StateNodeToLua(s, slotsCall(request, opts, false).ToValue());
    });
    // endregion </ZX-bus slots>

    // region <Media: every slot through MediaControl (media-control-design.md)>
    // media_list(), media_info(slot), media_formats([kind]),
    // media_insert(slot, path [, opts]), media_swap(slot, path [, opts]),
    // media_eject(slot [, opts]), media_save(slot [, path] [, opts]),
    // media_export(slot, path), media_discard(slot [, opts]),
    // media_rescan(slot [, opts]), media_create(slot [, opts]),
    // media_protect(slot, on), media_compose(descriptor [, opts]), media_layers(slot),
    // media_volumes(slot), media_files(slot [, dir, opts]), media_read(slot, file [, opts]),
    // media_read_bytes(slot, file [, volume]), media_write(slot, file, bytes [, opts]) (bytes nil: opts["in"]
    // names a host file), media_delete(slot, file [, opts]), media_rename(slot, file, to [, opts]),
    // media_mkdir(slot, dir [, opts]), media_attrib(slot, file, opts), media_label(slot, name [, opts]),
    // and media(verb, slot, path, opts).
    // slot: fdd.b, B, b:, sd, floppy:1, tag:sd+neogs ("auto" for insert).
    // opts: {access="readonly", save=true, export="x.trd", discard=true, async=true, ...}.
    // Each returns the reply table every surface returns: ok, error, message,
    // slot, pending, revision, report, and the verb's fields (slots, info, ...)
    auto mediaCall = [this](sol::this_state s, const std::string& verb, const std::string& selector,
                            const std::string& path, sol::optional<sol::table> opts) -> sol::object {
        MediaRequest request;
        request.verb = verb;
        request.selector = selector;
        request.path = path;
        if (opts)
        {
            for (const auto& [key, value] : *opts)
            {
                if (!key.is<std::string>())
                    continue;
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
        return StateNodeToLua(s, MediaControl(emulator->GetContext()).Execute(request).ToValue());
    };
    // controllers(verb, [connector], [device], [{options}]) -> the reply table every surface returns: the machine's
    // joystick connectors (list, get, plug, unplug, options, targets, auto, input; ControllerControl, as the
    // CLI `controllers`, WebAPI /controllers and Python controllers_*)
    lua.set_function("controllers", [this](sol::this_state s, const std::string& verb, sol::optional<std::string> connector,
                                           sol::optional<std::string> device, sol::optional<sol::table> opts) {
        controllers::ControllerRequest request;
        request.verb = verb;
        request.selector = connector.value_or("");
        request.device = device.value_or("");
        if (opts)
            for (const auto& [key, value] : *opts)
            {
                if (!key.is<std::string>())
                    continue;
                std::string text;
                if (value.is<bool>())
                    text = value.as<bool>() ? "true" : "false";
                else if (value.get_type() == sol::type::number)
                    text = std::to_string(value.as<long long>());
                else if (value.is<std::string>())
                    text = value.as<std::string>();
                request.options[key.as<std::string>()] = text;
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
        return StateNodeToLua(s, controllers::ControllerControl(emulator->GetContext()).Execute(request).ToValue());
    });
    // actions() -> {instance, actions = {{id, label, group, kind, held, scope, needs_pause, available, error,
    // reason}, ...}}; action(id, [value], [params]) -> {ok, action, error, message, result}: the core
    // ActionRegistry (control surfaces actions.md §6), as the CLI `action`, WebAPI /controllers/actions, MCP
    // `actions` and Python actions() / action(). value by the action's kind: nil (a press), true / false, a number
    // (delta steps or a 0..1 level), a string (text), or a table {pressed, delta, rate, level, text}. A changing
    // action passes the script gate as the script's own control calls (an implicit session without one)
    lua.set_function("actions", [this](sol::this_state s) {
        Emulator* emulator = effectiveEmulator();
        const std::string instance = emulator ? emulator->GetId() : std::string();
        actions::ActionOrigin origin;
        origin.origin = execution::Origin::Lua;
        origin.identity = "lua|script";
        StateNode body = StateNode::Object();
        body["instance"] = instance;
        body["rev"] = static_cast<int64_t>(actions::ActionRegistry::Instance().Revision());
        StateNode& rows = body["actions"];
        rows = StateNode::Array();
        for (const actions::ActionStatus& status : actions::ActionRegistry::Instance().Statuses(instance, origin))
        {
            StateNode row = StateNode::Object();
            row["id"] = status.info.id;
            row["label"] = status.info.label;
            row["group"] = status.info.group;
            row["kind"] = actions::ValueKindName(status.info.kind);
            row["held"] = status.info.held;
            row["scope"] = automation_gate::CompanionScopeName(status.scope);
            row["needs_pause"] = status.needsPause;
            row["toggle"] = status.info.toggle;
            row["description"] = status.info.description;
            if (!status.info.params.empty())
                row["params"] = status.info.params;
            row["available"] = status.available;
            if (!status.available)
            {
                row["error"] = status.error;
                row["reason"] = status.reason;
            }
            rows.push(std::move(row));
        }
        return StateNodeToLua(s, body);
    });
    lua.set_function("action", [this](sol::this_state s, const std::string& id, sol::optional<sol::object> rawValue,
                                      sol::optional<sol::table> params) {
        auto fail = [&s, &id](const std::string& error, const std::string& message) {
            StateNode out = StateNode::Object();
            out["ok"] = false;
            out["action"] = id;
            out["error"] = error;
            out["message"] = message;
            return StateNodeToLua(s, out);
        };
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return fail("Not Found", "no emulator selected");
        actions::ActionRegistry& registry = actions::ActionRegistry::Instance();
        const std::optional<actions::ActionInfo> info = registry.Find(id);
        if (!info)
            return fail("Not Found", "unknown action '" + id + "'");
        auto toNode = [](const sol::object& value) -> StateNode {
            if (value.is<bool>())
                return StateNode(value.as<bool>());
            if (value.get_type() == sol::type::number)
            {
                const double number = value.as<double>();
                return number == std::floor(number) ? StateNode(static_cast<int64_t>(number)) : StateNode(number);
            }
            if (value.is<std::string>())
                return StateNode(value.as<std::string>());
            return StateNode();
        };
        actions::ActionValue value;
        value.kind = info->kind;
        std::string error;
        if (rawValue && rawValue->valid() && rawValue->get_type() != sol::type::lua_nil)
        {
            const sol::object& raw = *rawValue;
            if (raw.is<bool>())
            {
                value.pressed = raw.as<bool>();
                value.level = value.pressed ? 1.0 : 0.0;
            }
            else if (raw.get_type() == sol::type::number)
            {
                value.delta = raw.as<double>();
                value.level = std::clamp(raw.as<double>(), 0.0, 1.0);
                value.pressed = value.level > 0;
            }
            else if (raw.is<std::string>())
            {
                if (!actions::ValueFromText(info->kind, raw.as<std::string>(), value, error))
                    return fail("Bad Request", error);
            }
            else if (raw.get_type() == sol::type::table)
            {
                const sol::table table = raw.as<sol::table>();
                value.pressed = table.get_or("pressed", true);
                value.delta = table.get_or("delta", 0.0);
                value.rate = table.get_or("rate", 0.0);
                value.level = std::clamp(table.get_or("level", 0.0), 0.0, 1.0);
                value.text = table.get_or<std::string>("text", "");
            }
            else
                return fail("Bad Request", "value: nil, a boolean, a number, a string or a table");
        }
        if (params)
            for (const auto& [key, item] : *params)
                if (key.is<std::string>())
                    value.params[key.as<std::string>()] = toNode(item);
        actions::ActionOrigin origin;
        origin.origin = execution::Origin::Lua;
        origin.identity = "lua|script";
        const actions::ActionResult result = registry.Run(id, std::move(value), emulator->GetId(), origin);
        StateNode out = StateNode::Object();
        out["ok"] = result.ok;
        out["action"] = id;
        if (!result.ok)
            out["error"] = result.error;
        out["message"] = result.message;
        out["result"] = result.body;
        return StateNodeToLua(s, out);
    });
    // context() -> {instance, context, set, by, default}: the instance's surface context (actions::ContextState,
    // control surfaces architecture §6), as the CLI `action context`, WebAPI GET /api/v1/controllers/context, MCP
    // `actions` context and Python context(); set it with action("ctx.set", "ttd") ("" returns to the default)
    lua.set_function("context", [this](sol::this_state s) {
        Emulator* emulator = effectiveEmulator();
        StateNode out = StateNode::Object();
        if (!emulator)
        {
            out["ok"] = false;
            out["error"] = "Not Found";
            out["message"] = "no emulator selected";
            return StateNodeToLua(s, out);
        }
        const actions::ContextInfo info = actions::ContextState::Instance().Get(emulator->GetId(), emulator);
        out["ok"] = true;
        out["instance"] = emulator->GetId();
        out["context"] = info.context;
        out["set"] = info.set;
        out["by"] = info.by;
        out["default"] = info.fallback;
        return StateNodeToLua(s, out);
    });
    // surfaces(verb, {args}) -> the reply table of a control surfaces verb (actions::surfaces::Call: views, target,
    // sessions, session, pin, readouts, activity, view_add, view_update, view_remove, session_open,
    // session_close), as WebAPI /api/v1/surfaces/..., the CLI `surface`, MCP `actions` and Python surfaces()
    lua.set_function("surfaces", [](sol::this_state s, sol::optional<std::string> verb, sol::optional<sol::table> opts) {
        std::function<StateNode(const sol::object&)> toNode = [&toNode](const sol::object& value) -> StateNode {
            if (value.is<bool>())
                return StateNode(value.as<bool>());
            if (value.get_type() == sol::type::number)
            {
                const double number = value.as<double>();
                return number == std::floor(number) ? StateNode(static_cast<int64_t>(number)) : StateNode(number);
            }
            if (value.is<std::string>())
                return StateNode(value.as<std::string>());
            if (value.get_type() == sol::type::table)
            {
                const sol::table table = value.as<sol::table>();
                // A sequence is a list ({"run.state", "media.*"}), anything else an object ({view = "v1"})
                if (table.size() > 0)
                {
                    StateNode array = StateNode::Array();
                    for (size_t i = 1; i <= table.size(); i++)
                        array.push(toNode(table[i]));
                    return array;
                }
                StateNode object = StateNode::Object();
                for (const auto& [key, item] : table)
                    if (key.is<std::string>())
                        object[key.as<std::string>()] = toNode(item);
                return object;
            }
            return StateNode();
        };
        StateNode args = StateNode::Object();
        if (opts)
            for (const auto& [key, item] : *opts)
                if (key.is<std::string>())
                    args[key.as<std::string>()] = toNode(item);
        return StateNodeToLua(s, actions::surfaces::Call(verb.value_or("views"), args).body);
    });
    // streamdeck(verb, {args}) -> the reply table of a Stream Deck daemon verb (companion::streamdeck::Call:
    // status, start, stop, restart, settings, pairing, pair, pair_cancel, revoke, brightness, packs, pack, udev),
    // as WebAPI /api/v1/streamdeck/..., the CLI `streamdeck`, MCP `streamdeck` and Python streamdeck():
    // streamdeck("pack", {deck = "A1B2C3", pack = "os4"})
    lua.set_function("streamdeck", [](sol::this_state s, sol::optional<std::string> verb, sol::optional<sol::table> opts) {
        StateNode args = StateNode::Object();
        if (opts)
            for (const auto& [key, item] : *opts)
            {
                if (!key.is<std::string>())
                    continue;
                const std::string name = key.as<std::string>();
                if (item.is<bool>())
                    args[name] = StateNode(item.as<bool>());
                else if (item.get_type() == sol::type::number)
                {
                    const double number = item.as<double>();
                    args[name] = number == std::floor(number) ? StateNode(static_cast<int64_t>(number))
                                                              : StateNode(number);
                }
                else if (item.is<std::string>())
                    args[name] = StateNode(item.as<std::string>());
            }
        return StateNodeToLua(s, companion::streamdeck::Call(verb.value_or("status"), args).body);
    });
    // monitor(name, {instance = }) -> the reply table of a monitoring verb (monitoring::Call: all, host.stats,
    // host.clients, video.present, emulator.stats, debug.breakpoints, media.slots, recording.state, debug.labels, ttd.status,
    // debug.analyzers, controllers.state, game.events, devices.access; an instance's name defaults to this emulator), as WebAPI
    // /api/v1/monitor[/{name}], the CLI `monitor`, MCP inspect_state aspect `monitor` and Python monitor()
    lua.set_function("monitor", [this](sol::this_state s, sol::optional<std::string> verb, sol::optional<sol::table> opts) {
        Emulator* emulator = effectiveEmulator();
        StateNode args = StateNode::Object();
        if (opts)
            for (const auto& [key, item] : *opts)
            {
                if (!key.is<std::string>())
                    continue;
                const std::string name = key.as<std::string>();
                if (item.get_type() == sol::type::number)
                {
                    const double number = item.as<double>();
                    args[name] = number == std::floor(number) ? StateNode(static_cast<int64_t>(number))
                                                              : StateNode(number);
                }
                else if (item.is<std::string>())
                    args[name] = StateNode(item.as<std::string>());
            }
        const std::string name = verb.value_or("all");
        if (args.find("instance") == nullptr && name != "all" && emulator != nullptr)
            args["instance"] = emulator->GetId();
        return StateNodeToLua(s, monitoring::Call(name, args).body);
    });
    // controllers_host(verb, {options}) -> the reply table every surface returns: the host game controllers
    // (devices, profiles, bindings, haptics, rumble, light; HostInputControl, the process's, not the machine's)
    auto hostControllersCall = [](sol::this_state s, const std::string& verb, sol::optional<sol::table> opts) {
#ifdef ENABLE_CONTROLLERS
        controllers::HostRequest request;
        request.verb = verb;
        if (opts)
            for (const auto& [key, value] : *opts)
            {
                if (!key.is<std::string>())
                    continue;
                std::string text;
                if (value.is<bool>())
                    text = value.as<bool>() ? "true" : "false";
                else if (value.get_type() == sol::type::number)
                    text = std::to_string(value.as<long long>());
                else if (value.is<std::string>())
                    text = value.as<std::string>();
                request.options[key.as<std::string>()] = text;
            }
        return JsonValueToLua(s, controllers::HostInputControl::Process(request).ToValue());
#else
        (void)verb;
        (void)opts;
        StateNode reply = StateNode::Object();
        reply["ok"] = false;
        reply["error"] = "unavailable";
        reply["message"] = "built without host game controllers (ENABLE_CONTROLLERS=OFF)";
        return StateNodeToLua(s, reply);
#endif
    };
    lua.set_function("controllers_host", hostControllersCall);
    // controllers_haptics([device]) -> devices[]: available or reason, actuators, the test effects
    lua.set_function("controllers_haptics", [hostControllersCall](sol::this_state s, sol::optional<std::string> device) {
        sol::table opts = sol::state_view(s).create_table();
        if (device && !device->empty())
            opts["device"] = *device;
        return hostControllersCall(s, "haptics", opts);
    });
    // controllers_rumble(device, [effect]) -> play a test effect (identify, low, high, clicks, left-trigger,
    // right-trigger, swell)
    lua.set_function("controllers_rumble", [hostControllersCall](sol::this_state s, const std::string& device,
                                                                 sol::optional<std::string> effect) {
        sol::table opts = sol::state_view(s).create_table();
        opts["device"] = device;
        opts["effect"] = effect.value_or("identify");
        return hostControllersCall(s, "rumble", opts);
    });
    // controllers_output(device, pattern | {steps=, clicks=, marks=}) -> a control surface's outputs (the Surface
    // Dial): a haptic pattern (tick, double, buzz), its steps per turn and clicks, or a seek's marks as its layer
    // plays them
    lua.set_function("controllers_output", [hostControllersCall](sol::this_state s, const std::string& device,
                                                                 sol::object what) {
        sol::table opts = sol::state_view(s).create_table();
        if (what.is<sol::table>())
            for (const auto& [key, value] : what.as<sol::table>())
                opts[key] = value;
        else if (what.is<std::string>())
            opts["pattern"] = what.as<std::string>();
        opts["device"] = device;
        return hostControllersCall(s, "output", opts);
    });
    // controllers_light(device, color) -> a pad's light colour (#RRGGBB, r,g,b or off: a DualSense / DualShock 4
    // light bar)
    lua.set_function("controllers_light", [hostControllersCall](sol::this_state s, const std::string& device,
                                                                const std::string& color) {
        sol::table opts = sol::state_view(s).create_table();
        opts["device"] = device;
        opts["color"] = color;
        return hostControllersCall(s, "light", opts);
    });
    lua.set_function("media", [mediaCall](sol::this_state s, const std::string& verb, sol::optional<std::string> slot,
                                          sol::optional<std::string> path, sol::optional<sol::table> opts) {
        return mediaCall(s, verb, slot.value_or(""), path.value_or(""), opts);
    });
    lua.set_function("media_list", [mediaCall](sol::this_state s) { return mediaCall(s, "list", "", "", sol::nullopt); });
    lua.set_function("media_info", [mediaCall](sol::this_state s, const std::string& slot) {
        return mediaCall(s, "info", slot, "", sol::nullopt);
    });
    lua.set_function("media_formats", [mediaCall](sol::this_state s, sol::optional<std::string> kind) {
        sol::state_view view(s);
        sol::optional<sol::table> opts;
        if (kind)
        {
            sol::table t = view.create_table();
            t["kind"] = *kind;
            opts = t;
        }
        return mediaCall(s, "formats", "", "", opts);
    });
    // Where a file can go: what it is, the slots that take it (in a chooser's order), the default, the refusal
    lua.set_function("media_targets", [mediaCall](sol::this_state s, const std::string& path) {
        return mediaCall(s, "targets", "", path, sol::nullopt);
    });
    // A composition descriptor (*.ucompose.yaml, or its JSON text) built without inserting it
    lua.set_function("media_compose", [mediaCall](sol::this_state s, const std::string& descriptor,
                                                  sol::optional<sol::table> opts) {
        return mediaCall(s, "compose", "", descriptor, opts);
    });
    lua.set_function("media_layers", [mediaCall](sol::this_state s, const std::string& slot) {
        return mediaCall(s, "layers", slot, "", sol::nullopt);
    });
    // The guest's unsaved writes as file operations, with the layer each touched
    lua.set_function("media_changes", [mediaCall](sol::this_state s, const std::string& slot) {
        return mediaCall(s, "changes", slot, "", sol::nullopt);
    });
    // The files on a slot's disk (DiskFs): TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02 / MB-01, MDOS, FAT (DSS, NedoOS); write, delete, rename, mkdir
    lua.set_function("media_volumes", [mediaCall](sol::this_state s, const std::string& slot) {
        return mediaCall(s, "volumes", slot, "", sol::nullopt);
    });
    lua.set_function("media_files", [mediaCall](sol::this_state s, const std::string& slot, sol::optional<std::string> path,
                                                sol::optional<sol::table> opts) {
        return mediaCall(s, "files", slot, path.value_or(""), opts);
    });
    lua.set_function("media_read", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                               sol::optional<sol::table> opts) {
        return mediaCall(s, "read", slot, path, opts);
    });
    // A file's bytes as a Lua string, or nil and the reason
    lua.set_function("media_read_bytes", [this](sol::this_state s, const std::string& slot, const std::string& path,
                                                sol::optional<std::string> volume) -> std::tuple<sol::object, sol::object> {
        sol::state_view view(s);
        MediaRequest request{"read", slot, path, {}};
        if (volume && !volume->empty())
            request.options["volume"] = *volume;
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return {sol::make_object(view, sol::lua_nil), sol::make_object(view, "no emulator selected")};
        const MediaReply reply = MediaControl(emulator->GetContext()).Execute(request);
        if (!reply.result.Ok())
            return {sol::make_object(view, sol::lua_nil), sol::make_object(view, reply.result.message)};
        std::vector<uint8_t> bytes;
        base64::Decode(reply.body.find("data")->s, bytes);
        return {sol::make_object(view, std::string(bytes.begin(), bytes.end())), sol::make_object(view, sol::lua_nil)};
    });
    // A file onto a slot's disk (TR-DOS, +3DOS, iS-DOS, FAT): `bytes` a Lua string, or nil with opts["in"] a host file
    lua.set_function("media_write", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                sol::optional<std::string> bytes, sol::optional<sol::table> opts) {
        if (!bytes)
            return mediaCall(s, "write", slot, path, opts);
        sol::state_view view(s);
        sol::table all = view.create_table();
        if (opts)
            for (const auto& [key, value] : *opts)
                all[key] = value;
        all["data"] = base64::Encode(std::vector<uint8_t>(bytes->begin(), bytes->end()));
        return mediaCall(s, "write", slot, path, all);
    });
    lua.set_function("media_delete", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                 sol::optional<sol::table> opts) {
        return mediaCall(s, "delete", slot, path, opts);
    });
    // A new directory (FAT, iS-DOS catalogue, MB-02 / MB-01); opts {parents = true} makes the missing ones on the way, an
    // iS-DOS catalogue takes {continuous = true, blocks = 1-16}
    lua.set_function("media_mkdir", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                sol::optional<sol::table> opts) {
        return mediaCall(s, "mkdir", slot, path, opts);
    });
    // A file's attributes and time: opts {attributes = "+R-H", time = "2026-10-26 14:30"}
    lua.set_function("media_attrib", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                 sol::optional<sol::table> opts) {
        return mediaCall(s, "attrib", slot, path, opts);
    });
    // The volume's name: FAT label ("" removes it), TR-DOS disk title, iS-DOS device name
    lua.set_function("media_label", [mediaCall](sol::this_state s, const std::string& slot, const std::string& label,
                                                sol::optional<sol::table> opts) {
        sol::state_view view(s);
        sol::table all = view.create_table();
        if (opts)
            for (const auto& [key, value] : *opts)
                all[key] = value;
        all["label"] = label;
        return mediaCall(s, "label", slot, "", all);
    });
    // A bare `to` stays in the file's directory; a path from the root moves it (iS-DOS, FAT)
    lua.set_function("media_rename", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                 const std::string& to, sol::optional<sol::table> opts) {
        sol::state_view view(s);
        sol::table all = view.create_table();
        if (opts)
            for (const auto& [key, value] : *opts)
                all[key] = value;
        all["to"] = to;
        return mediaCall(s, "rename", slot, path, all);
    });
    for (const char* verb : {"insert", "swap"})
    {
        lua.set_function(std::string("media_") + verb,
                         [mediaCall, verb = std::string(verb)](sol::this_state s, const std::string& slot, const std::string& path,
                                                               sol::optional<sol::table> opts) {
                             return mediaCall(s, verb, slot, path, opts);
                         });
    }
    for (const char* verb : {"eject", "discard", "rescan", "create"})
    {
        lua.set_function(std::string("media_") + verb,
                         [mediaCall, verb = std::string(verb)](sol::this_state s, const std::string& slot,
                                                               sol::optional<sol::table> opts) {
                             return mediaCall(s, verb, slot, "", opts);
                         });
    }
    lua.set_function("media_save", [mediaCall](sol::this_state s, const std::string& slot, sol::optional<std::string> path,
                                               sol::optional<sol::table> opts) {
        return mediaCall(s, "save", slot, path.value_or(""), opts);
    });
    // A composite by a named strategy: strategy = "flat" (path) | "delta" | "commit"; plan, force
    lua.set_function("media_flatten", [mediaCall](sol::this_state s, const std::string& slot, sol::optional<std::string> path,
                                                  sol::optional<sol::table> opts) {
        return mediaCall(s, "flatten", slot, path.value_or(""), opts);
    });
    lua.set_function("media_export", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path) {
        return mediaCall(s, "export", slot, path, sol::nullopt);
    });
    lua.set_function("media_protect", [mediaCall](sol::this_state s, const std::string& slot, bool on) {
        sol::state_view view(s);
        sol::table t = view.create_table();
        t["on"] = on;
        return mediaCall(s, "protect", slot, "", sol::optional<sol::table>(t));
    });
    // endregion <Media>
}
