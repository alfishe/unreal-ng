// LuaEmulator bindings: the General Sound card and time travel.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterGsAndTimeTravel(sol::state& lua)
{
    lua.set_function("gs_firmware_profiles", [](sol::this_state s) -> sol::object {
        return StateNodeToLua(s, GSFirmwareProfilesState()["profiles"]);
    });

    // Reads are peeks: no host read cycle (read_data leaves status bit 7
    // set) and the card is not stepped
    lua.set_function("gs_read_data", [this]() -> int {
        if (!effectiveEmulator()) return -1;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        return gs ? gs->getDataToHost() : -1;
    });

    lua.set_function("gs_read_status", [this]() -> int {
        if (!effectiveEmulator()) return -1;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        return gs ? (gs->getStatusRaw() | 0x7E) : -1;
    });

    // The General Sound personality: the card in the GS slot replaced as a slot change, applied by a machine
    // restart (ZX-bus slots, owner decision Q10) - the same as slots_gs(card). Returns (ok, message); the reply
    // table of slots_gs has the plan and the new emulator id
    lua.set_function("gs_switch_personality", [this](const std::string& personality,
                                                      sol::optional<sol::table> opts) -> std::tuple<bool, std::string> {
        SlotControlRequest request;
        request.verb = "gs";
        request.card = personality;
        const SlotControlReply reply = slotsCall(request, opts, false);
        return {reply.Ok(), reply.message};
    });

    // Diagnostics: write the last completed COM30..D2 upload (the raw
    // ProTracker module the host streamed) to a file - same data
    // dump_module serves via the WebAPI/MCP
    lua.set_function("gs_dump_module", [this](sol::optional<std::string> path) -> sol::object {
        sol::state_view lua_view(*_lua);
        if (!effectiveEmulator()) return sol::make_object(lua_view, sol::lua_nil);
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (!gs) return sol::make_object(lua_view, sol::lua_nil);

        std::vector<uint8_t> bytes;
        bool playing = false;
        if (!gs->captureModuleUpload(bytes, playing))
            return sol::make_object(lua_view, sol::lua_nil);

        const std::string outPath = path.value_or("gs-module-dump.mod");
        std::ofstream out(outPath, std::ios::binary);
        if (!out)
            return sol::make_object(lua_view, sol::lua_nil);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        sol::table t = lua_view.create_table();
        t["path"] = outPath;
        t["bytes"] = static_cast<double>(bytes.size());
        t["playing"] = playing;
        return t;
    });

    // GS coprocessor triage: always-on activity counters + opt-in
    // port/DAC event trace - the "is the GS Z80 alive and doing DAC
    // pushes" tool, same data model as CLI 'gsporttrace' / WebAPI /
    // MCP / Python (see gsporttrace.h).
    lua.set_function("gs_counters", [this]() -> sol::object {
        sol::state_view lua_view(*_lua);
        if (!effectiveEmulator()) return sol::make_object(lua_view, sol::lua_nil);
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (!gs) return sol::make_object(lua_view, sol::lua_nil);

        const GSActivityCounters& c = gs->getActivityCounters();
        sol::table t = lua_view.create_table();
        t["cpu_steps"] = static_cast<double>(c.cpuSteps);
        t["interrupts_accepted"] = static_cast<double>(c.interruptsAccepted);
        t["interrupt_periods"] = static_cast<double>(c.interruptPeriods);
        t["interrupts_coalesced"] = static_cast<double>(c.interruptsCoalesced);
        t["nmis_accepted"] = static_cast<double>(c.nmisAccepted);
        t["dac_fetches"] = static_cast<double>(c.dacFetches);
        t["volume_latch_writes"] = static_cast<double>(c.volumeLatchWrites);
        t["host_commands_received"] = static_cast<double>(c.hostCommandsReceived);
        t["host_commands_dropped"] = static_cast<double>(c.hostCommandsDropped);
        t["host_data_written"] = static_cast<double>(c.hostDataWritten);
        t["host_data_dropped"] = static_cast<double>(c.hostDataDropped);
        t["host_data_read"] = static_cast<double>(c.hostDataRead);
        t["last_dac_fetch_gs_cycle"] = static_cast<double>(c.lastDacFetchGsCycle);
        t["last_dac_fetch_frame"] = static_cast<double>(c.lastDacFetchFrame);
        t["trace_capturing"] = gs->isPortTraceCapturing();
        t["trace_event_count"] = static_cast<double>(gs->getPortTraceEventCount());
        t["pc"] = gs->getCPUReg(GSCpuRegister::PC);
        t["halted"] = gs->isCPUHalted();
        return t;
    });

    lua.set_function("gs_porttrace_start", [this]() {
        if (!effectiveEmulator()) return;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (gs) gs->startPortTrace();
    });
    lua.set_function("gs_porttrace_stop", [this]() {
        if (!effectiveEmulator()) return;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (gs) gs->stopPortTrace();
    });
    lua.set_function("gs_porttrace_pause", [this]() {
        if (!effectiveEmulator()) return;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (gs) gs->pausePortTrace();
    });
    lua.set_function("gs_porttrace_resume", [this]() {
        if (!effectiveEmulator()) return;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (gs) gs->resumePortTrace();
    });
    lua.set_function("gs_porttrace_clear", [this]() {
        if (!effectiveEmulator()) return;
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (gs) gs->clearPortTrace();
    });

    lua.set_function("gs_porttrace_events", [this](sol::optional<int> count) -> sol::object {
        sol::state_view lua_view(*_lua);
        if (!effectiveEmulator()) return sol::make_object(lua_view, sol::lua_nil);
        auto* ctx = effectiveEmulator()->GetContext();
        GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
        if (!gs) return sol::make_object(lua_view, sol::lua_nil);

        auto events = gs->getPortTraceLast(static_cast<size_t>(count.value_or(50)));
        sol::table result = lua_view.create_table();
        int idx = 1;
        for (const auto& e : events)
        {
            sol::table ev = lua_view.create_table();
            ev["timestamp"] = static_cast<double>(e.timestamp);
            ev["frame"] = e.frameNumber;
            switch (e.side)
            {
                case GSTraceSide::Host: ev["side"] = "host"; break;
                case GSTraceSide::GsInternal: ev["side"] = "gs"; break;
                case GSTraceSide::DacFetch: ev["side"] = "dac"; break;
                case GSTraceSide::Interrupt: ev["side"] = "interrupt"; break;
                case GSTraceSide::ZxDma: ev["side"] = "zxdma"; break;
                case GSTraceSide::Idle: ev["side"] = "idle"; break;
            }
            ev["direction"] = e.isOut() ? "out" : "in";
            ev["port"] = e.port;
            ev["value"] = e.value;
            ev["pc"] = e.pc;
            if (e.side == GSTraceSide::DacFetch) ev["channel"] = e.channel;
            if (e.side == GSTraceSide::ZxDma) { ev["card_address"] = e.cardAddress(); ev["host_t"] = e.hostT(); ev["wait"] = e.waitT(); ev["dropped"] = e.isDropped(); }
            if (e.side == GSTraceSide::Interrupt) ev["nmi"] = e.isNmi();
            if (e.side == GSTraceSide::Idle) ev["iterations"] = e.iterations;
            result[idx++] = ev;
        }
        return result;
    });

    // Advanced disk operations
    lua.set_function("disk_info", [this](int drive) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table info = lua_view.create_table();
        if (!effectiveEmulator() || drive < 0 || drive > 3) return info;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx) return info;
        FDD* fdd = ctx->coreState.diskDrives[drive];
        if (!fdd) return info;
        std::unique_ptr<DiskImage> snapshot = fdd->SnapshotDisk();  // the drive's image may change meanwhile
        DiskImage* disk = snapshot.get();
        if (!disk) return info;
        info["cylinders"] = disk->getCylinders();
        info["sides"] = disk->getSides();
        info["tracks"] = disk->getCylinders() * disk->getSides();
        // Geometry of track 0 side 0 (tracks may differ on non-TR-DOS images)
        auto* track0 = disk->getTrackForCylinderAndSide(0, 0);
        info["sectors_per_track"] = track0 ? static_cast<int>(track0->sectorCount()) : 0;
        info["sector_size"] = (track0 && track0->sectorCount() > 0) ? static_cast<int>(track0->getRawSector(0)->dataSize) : 0;
        info["track_size"] = track0 ? static_cast<int>(track0->rawSize()) : 0;
        return info;
    });

    lua.set_function("disk_read_sector", [this](int drive, int cyl, int side, int sector) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table data = lua_view.create_table();
        if (!effectiveEmulator() || drive < 0 || drive > 3) return data;
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx) return data;
        FDD* fdd = ctx->coreState.diskDrives[drive];
        if (!fdd) return data;
        std::unique_ptr<DiskImage> snapshot = fdd->SnapshotDisk();  // the drive's image may change meanwhile
        DiskImage* disk = snapshot.get();
        if (!disk) return data;
        auto* track = disk->getTrackForCylinderAndSide(cyl, side);
        if (!track) return data;
        auto* sec = track->getSector(static_cast<uint8_t>(sector));  // Sector number = sector + 1
        if (!sec || !sec->hasData) return data;
        for (int i = 0; i < sec->dataSize; i++) {
            data[i + 1] = sec->data[i];  // Lua tables start at 1
        }
        return data;
    });

    // A debugger's write into the sector's data field (SectorWrite); sector 0-based as disk_read_sector (ID - 1);
    // data a string of bytes or a table -> true | nil, error
    lua.set_function("disk_write_sector", [this](sol::this_state s, int drive, int cyl, int side, int sector,
                                                 sol::object data, sol::optional<uint32_t> offset)
                                              -> std::tuple<sol::object, sol::object> {
        auto fail = [&](const std::string& error) -> std::tuple<sol::object, sol::object> {
            return {sol::make_object(s, sol::lua_nil), sol::make_object(s, error)};
        };
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return fail("no emulator");
        if (drive < 0 || drive > 3)
            return fail("bad drive (0-3)");
        std::vector<uint8_t> bytes;
        if (data.is<std::string>())
        {
            const std::string text = data.as<std::string>();
            bytes.assign(text.begin(), text.end());
        }
        else if (data.is<sol::table>())
        {
            for (const auto& kv : data.as<sol::table>())
            {
                if (!kv.second.is<int>() || kv.second.as<int>() < 0 || kv.second.as<int>() > 255)
                    return fail("data table must hold bytes 0..255");
                bytes.push_back(static_cast<uint8_t>(kv.second.as<int>()));
            }
        }
        else
            return fail("data must be a string of bytes or a table");
        const SectorWrite::Result result = SectorWrite::Write(emulator, static_cast<uint8_t>(drive), cyl, side,
                                                              sector + 1, offset.value_or(0), bytes, "lua");
        if (!result.ok)
            return fail(result.error);
        return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
    });

    lua.set_function("disk_read_sector_hex", [this](int drive, int trackNo, int sector) -> std::string {
        if (!effectiveEmulator() || drive < 0 || drive > 3) return "";
        auto* ctx = effectiveEmulator()->GetContext();
        if (!ctx) return "";
        FDD* fdd = ctx->coreState.diskDrives[drive];
        if (!fdd) return "";
        std::unique_ptr<DiskImage> snapshot = fdd->SnapshotDisk();  // the drive's image may change meanwhile
        DiskImage* disk = snapshot.get();
        if (!disk) return "";
        return disk->DumpSectorHex(trackNo, sector);
    });

    // Set the emulator instance in the Lua environment
    lua["emulator"] = _emulator;

    // -----------------------------------------------------------------
    // TTD (Time-Travel Debug) bindings — Phase 2 surface
    // -----------------------------------------------------------------
    // All functions return tables or booleans matching the WebAPI/Python
    // shape. No-op (return false / empty table) when TTD is unavailable.
    // -----------------------------------------------------------------

    lua.set_function("ttd_status", [this](sol::this_state ts) -> sol::object {
        const ttd::TTDReply reply = TtdRun("status");
        return StateNodeToLua(ts, reply.Ok() ? reply.body : ttd::TTDControl::StatusBody(nullptr));
    });

    // ttd_file_info(path) - a .ttd file's header, sections and recorded machine,
    // read without loading it: {ok, error | path, file_bytes, ..., machine, sections}
    lua.set_function("ttd_file_info", [this](sol::this_state ts, const std::string& path) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(ttd::TTDControl(nullptr).Execute({"file-info", {{"path", path}}})));
    });

    // ttd_start([journal], [black_box], [minutes]) - start recording; journal = true also records the
    // write journal. Without it the ttd_set_journal_enabled choice stands (off by default, D40).
    // black_box = true records as the black box: the last `minutes` (default 5), turbo and host speed free
    lua.set_function("ttd_start", [this](sol::optional<bool> journalOpt, sol::optional<bool> blackBoxOpt,
                                         sol::optional<uint32_t> minutesOpt) -> bool {
        std::map<std::string, std::string> options;
        if (journalOpt.has_value())
            options["journal"] = journalOpt.value() ? "true" : "false";
        if (blackBoxOpt.has_value())
            options["black_box"] = blackBoxOpt.value() ? "true" : "false";
        if (minutesOpt.has_value())
            options["minutes"] = std::to_string(minutesOpt.value());
        const ttd::TTDReply reply = TtdRun("start", options);
        return reply.Ok() && (reply.body.find("started")->b || reply.body.find("already_active")->b);
    });

    // ttd_set_history_limit(frames, bytes) - bound the recorded history: while
    // recording, the oldest frames are released beyond either limit (0 = none;
    // nil keeps the current value). Returns the limit now in force: frames, bytes
    lua.set_function("ttd_set_history_limit",
                     [this](sol::optional<uint64_t> frames, sol::optional<uint64_t> bytes) -> std::tuple<uint64_t, uint64_t> {
        std::map<std::string, std::string> options;
        if (frames.has_value())
            options["frames"] = std::to_string(frames.value());
        if (bytes.has_value())
            options["bytes"] = std::to_string(bytes.value());
        const ttd::TTDReply reply = TtdRun("history-limit", options);
        if (!reply.Ok())
            return {0, 0};
        return {static_cast<uint64_t>(reply.body.find("history_limit_frames")->i),
                static_cast<uint64_t>(reply.body.find("history_limit_bytes")->i)};
    });

    // ttd_set_journal_enabled(bool) - switch the write journal at any moment,
    // also while recording: a journal segment starts or ends there (D40). ok, reason
    lua.set_function("ttd_set_journal_enabled", [this](bool enabled) -> std::tuple<bool, std::string> {
        const ttd::TTDReply reply = TtdRun("journal", {{"enabled", enabled ? "true" : "false"}});
        return {reply.Ok(), reply.message};
    });

    // ttd_build_journal([from_frame], [to_frame]) - build the write journal for
    // frames from..to (default: the whole session) by replaying them, about
    // 2-4 ms per frame; not while recording. Returns a table: ok, error,
    // cancelled, frames_built, frames_covered, frames_refused, records
    lua.set_function("ttd_build_journal", [this](sol::this_state ts, sol::optional<uint64_t> fromOpt,
                                                sol::optional<uint64_t> toOpt) -> sol::object {
        std::map<std::string, std::string> options;
        if (fromOpt.has_value())
            options["from_frame"] = std::to_string(fromOpt.value());
        if (toOpt.has_value())
            options["to_frame"] = std::to_string(toOpt.value());
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("journal-build", options)));
    });

    // ttd_cancel_journal_build() -> {cancelled}: stops a write-journal build that runs (another thread's call)
    lua.set_function("ttd_cancel_journal_build", [this](sol::this_state ts) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("journal-build-cancel")));
    });

    lua.set_function("ttd_get_journal_enabled", [this]() -> bool {
        const ttd::TTDReply reply = TtdRun("journal");
        return reply.Ok() && reply.body.find("write_journal_setting")->b;
    });

    lua.set_function("ttd_stop", [this]() { (void)TtdRun("stop"); });

    // ok, reason: refused while recording (stop the recording first)
    lua.set_function("ttd_invalidate", [this](sol::optional<std::string> reason) -> std::tuple<bool, std::string> {
        const ttd::TTDReply reply = TtdRun("invalidate", {{"reason", reason.value_or("lua invalidate")}});
        return {reply.Ok(), reply.message};
    });

    lua.set_function("ttd_seek", [this](sol::this_state ts, uint64_t frame, sol::optional<uint32_t> tInFrameOpt) -> sol::object {
        // Without a T-state: the frame's end (D13)
        std::map<std::string, std::string> options{{"frame", std::to_string(frame)}};
        if (tInFrameOpt)
            options["tinframe"] = std::to_string(*tInFrameOpt);
        return TtdSeekValue(ts, TtdRun("seek", options));
    });

    lua.set_function("ttd_step_back", [this]() -> bool {
        const ttd::TTDReply reply = TtdRun("step-back");
        return reply.Ok() && reply.body.find("stepped")->b;
    });

    lua.set_function("ttd_step_forward", [this]() -> bool {
        const ttd::TTDReply reply = TtdRun("step-forward");
        return reply.Ok() && reply.body.find("stepped")->b;
    });

    lua.set_function("ttd_resume", [this](sol::optional<uint64_t> frameOpt, sol::optional<uint32_t> tInFrameOpt) -> bool {
        // No frame: resume exactly where the machine stands
        std::map<std::string, std::string> options;
        if (frameOpt)
        {
            options["frame"] = std::to_string(*frameOpt);
            options["tinframe"] = std::to_string(tInFrameOpt.value_or(0));
        }
        const ttd::TTDReply reply = TtdRun("resume", options);
        return reply.Ok() && reply.body.find("resumed")->b;
    });

    lua.set_function("ttd_position", [this](sol::this_state ts) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("position")));
    });

    lua.set_function("ttd_markers", [this](sol::this_state ts) -> sol::object {
        const ttd::TTDReply reply = TtdRun("markers");
        return StateNodeToLua(ts, reply.Ok() ? *reply.body.find("markers") : StateNode::Array());
    });

    // -----------------------------------------------------------------
    // TD-4 — agent bookmarks (advisory annotations, never barriers).
    // Labels are keys: non-empty, at most 63 chars, unique per session.
    // -----------------------------------------------------------------

    // ttd_bookmark_add(label [, frame [, tinframe]] [, {note = "...", tags = {"a", "b"}, watch = {"#A130",
    // "#A140:4"}, context = true}]): the options table may stand in for the frame
    lua.set_function("ttd_bookmark_add", [this](sol::this_state ts, const std::string& label, sol::object a,
                                                 sol::object b, sol::object c) -> sol::object {
        // Position omitted: the current position (mark here)
        std::map<std::string, std::string> options{{"label", label}};
        sol::object table;
        if (a.get_type() == sol::type::number)
        {
            options["frame"] = TtdOptionText(a);
            options["tinframe"] = b.get_type() == sol::type::number ? TtdOptionText(b) : "0";
            table = b.get_type() == sol::type::table ? b : c;
        }
        else
            table = a;
        if (table.get_type() == sol::type::table)
            for (const auto& [key, value] : table.as<sol::table>())
                if (key.is<std::string>())
                    options[key.as<std::string>()] = TtdOptionText(value);
        const ttd::TTDReply reply = TtdRun("bookmark-add", options);
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["added"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    lua.set_function("ttd_bookmarks", [this](sol::this_state ts, sol::optional<std::string> tag) -> sol::object {
        std::map<std::string, std::string> options;
        if (tag)
            options["tag"] = *tag;
        const ttd::TTDReply reply = TtdRun("bookmarks", options);
        return StateNodeToLua(ts, reply.Ok() ? *reply.body.find("bookmarks") : StateNode::Array());
    });

    // ttd_bookmark_note(label, {note = "...", append = true, tags = {...}, add_tags = {...}, remove_tags = {...},
    // rename = "..."}): the bookmark as it is now, or {updated = false, error = ...}
    lua.set_function("ttd_bookmark_note", [this](sol::this_state ts, const std::string& label,
                                                  sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options{{"label", label}};
        for (const auto& [key, value] : argsTable)
            if (key.is<std::string>())
                options[key.as<std::string>()] = TtdOptionText(value);
        const ttd::TTDReply reply = TtdRun("bookmark-note", options);
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["updated"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    // ttd_bookmark_export{format = "markdown" | "profile" | "json", tag = "...", title = "...", path = "..."}:
    // {format, count, text, path, bytes}
    lua.set_function("ttd_bookmark_export", [this](sol::this_state ts, sol::optional<sol::table> argsTable)
                                                -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                if (key.is<std::string>())
                    options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("bookmark-export", options)));
    });

    lua.set_function("ttd_bookmark_delete", [this](const std::string& label) -> bool {
        return TtdRun("bookmark-delete", {{"label", label}}).Ok();
    });

    // A bookmark seek IS a seek — identical result shape to ttd_seek
    // (plus the resolved label), so a real barrier between the restore
    // checkpoint and the target still surfaces as halt_reason
    // "external_event". A bookmark itself never halts anything.
    lua.set_function("ttd_seek_bookmark", [this](sol::this_state ts, const std::string& label) -> sol::object {
        return TtdSeekValue(ts, TtdRun("seek", {{"bookmark", label}}));
    });

    // -----------------------------------------------------------------
    // TTD branches: a resume in the past keeps the old future as a branch.
    // A branch is named by its id (0 = the trunk), its GUID or its name.
    // -----------------------------------------------------------------

    // ttd_branches([{scope = "all" | "lineage", created_by = "...", run = "...", tag = "..." or {...},
    // name_prefix = "...", pinned = true, min_length = F, max_length = F, created_after = US, created_before = US,
    // limit = N, offset = N}]): {branches = {...}, total, count, active, viewed, lineage, text}
    lua.set_function("ttd_branches", [this](sol::this_state ts, sol::optional<sol::table> argsTable)
                                         -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                if (key.is<std::string>())
                    options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("branches", options)));
    });

    // ttd_branch_switch_to(to): "previous" (the branch left last; again: further back), "next_sibling" /
    // "prev_sibling" (the next / previous branch at the nearest fork, by creation); as ttd_branch_switch
    lua.set_function("ttd_branch_switch_to", [this](sol::this_state ts, const std::string& to) -> sol::object {
        const ttd::TTDReply reply = TtdRun("branch-switch", {{"to", to}});
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["switched"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    // ttd_branch_switch(branch [, frame [, tinframe]]): continue on the branch, at the moment on it (default: its
    // end); {switched, branch, arrived_at, state, check, text} or {switched = false, error = ...}
    lua.set_function("ttd_branch_switch", [this](sol::this_state ts, sol::object branch,
                                                  sol::optional<uint64_t> frame,
                                                  sol::optional<uint32_t> tinframe) -> sol::object {
        std::map<std::string, std::string> options{{"branch", TtdOptionText(branch)}};
        if (frame)
        {
            options["frame"] = std::to_string(*frame);
            options["tinframe"] = std::to_string(tinframe.value_or(0));
        }
        const ttd::TTDReply reply = TtdRun("branch-switch", options);
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["switched"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    // ttd_branch_update(branch, {name = "...", note = "...", tags = {...}, add_tags = {...}, remove_tags = {...},
    // pinned = true}): the branch as it is now, or {updated = false, error = ...}
    lua.set_function("ttd_branch_update", [this](sol::this_state ts, sol::object branch,
                                                  sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options{{"branch", TtdOptionText(branch)}};
        for (const auto& [key, value] : argsTable)
            if (key.is<std::string>())
                options[key.as<std::string>()] = TtdOptionText(value);
        const ttd::TTDReply reply = TtdRun("branch-update", options);
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["updated"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    // ttd_branch_info(branch): {branch, children = {...}, ancestors = {...}, files = {...}, file_bytes, folder,
    // text} or {error = ...}
    lua.set_function("ttd_branch_info", [this](sol::this_state ts, sol::object branch) -> sol::object {
        const ttd::TTDReply reply = TtdRun("branch-info", {{"branch", TtdOptionText(branch)}});
        StateNode value = reply.body;
        if (!reply.Ok())
            value["error"] = reply.message;
        return StateNodeToLua(ts, value);
    });

    // ttd_branch_create([{name = "...", frame = F, tinframe = T, branch = <b>, reason = "...", tags = {...},
    // note = "...", pinned = true, run = "<run id>", created_by = "...", media = "ask"|"empty"|"keep",
    // run_frames = N, input = {"0:a", "10:caps+5:4"}, read = {"#5C00:16"}, go_back = true}]): a branch at the
    // moment (default: here); with run_frames the machine runs it headless N frames fed by the input and records
    // it. {created, branch, parent, at, ran_frames, aborted, end, reads, returned_to, state, text} or
    // {created = false, error = ...}
    lua.set_function("ttd_branch_create", [this](sol::this_state ts, sol::optional<sol::table> argsTable)
                                               -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                if (key.is<std::string>())
                    options[key.as<std::string>()] = TtdOptionText(value);
        const ttd::TTDReply reply = TtdRun("branch-create", options);
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["created"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    // ttd_branch_prune({run = "...", created_by = "...", tag = ..., ... [, dry_run = false]}): the branches the
    // filter picks, deleted with dry_run = false (default: a dry run). {dry_run, would_delete | deleted = {...},
    // kept = {...}, frames, file_bytes, text} or {error = ...}
    lua.set_function("ttd_branch_prune", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            if (key.is<std::string>())
                options[key.as<std::string>()] = TtdOptionText(value);
        const ttd::TTDReply reply = TtdRun("branch-prune", options);
        StateNode value = reply.body;
        if (!reply.Ok())
            value["error"] = reply.message;
        return StateNodeToLua(ts, value);
    });

    // ttd_branch_delete(branch [, force]): the branch and its descendants (force: pinned ones too);
    // {deleted, removed = {ids}, removed_names = {...}, text} or {deleted = false, error = ...}
    lua.set_function("ttd_branch_delete", [this](sol::this_state ts, sol::object branch,
                                                  sol::optional<bool> force) -> sol::object {
        std::map<std::string, std::string> options{{"branch", TtdOptionText(branch)}};
        if (force && *force)
            options["force"] = "true";
        const ttd::TTDReply reply = TtdRun("branch-delete", options);
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["deleted"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });

    // -----------------------------------------------------------------
    // Phase 4 — Reverse search + dump + instruction step
    // -----------------------------------------------------------------

    // ttd_dump(path [, lineage]): lineage = true saves only the active branch's lineage as one linear history.
    // true, or false and the reason (the system's words, a full disk only when it is, the partial file removed)
    lua.set_function("ttd_dump", [this](sol::this_state ts, const std::string& path,
                                        sol::optional<bool> lineage) -> sol::variadic_results {
        std::map<std::string, std::string> options{{"path", path}};
        if (lineage && *lineage)
            options["lineage"] = "true";
        const ttd::TTDReply reply = TtdRun("dump", options);
        sol::variadic_results results;
        results.push_back(sol::make_object(ts, reply.Ok()));
        if (!reply.Ok())
            results.push_back(sol::make_object(ts, reply.message));
        return results;
    });

    // Loading refuses a session recorded on a different machine model: a
    // checkpoint is raw RAM pages plus a chipset snapshot, so it only
    // restores into an instance of the model it came from. Returns a table
    // with ok/error so scripts can report the reason.
    lua.set_function("ttd_load", [this](sol::this_state ts, const std::string& path) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("load", {{"path", path}})));
    });

    // ttd_export_clip(from_frame, to_frame, dir, [chunk]) - frames from..to as a lossless
    // clip in dir (final picture, plane B when zxdlss is on, frame meta), one call instead
    // of a seek and a capture per frame; not while recording. {ok, frames, bytes, planeb,
    // width, height, seconds, path} or {ok = false, error}
    lua.set_function("ttd_export_clip", [this](sol::this_state ts, uint64_t fromFrame, uint64_t toFrame,
                                                const std::string& dir, sol::optional<uint32_t> chunkOpt) -> sol::object {
        std::map<std::string, std::string> options{
            {"from", std::to_string(fromFrame)}, {"to", std::to_string(toFrame)}, {"path", dir}};
        if (chunkOpt)
            options["chunk"] = std::to_string(*chunkOpt);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("export-clip", options)));
    });

    // "When did the program ...": ttd_port_events(event, [arg], [options])
    // over the session's port journals (ttdportsearch.h). event: "key",
    // "ear", "ay-read", "ay-write", "ay-select", "border", "beeper", "in",
    // "out"; arg: a key name or an AY register; options: a table of
    // ttd::ApplyPortQueryOption names (limit, newest, from, to, port,
    // port_mask, value, value_mask, match, trigger, ay_register; file = a
    // .ttd path searched without loading it; context = each hit's
    // registers at its instruction, default on except while recording)
    lua.set_function("ttd_port_events", [this](sol::this_state ts, const std::string& event, sol::object argObj,
                                                 sol::object optionsObj) -> sol::object {
        std::map<std::string, std::string> options{{"event", event}};
        if (argObj.valid() && argObj.get_type() != sol::type::lua_nil)
            options["arg"] = TtdOptionText(argObj);
        if (optionsObj.is<sol::table>())
            for (const auto& [key, value] : optionsObj.as<sol::table>())
                options[key.as<std::string>()] = TtdOptionText(value);
        const ttd::TTDReply reply = TtdRun("port-events", options);
        StateNode value = TtdScriptValue(reply);
        if (reply.Ok())
            value["ok"] = true;
        return StateNodeToLua(ts, value);
    });

    lua.set_function("ttd_find_last", [this](sol::this_state ts, sol::object firstArgOpt,
                                               sol::optional<std::string> accessOpt,
                                               sol::optional<uint8_t> valueOpt,
                                               sol::optional<uint16_t> pcFromOpt,
                                               sol::optional<uint16_t> pcToOpt,
                                               sol::optional<uint64_t> beforeFrameOpt,
                                               sol::optional<uint32_t> beforeTinOpt,
                                               sol::optional<uint32_t> physPageOpt,
                                               sol::optional<uint16_t> addrFromOpt,
                                               sol::optional<uint16_t> addrToOpt,
                                               sol::optional<std::string> spaceOpt) -> sol::object {
        std::map<std::string, std::string> options;
        if (firstArgOpt.is<sol::table>())
        {
            // A table of options; the camelCase spellings are accepted as before
            static const std::map<std::string, std::string> aliases = {
                {"addrFrom", "addr_from"}, {"addrTo", "addr_to"}, {"pcFrom", "pc_from"},
                {"pcTo", "pc_to"},         {"physPage", "phys_page"}};
            for (const auto& [key, value] : firstArgOpt.as<sol::table>())
            {
                const std::string name = key.as<std::string>();
                const auto alias = aliases.find(name);
                options[alias == aliases.end() ? name : alias->second] = TtdOptionText(value);
            }
        }
        else
        {
            if (firstArgOpt.get_type() == sol::type::number)
                options["addr"] = TtdOptionText(firstArgOpt);
            if (addrFromOpt)
                options["addr_from"] = std::to_string(*addrFromOpt);
            if (addrToOpt)
                options["addr_to"] = std::to_string(*addrToOpt);
            if (accessOpt)
                options["access"] = *accessOpt;
            if (valueOpt)
                options["value"] = std::to_string(*valueOpt);
            if (pcFromOpt)
                options["pc_from"] = std::to_string(*pcFromOpt);
            if (pcToOpt)
                options["pc_to"] = std::to_string(*pcToOpt);
            if (physPageOpt)
                options["phys_page"] = std::to_string(*physPageOpt);
            if (spaceOpt)
                options["space"] = *spaceOpt;   // "vram" / "cache": the address is an offset in that memory
            if (beforeFrameOpt)
            {
                options["before_frame"] = std::to_string(*beforeFrameOpt);
                options["before_tin"] = std::to_string(beforeTinOpt.value_or(0));
            }
        }
        StateNode value = TtdScriptValue(TtdRun("find-last", options));
        if (!value.find("found"))
            value["found"] = false;
        return StateNodeToLua(ts, value);
    });

    lua.set_function("ttd_step_instruction_back", [this]() -> bool {
        const ttd::TTDReply reply = TtdRun("step-instruction", {{"dir", "back"}});
        return reply.Ok() && reply.body.find("stepped")->b;
    });

    lua.set_function("ttd_step_instruction_forward", [this]() -> bool {
        const ttd::TTDReply reply = TtdRun("step-instruction", {{"dir", "forward"}});
        return reply.Ok() && reply.body.find("stepped")->b;
    });

    // -----------------------------------------------------------------
    // Phase 4 — Reverse execution (multi-step)
    // -----------------------------------------------------------------

    lua.set_function("ttd_reverse_step", [this](sol::optional<uint32_t> countOpt) -> bool {
        const ttd::TTDReply reply = TtdRun("reverse-step", {{"count", std::to_string(countOpt.value_or(1))}});
        return reply.Ok() && reply.body.find("reached")->b;
    });

    lua.set_function("ttd_reverse_step_tstates", [this](uint64_t tstates) -> bool {
        const ttd::TTDReply reply = TtdRun("reverse-step", {{"tstates", std::to_string(tstates)}});
        return reply.Ok() && reply.body.find("reached")->b;
    });

    // ttd_reverse_step_scanlines(n): n scanlines of the machine back (lands on an instruction start)
    lua.set_function("ttd_reverse_step_scanlines", [this](uint64_t scanlines) -> bool {
        const ttd::TTDReply reply = TtdRun("reverse-step", {{"scanlines", std::to_string(scanlines)}});
        return reply.Ok() && reply.body.find("reached")->b;
    });

    // ttd_reverse_step_over(): back over a routine when the instruction before is its return, else one instruction
    lua.set_function("ttd_reverse_step_over", [this]() -> bool {
        const ttd::TTDReply reply = TtdRun("reverse-step-over", {});
        return reply.Ok() && reply.body.find("stepped")->b;
    });

    // -----------------------------------------------------------------
    // Positions on a sound card's CPU (gs-debugger design §8): cpu "gs" / "neogs" / "ms" / "multisound"; the
    // main CPU follows by replay. Each returns the move's reply: status ("ok", "at_start", "no_hit", ...),
    // reached, from, target, card = {instruction, pc, card_position, main = {frame, tinframe, pc, t_offset,
    // inside}}; on a refusal ok = false and error
    // -----------------------------------------------------------------

    // ttd_card_seek(cpu, instruction): the card at its instruction count
    lua.set_function("ttd_card_seek", [this](sol::this_state ts, const std::string& cpu, uint64_t instruction) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("seek", {{"cpu", cpu}, {"instruction", std::to_string(instruction)}})));
    });

    // ttd_card_step(cpu [, n]): n card instructions forward (negative: back; default 1)
    lua.set_function("ttd_card_step", [this](sol::this_state ts, const std::string& cpu, sol::optional<int64_t> nOpt) -> sol::object {
        const int64_t n = nOpt.value_or(1);
        const uint64_t count = n < 0 ? 0 - static_cast<uint64_t>(n) : static_cast<uint64_t>(n);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("step-instruction", {{"cpu", cpu},
                                                                            {"dir", n < 0 ? "back" : "forward"},
                                                                            {"count", std::to_string(count)}})));
    });

    // ttd_card_reverse_step_over(cpu): the card's step over back: one card instruction back, or past a whole card
    // routine to its CALL / RST when that instruction is its return (over_call, skipped in the reply)
    lua.set_function("ttd_card_reverse_step_over", [this](sol::this_state ts, const std::string& cpu) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("reverse-step-over", {{"cpu", cpu}})));
    });

    // ttd_card_step_over(cpu) / ttd_card_step_out(cpu): the card's step over / step out through the history (its
    // breakpoints end it first; history_end: not ended in the history, the card at its end)
    lua.set_function("ttd_card_step_over", [this](sol::this_state ts, const std::string& cpu) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("step-instruction", {{"cpu", cpu}, {"kind", "over"}})));
    });
    lua.set_function("ttd_card_step_out", [this](sol::this_state ts, const std::string& cpu) -> sol::object {
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("step-instruction", {{"cpu", cpu}, {"kind", "out"}})));
    });

    // ttd_card_run_to_cursor(cpu, pc [, max_frames]): the card's run to the cursor through the history (its next
    // instruction at pc, its breakpoints first; not reached: reached false, status no_hit, nothing moved)
    lua.set_function("ttd_card_run_to_cursor", [this](sol::this_state ts, const std::string& cpu, uint32_t pc,
                                                      sol::optional<uint64_t> maxFrames) -> sol::object {
        std::map<std::string, std::string> options{
            {"cpu", cpu}, {"dir", "forward"}, {"kind", "to"}, {"pc", std::to_string(pc)}};
        if (maxFrames)
            options["max_frames"] = std::to_string(*maxFrames);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("step-instruction", options)));
    });

    // ttd_card_reverse_continue(cpu [, max_frames]): back to the card's previous breakpoint hit
    lua.set_function("ttd_card_reverse_continue", [this](sol::this_state ts, const std::string& cpu,
                                                         sol::optional<uint64_t> maxFrames) -> sol::object {
        std::map<std::string, std::string> options{{"cpu", cpu}};
        if (maxFrames)
            options["max_frames"] = std::to_string(*maxFrames);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("reverse-continue", options)));
    });

    // ttd_card_position([cpu]): the card's readout (position verb's cards[]); every card's array without cpu,
    // nil for a cpu without a card
    lua.set_function("ttd_card_position", [this](sol::this_state ts, sol::optional<std::string> cpu) -> sol::object {
        const ttd::TTDReply reply = TtdRun("position");
        const StateNode* cards = reply.Ok() ? reply.body.find("cards") : nullptr;
        if (!cpu)
            return StateNodeToLua(ts, cards ? *cards : StateNode::Array());
        std::string name = *cpu;
        DebugCpuId id = DebugCpuId::Main;
        if (cards && DebugManager::ParseCpuName(name, id))
            for (const StateNode& card : cards->items)
                if (card.find("cpu")->s == DebugCpuName(id))
                    return StateNodeToLua(ts, card);
        return sol::make_object(ts, sol::lua_nil);
    });

    // ttd_reverse_continue({pcs}, {phys_page=, all=, first_per_pc=, limit=, before_frame=, before_tin=, before=, from_frame=,
    // condition=})
    lua.set_function("ttd_reverse_continue", [this](sol::this_state ts, sol::table pcsTable,
                                                    sol::optional<sol::table> optionsTable) -> sol::object {
        std::string pcs;
        for (auto& pair : pcsTable)
            pcs += (pcs.empty() ? "" : ",") + TtdOptionText(pair.second);
        std::map<std::string, std::string> options;
        if (optionsTable)
            for (const auto& [key, value] : *optionsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        options["pcs"] = pcs;
        StateNode value = TtdScriptValue(TtdRun("reverse-continue", options));
        if (!value.find("matched"))
            value["matched"] = false;
        return StateNodeToLua(ts, value);
    });
}
