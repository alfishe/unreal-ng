// LuaEmulator bindings: network, execution state, RTC, ISA and the General Sound card.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterNetworkAndGs(sol::state& lua)
{
    // Network adapters: the same report every interface uses (DeviceState::Network)
    lua.set_function("network_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Network(emulator->GetContext()));
    });
    // network_configure{card="zxnetusb", host_access=true, hosts="name=1.2.3.4", remote_access=false} [, opts]
    //   -> the reply table (truthy) | nil, err, reply
    // (NetworkManager::ParseChange keys; remote_access: guest servers listen on 0.0.0.0, off = 127.0.0.1 only).
    // SlotControl verb network (ZX-bus slots, owner decision Q11): a change of the ZX-bus cards is a slot change
    // applied by a restart (opts: replace, dry_run, media as for slots_plug; an interpreter bound to the machine
    // follows the restarted one), the other keys go to the restarted machine; without one they apply in place
    // (status "accepted")
    lua.set_function("network_configure", [this](sol::this_state s, sol::table settings,
                                                 sol::optional<sol::table> opts) -> sol::variadic_results {
        sol::variadic_results out;
        SlotControlRequest request;
        request.verb = "network";
        for (const auto& [key, value] : settings)
        {
            std::string text;
            if (value.get_type() == sol::type::boolean)
                text = value.as<bool>() ? "on" : "off";
            else if (value.get_type() == sol::type::number)
                text = std::to_string(value.as<long long>());
            else
                text = value.as<std::string>();
            request.settings.emplace_back(key.as<std::string>(), text);
        }
        const SlotControlReply reply = slotsCall(request, opts, false);
        sol::object table = StateNodeToLua(s, reply.ToValue());
        if (reply.Ok())
        {
            out.push_back(table);
            return out;
        }
        out.push_back(sol::make_object(s, sol::lua_nil));
        out.push_back(sol::make_object(s, reply.message));
        out.push_back(table);
        return out;
    });

    // CMOS clock: the same report and cell access every interface uses
    // (DeviceState::Rtc, RtcAccess). Cells are numbered as the guest numbers them
    // The execution arbiter: intent and who set it, the command holding the CPU, queue, claim (DeviceState::Execution)
    lua.set_function("execution_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Execution(emulator->GetContext()));
    });
    lua.set_function("rtc_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Rtc(emulator->GetContext()));
    });
    // ZX Profi board chips: port map, 8255, 8253, 8251 (DeviceState::ProfiPeripherals); available = false elsewhere
    lua.set_function("profi_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::ProfiPeripherals(emulator->GetContext()));
    });
    lua.set_function("rtc_read", [this](sol::this_state s, int start, sol::optional<int> count) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::vector<uint8_t> bytes;
        std::string error;
        if (start < 0 || count.value_or(1) < 0 ||
            !RtcAccess::Read(emulator->GetContext(), static_cast<unsigned>(start),
                             static_cast<unsigned>(count.value_or(1)), bytes, error))
            return mouseError(s, error.empty() ? "start and count must not be negative" : error);
        sol::state_view view(s);
        sol::table out = view.create_table();
        for (size_t i = 0; i < bytes.size(); ++i)
            out[i + 1] = bytes[i];
        sol::variadic_results results;
        results.push_back(out);
        return results;
    });
    lua.set_function("rtc_write", [this](sol::this_state s, int start, sol::table values) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::vector<uint8_t> bytes;
        for (size_t i = 1; i <= values.size(); ++i)
        {
            const int v = values.get_or(i, -1);
            if (v < 0 || v > 255)
                return mouseError(s, "Every value must be 0-255");
            bytes.push_back(static_cast<uint8_t>(v));
        }
        std::string error;
        if (start < 0 || !RtcAccess::Write(emulator->GetContext(), static_cast<unsigned>(start), bytes,
                                           "Lua rtc_write", error))
            return mouseError(s, error.empty() ? "start must not be negative" : error);
        sol::variadic_results results;
        results.push_back(sol::make_object(s, true));
        return results;
    });

    // ISA slots (Sprinter ISA tdd §10): the report and cycles every interface uses (DeviceState::Isa,
    // IsaAccess). Addresses are 20-bit ISA addresses (a number, or "#30A" / "0x30A" text)
    lua.set_function("isa_state", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::Isa(emulator->GetContext()));
    });
    // Ethernet frames of the frame-level cards (EthernetAccess): the capture, a frame towards a card
    lua.set_function("network_frames", [this](sol::this_state s, sol::optional<std::string> link, sol::optional<int> last) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, EthernetAccess::Frames(emulator->GetContext(), link.value_or(""), static_cast<unsigned>(last.value_or(32))));
    });
    // Everything the network adapters sent and received (TrafficAccess, network #91)
    lua.set_function("network_traffic", [this](sol::this_state s, sol::optional<sol::table> q) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        TrafficAccess::Query query;
        if (q)
        {
            query.since = (*q).get_or("since", uint64_t{0});
            query.adapter = (*q).get_or("adapter", std::string());
            query.kind = (*q).get_or("kind", std::string());
            query.last = (*q).get_or("last", 64u);
        }
        return StateNodeToLua(s, TrafficAccess::Records(emulator->GetContext(), query));
    });
    lua.set_function("network_traffic_control", [this](sol::this_state s, const std::string& action, sol::optional<std::string> path,
                                                       sol::optional<uint64_t> value) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string error;
        // value: ring -> its bytes, stream -> the TCP port (0 = any free one)
        if (!TrafficAccess::Control(emulator->GetContext(), action, path.value_or(""), value.value_or(0), error))
            return mouseError(s, error);
        sol::variadic_results r;
        r.push_back(sol::make_object(s, true));
        return r;
    });
    // The host adapters the bridge can use (ethernet_mode=bridge, network SN6)
    lua.set_function("network_adapters", [](sol::this_state s) -> sol::object {
        return StateNodeToLua(s, EthernetAccess::Adapters());
    });
    lua.set_function("network_inject_frame", [this](sol::this_state s, const std::string& link, const std::string& hex) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        std::string error;
        if (!EthernetAccess::Inject(emulator->GetContext(), link, hex, "Lua network_inject_frame", error))
            return mouseError(s, error);
        sol::variadic_results results;
        results.push_back(sol::make_object(s, true));
        return results;
    });
    lua.set_function("isa_journal", [this](sol::this_state s, sol::optional<int> last) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::IsaJournal(emulator->GetContext(), static_cast<unsigned>(last.value_or(64))));
    });
    auto isaCycle = [this](sol::this_state s, const std::string& action, int slot, sol::object address,
                           int value) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        uint32_t addr = 0;
        if (address.is<int>())
            addr = static_cast<uint32_t>(address.as<int>());
        else if (!address.is<std::string>() || !IsaAccess::ParseAddress(address.as<std::string>(), addr))
            return mouseError(s, "address: a number or \"#..\" / \"0x..\" text");
        StateNode result;
        std::string error;
        if (!IsaAccess::Execute(emulator->GetContext(), action, slot, addr, value, "Lua isa", result, error))
            return mouseError(s, error);
        sol::variadic_results results;
        const StateNode* v = result.find("value");
        if (v && (action == "io_read" || action == "mem_read" || action == "io_peek" || action == "mem_peek"))
            results.push_back(sol::make_object(s, static_cast<int>(std::strtol(v->s.c_str() + 1, nullptr, 16))));
        else
            results.push_back(sol::make_object(s, true));
        return results;
    };
    lua.set_function("isa_io_read", [isaCycle](sol::this_state s, int slot, sol::object address) {
        return isaCycle(s, "io_read", slot, address, -1);
    });
    lua.set_function("isa_io_write", [isaCycle](sol::this_state s, int slot, sol::object address, int value) {
        return isaCycle(s, "io_write", slot, address, value);
    });
    lua.set_function("isa_io_peek", [isaCycle](sol::this_state s, int slot, sol::object address) {
        return isaCycle(s, "io_peek", slot, address, -1);
    });
    lua.set_function("isa_mem_read", [isaCycle](sol::this_state s, int slot, sol::object address) {
        return isaCycle(s, "mem_read", slot, address, -1);
    });
    lua.set_function("isa_mem_write", [isaCycle](sol::this_state s, int slot, sol::object address, int value) {
        return isaCycle(s, "mem_write", slot, address, value);
    });
    lua.set_function("isa_reset", [isaCycle](sol::this_state s) {
        return isaCycle(s, "reset", 0, sol::make_object(s, 0), -1);
    });
    lua.set_function("isa_latch", [isaCycle](sol::this_state s, int value) {
        return isaCycle(s, "latch", 0, sol::make_object(s, 0), value);
    });

    // The device catalog: the state reports this machine has, each with its function here (DeviceState::Catalog)
    lua.set_function("devices_state", [this](sol::this_state s, sol::optional<bool> all) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Catalog(ctx, all.value_or(false)));
    });
    lua.set_function("fdc_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Fdc(ctx));
    });
    // Device histories (core/src/debugger/history/historycontrol.h): each returns the reply, {error = ...} when
    // refused
    auto historyRun = [this](sol::this_state s, const HistoryRequest& request) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, HistoryControl::Execute(ctx, request).body);
    };
    lua.set_function("history_list", [historyRun](sol::this_state s) {
        HistoryRequest request;
        request.verb = "list";
        return historyRun(s, request);
    });
    // history(device[, last = 0[, since]]): the newest `last` records (0: all); with since the first `last`
    // numbered after it, each with its seq, and next_since, missed, restarted (a poller pages forward with no gap)
    lua.set_function("history", [historyRun](sol::this_state s, const std::string& device, sol::optional<size_t> last,
                                             sol::optional<uint64_t> since) {
        HistoryRequest request;
        request.verb = "read";
        request.device = device;
        request.last = last.value_or(0);
        if (since)
            request.since = *since;
        return historyRun(s, request);
    });
    // history_config(device, {mode = "ring", limit = 256, stream = "fdc.jsonl", stream_limit = 0,
    //                         filter = "side=host out=true"})
    lua.set_function("history_config", [historyRun](sol::this_state s, const std::string& device, sol::table options) {
        HistoryRequest request;
        request.verb = "config";
        request.device = device;
        if (sol::optional<std::string> mode = options["mode"])
            request.mode = *mode;
        if (sol::optional<uint64_t> limit = options["limit"])
            request.limit = *limit;
        if (sol::optional<std::string> stream = options["stream"])
            request.stream = *stream;
        if (sol::optional<uint64_t> streamLimit = options["stream_limit"])
            request.streamLimit = *streamLimit;
        if (sol::optional<std::string> filter = options["filter"])
            request.filter = *filter;
        return historyRun(s, request);
    });
    lua.set_function("history_clear", [historyRun](sol::this_state s, const std::string& device) {
        HistoryRequest request;
        request.verb = "clear";
        request.device = device;
        return historyRun(s, request);
    });
    // history_pause(device[, paused = true]): hold the history (nothing recorded, the records kept); false goes on
    lua.set_function("history_pause", [historyRun](sol::this_state s, const std::string& device, sol::optional<bool> paused) {
        HistoryRequest request;
        request.verb = paused.value_or(true) ? "pause" : "resume";
        request.device = device;
        return historyRun(s, request);
    });
    lua.set_function("contention_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = _emulator ? _emulator->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Contention(ctx));
    });

    // Audio state
    lua.set_function("audio_is_muted", [this]() -> bool {
        if (!effectiveEmulator()) return true;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pSoundManager) return true;
        return ctx->pSoundManager->isMuted();
    });

    lua.set_function("audio_ay_read", [this](int chip, int reg) -> int {
        if (!effectiveEmulator()) return 0;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pSoundManager) return 0;
        auto* ay = ctx->pSoundManager->getAYChip(chip);
        if (!ay || reg < 0 || reg > 15) return 0;
        return ay->readRegister(static_cast<uint8_t>(reg));
    });

    lua.set_function("audio_ay_registers", [this](sol::optional<int> chip) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table regs = lua_view.create_table();
        if (!effectiveEmulator()) return regs;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pSoundManager) return regs;
        auto* ay = ctx->pSoundManager->getAYChip(chip.value_or(0));
        if (!ay) return regs;
        const uint8_t* data = ay->getRegisters();
        for (int i = 0; i < 16; i++) {
            regs[i + 1] = data[i];  // Lua tables start at 1
        }
        return regs;
    });

    lua.set_function("audio_ay_count", [this]() -> int {
        if (!effectiveEmulator()) return 0;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pSoundManager) return 0;
        return ctx->pSoundManager->getAYChipCount();
    });

    // General Sound card (GS design §11.5). All actions mirror the
    // host-port semantics - each flushes the coprocessor to the current
    // ZX tact first. No-ops / nil when the card is not fitted.
    lua.set_function("gs_enabled", [this]() -> bool {
        if (!effectiveEmulator()) return false;
        auto* ctx = effectiveEmulator()->GetContext();
        return ctx && ctx->pSoundManager && ctx->pSoundManager->getGeneralSound() != nullptr;
    });

    // One report for every interface (DeviceState::Gs); `available = false`
    // with a description when no card is fitted
    lua.set_function("gs_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Gs(ctx));
    });
    lua.set_function("audio_covox_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Covox(ctx));
    });
    // ZX-MultiSound (DeviceState::MultiSound), its MIDI synthesizer (DeviceState::Midi) and the MIDI panic
    // (MidiControl: every voice stops, a TTD live input); unavailable / (false, reason) without the card
    lua.set_function("multisound_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::MultiSound(ctx));
    });
    // The Sprinter's ISA Wild Sound (DeviceState::WildSound); unavailable without the card
    lua.set_function("wildsound_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::WildSound(ctx));
    });
    lua.set_function("midi_state", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::Midi(ctx));
    });
    lua.set_function("midi_panic", [this]() -> std::tuple<bool, std::string> {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        if (!ctx)
            return {false, "no emulator selected"};
        const MidiControlReply reply = MidiControl::Execute(ctx, "panic");
        return {reply.ok, reply.message};
    });
    // MoonSound: overview, or part "fm" / "pcm"
    lua.set_function("audio_moonsound_state", [this](sol::this_state s, sol::optional<std::string> part) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        if (part && *part == "fm")
            return StateNodeToLua(s, DeviceState::MoonSoundFm(ctx));
        if (part && *part == "pcm")
            return StateNodeToLua(s, DeviceState::MoonSoundPcm(ctx));
        return StateNodeToLua(s, DeviceState::MoonSound(ctx));
    });

    // NeoGS SD slot and flash (other cards: false / no-op)
    // NeoGS media: checked here, carried out on the machine's thread
    // (neogsmedia.h); true when accepted. Insert / eject are refused while
    // a TTD recording runs
    lua.set_function("gs_sd_insert", [this](const std::string& path) -> bool {
        return _emulator && NeoGSMediaAccepted(NeoGSRequestSdInsert(_emulator->GetContext(), path));
    });
    lua.set_function("gs_sd_eject", [this]() -> bool {
        return _emulator && NeoGSMediaAccepted(NeoGSRequestSdEject(_emulator->GetContext()));
    });
    lua.set_function("gs_flash_save", [this]() -> bool {
        return _emulator && NeoGSMediaAccepted(NeoGSRequestFlashSave(_emulator->GetContext()));
    });
    // ZX-Evo flash ROM (TS-Conf, ATM3): the saved flash (evoflashrequest.h). rom_flash_state() is nil on other
    // machines; save / discard return true when accepted (discard: the shipped ROM returns at the next reset)
    lua.set_function("rom_flash_state", [this](sol::this_state s) -> sol::object {
        EvoFlash::PersistStatus st;
        if (!_emulator || !EvoFlashGetStatus(_emulator->GetContext(), st))
            return sol::make_object(s, sol::lua_nil);
        sol::state_view lv(s);
        sol::table t = lv.create_table();
        t["machine"] = st.machine;
        t["file"] = st.path;
        t["file_exists"] = st.fileExists;
        t["base_rom_sha256"] = st.baseDigest;
        t["loaded_from_file"] = st.loadedFromFile;
        t["unsaved"] = st.unsaved;
        t["changes"] = st.changes;
        sol::table others = lv.create_table();
        for (size_t i = 0; i < st.otherImageFiles.size(); ++i)
            others[i + 1] = st.otherImageFiles[i];
        t["other_image_files"] = others;
        return t;
    });
    lua.set_function("rom_flash_save", [this]() -> bool {
        return _emulator && EvoFlashAccepted(EvoFlashRequestSave(_emulator->GetContext()));
    });
    lua.set_function("rom_flash_discard", [this]() -> bool {
        return _emulator && EvoFlashAccepted(EvoFlashRequestDiscard(_emulator->GetContext()));
    });
    // NeoGS stereo mode: "separated" (as on the board), "gs" (50% cross-feed
    // like the classic GS) or "mono"; applied at the next frame. False on
    // an unknown name
    lua.set_function("gs_stereo_mode", [this](const std::string& mode) -> bool {
        NeoGSConfig::StereoMode parsed = NeoGSConfig::StereoMode::Separated;
        SoundManager* sm = _emulator && _emulator->GetContext() ? _emulator->GetContext()->pSoundManager : nullptr;
        if (!sm || !neogsParseStereoMode(mode, parsed))
            return false;
        sm->setNeoGSStereoMode(parsed);
        return true;
    });

    // Host-port stimuli step the card's Z80, so they go through the live-input
    // path: applied on the machine's thread at an instruction boundary and
    // journaled for TTD replay. Each returns true when submitted; false when
    // no card is fitted, the byte is out of range or TTD replay owns input
    auto submitGS = [this](ttd::TTDInputKind kind, int value) -> bool {
        if (!effectiveEmulator() || value < 0 || value > 255) return false;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx || !ctx->pTimeTravelHooks) return false;
        if (!ctx->pSoundManager || !ctx->pSoundManager->getGeneralSound()) return false;
        ttd::TTDInputEvent ev;
        ev.kind = kind;
        ev.value = static_cast<uint8_t>(value);
        return ctx->pTimeTravelHooks->SubmitLiveInput(ev);
    };

    lua.set_function("gs_reset", [submitGS]() { return submitGS(ttd::TTDInputKind::GSReset, 0); });

    // #33 bit7 semantics: CPU/banking/timing only, mailbox survives
    lua.set_function("gs_reset_card", [submitGS]() { return submitGS(ttd::TTDInputKind::GSResetCard, 0); });

    lua.set_function("gs_nmi", [submitGS]() { return submitGS(ttd::TTDInputKind::GSNmi, 0); });

    lua.set_function("gs_send_command",
                     [submitGS](int byte) { return submitGS(ttd::TTDInputKind::GSCommand, byte); });

    lua.set_function("gs_send_data", [submitGS](int byte) { return submitGS(ttd::TTDInputKind::GSData, byte); });

    // The debugger's send (gs-debugger design §6.4, F6): queued, each byte when the card may take it, the replies
    // read and named by the firmware profile. Returns the send (state "queued"; gs_conversation(id) later), or
    // {error = ...}. Never waits: a script may run on the thread that runs the steps
    lua.set_function("gs_send", [this](sol::this_state s, int command, sol::optional<sol::table> opts) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSSendRequest request;
        request.command = static_cast<uint8_t>(command);
        bool ok = command >= 0 && command <= 255;
        if (ok && opts)
        {
            const auto byte = [&ok](sol::object v, std::optional<uint8_t>& out) {
                if (v.get_type() == sol::type::lua_nil)
                    return;
                ok = ok && v.is<int>() && v.as<int>() >= 0 && v.as<int>() <= 255;
                if (ok)
                    out = static_cast<uint8_t>(v.as<int>());
            };
            byte((*opts)["pre"], request.pre);
            byte((*opts)["replies"], request.replies);
            if (sol::optional<sol::table> params = (*opts)["params"])
            {
                for (size_t i = 1; ok && i <= params->size(); i++)
                {
                    std::optional<uint8_t> b;
                    byte((*params)[i], b);
                    if (ok && b)
                        request.params.push_back(*b);
                }
            }
        }
        if (!ok)
        {
            StateNode error = StateNode::Object();
            error["error"] = std::string("gs_send(command, {pre =, params = {...}, replies =}): bytes 0..255");
            return StateNodeToLua(s, error);
        }
        return StateNodeToLua(s, GSSend(ctx, request, 0).body);
    });
    // Device boards (debugger-model widget catalog §4, core DebugBoards): the same data as /debug/boards;
    // a refused read or action is {error = ...}
    lua.set_function("boards", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DebugBoards::List(ctx).body["boards"]);
    });
    lua.set_function("board", [this](sol::this_state s, const std::string& id, sol::optional<bool> values) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DebugBoards::Read(ctx, id, values.value_or(false)).body);
    });
    lua.set_function("board_action", [this](sol::this_state s, const std::string& id, const std::string& action,
                                            sol::optional<std::string> arg) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DebugBoards::Action(ctx, id, action, arg.value_or("")).body);
    });
    lua.set_function("gs_conversation", [this](sol::this_state s, sol::optional<uint32_t> id) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, GSConversationRead(ctx, id.value_or(0)).body);
    });
    // The NeoGS ZX-DMA trace (gszxdmatrace.h, gs-debugger N6): the newest `last` bytes (default 256, 0: all)
    // after the seq `since`; {available = false, description} without a NeoGS
    lua.set_function("gs_zxdma", [this](sol::this_state s, sol::optional<uint64_t> last,
                                        sol::optional<uint64_t> since) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSZxDmaTraceQuery query;
        query.last = static_cast<size_t>(last.value_or(query.last));
        query.since = since.value_or(0);
        return StateNodeToLua(s, DeviceState::GsZxDmaTrace(ctx, query));
    });
    // The card CPU's profiler (gsprofilereport.h, gs-debugger X9 / X20): the report (cpu "gs" / "ms", the top
    // `routines`, the newest `activity` frames); {available = false, description} without a card CPU
    lua.set_function("gs_profile", [this](sol::this_state s, sol::optional<std::string> cpu,
                                          sol::optional<uint64_t> routines, sol::optional<uint64_t> activity) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSProfileQuery query;
        query.cpu = cpu.value_or("");
        query.routines = static_cast<size_t>(routines.value_or(query.routines));
        query.activity = static_cast<size_t>(activity.value_or(query.activity));
        return StateNodeToLua(s, DeviceState::GsProfile(ctx, query));
    });
    // "start" (cleared, then on), "stop", "clear": {action, cpu, card, on} or {error}
    lua.set_function("gs_profile_control", [this](sol::this_state s, const std::string& action,
                                                  sol::optional<std::string> cpu) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, GSProfileControl(ctx, action, cpu.value_or("")).body);
    });
    // The last complete host frame per card interrupt period (X2): rows `first` .. `first + count` (0: to the end)
    lua.set_function("gs_events", [this](sol::this_state s, sol::optional<std::string> cpu, sol::optional<uint64_t> first,
                                         sol::optional<uint64_t> count, sol::optional<uint64_t> rows) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSEventsQuery query;
        query.cpu = cpu.value_or("");
        query.first = static_cast<size_t>(first.value_or(0));
        query.count = static_cast<size_t>(count.value_or(0));
        query.rows = static_cast<size_t>(rows.value_or(query.rows));
        return StateNodeToLua(s, DeviceState::GsEvents(ctx, query));
    });
    // The card CPU analysis (gsanalysisreport.h, gs-debugger P5.6): gs_analysis(action, {cpu =, decay =}) with
    // action status / on / off / reset / release; gs_coverage({cpu, space, page, ranges, raw}) and
    // gs_heatmap({cpu, channels, recent, frames, cells, format}); a bad option is {error = ...}
    {
        auto options = [](const sol::optional<sol::table>& table) {
            return [table](const std::string& key) -> std::string {
                if (!table)
                    return {};
                const sol::object value = (*table)[key];
                switch (value.get_type())
                {
                    case sol::type::string: return value.as<std::string>();
                    case sol::type::number:
                        return std::to_string(static_cast<long long>(value.as<double>()));
                    case sol::type::boolean: return value.as<bool>() ? "1" : "0";
                    default: return {};
                }
            };
        };
        auto failed = [](sol::this_state s, const std::string& message) {
            StateNode error = StateNode::Object();
            error["error"] = message;
            return StateNodeToLua(s, error);
        };
        lua.set_function("gs_analysis", [this, options](sol::this_state s, sol::optional<std::string> action,
                                                        sol::optional<sol::table> opts) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            const auto get = options(opts);
            GSAnalysisControlRequest request;
            request.action = action.value_or("status");
            request.cpu = get("cpu");
            if (const std::string decay = get("decay"); !decay.empty())
                request.decay = std::atoi(decay.c_str());
            return StateNodeToLua(s, GSAnalysisControl(ctx, request).body);
        });
        lua.set_function("gs_coverage", [this, options, failed](sol::this_state s,
                                                                sol::optional<sol::table> opts) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            GSAnalysisQuery query;
            std::string error;
            if (!GSAnalysisQueryFromParams(options(opts), query, error))
                return failed(s, error);
            return StateNodeToLua(s, DeviceState::GsCoverage(ctx, query));
        });
        lua.set_function("gs_heatmap", [this, options, failed](sol::this_state s,
                                                               sol::optional<sol::table> opts) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            GSAnalysisQuery query;
            std::string error;
            if (!GSAnalysisQueryFromParams(options(opts), query, error))
                return failed(s, error);
            return StateNodeToLua(s, DeviceState::GsHeatmap(ctx, query));
        });
    }
    // The GS slot's statistics (gsslotstats.h, gs-debugger N7): the block or one section, counters since a
    // baseline; a running machine answers from the next frame end (at most 500 ms; {error} after that)
    lua.set_function("gs_stats", [this](sol::this_state s, sol::optional<std::string> section,
                                        sol::optional<uint32_t> since, sol::optional<std::string> cpu) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSStatsQuery query;
        query.section = section.value_or("");
        query.since = since.value_or(0);
        query.cpu = cpu.value_or(""); // "ms": the ZX-MultiSound's GS
        query.waitMs = 500;
        return StateNodeToLua(s, GSStatsRead(ctx, query).body);
    });
    lua.set_function("gs_reset_counters", [this](sol::this_state s, sol::optional<std::string> cpu) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSStatsQuery query;
        query.cpu = cpu.value_or("");
        query.waitMs = 500;
        return StateNodeToLua(s, GSStatsResetCounters(ctx, query).body);
    });

    // The firmware objects through the card's profile (gsfirmwareobjects.h, gs-debugger §8A.9): struct instances
    // as rows of fields, variables; one object by name; another profile by id
    lua.set_function("gs_firmware", [this](sol::this_state s, sol::optional<std::string> object,
                                           sol::optional<std::string> profile,
                                           sol::optional<std::string> cpu) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        GSFirmwareObjectsQuery query;
        query.object = object.value_or("");
        query.profile = profile.value_or("");
        query.cpu = cpu.value_or(""); // "ms": the ZX-MultiSound's GS
        return StateNodeToLua(s, GSFirmwareObjectsRead(ctx, query).body);
    });
}
