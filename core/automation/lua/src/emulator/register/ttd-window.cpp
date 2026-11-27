// LuaEmulator bindings: time-travel probes, screen and ports reads, the window.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterTimeTravelAndWindow(sol::state& lua)
{
    // ttd_probe_frames(probe or {probe, ...}, {from_frame=, to_frame=, limit=}): a game profile's probes over the
    // history ({probe="exec", address=0x8000, page=, end=, port_mask=, ["if"]=}), replayed, the machine put back
    lua.set_function("ttd_probe_frames", [this](sol::this_state ts, sol::table probesTable,
                                                sol::optional<sol::table> optionsTable) -> sol::object {
        StateNode probes = StateNode::Array();
        auto add = [&](const sol::table& probe) {
            StateNode node = StateNode::Object();
            for (const auto& [key, value] : probe)
                node[key.as<std::string>()] = TtdOptionText(value);
            probes.push(node);
        };
        if (probesTable["probe"].valid())
            add(probesTable);
        else
            for (const auto& [index, probe] : probesTable)
                if (probe.is<sol::table>())
                    add(probe.as<sol::table>());
        std::map<std::string, std::string> options;
        if (optionsTable)
            for (const auto& [key, value] : *optionsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        options["probes"] = StateNodeToJsonText(probes);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("probe-frames", options)));
    });

    lua.set_function("ttd_coverage_probe", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-probe", options)));
    });

    lua.set_function("ttd_coverage_scan", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-scan", options)));
    });

    lua.set_function("ttd_coverage_rare", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-rare", options)));
    });

    lua.set_function("ttd_coverage_summary", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-summary", options)));
    });

    // ttd_timeline{from_frame=, to_frame=, bucket_size=, limit=, buckets=, ports=, min_phase_ms=, file=, branch=}:
    // the recording cut into phases of the same activity, from what the session holds (no replay); file= a .ttd
    // file (or a recording's folder) read without loading it, branch= a line of that file
    lua.set_function("ttd_timeline", [this](sol::this_state ts, sol::optional<sol::table> argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("timeline", options)));
    });

    // ttd_cause_chain(target, {frame=, tinframe=, phys_page=, depth=, max_links=, max_frames=, flags=, text=}):
    // where a value came from. target: a Z80 address (a number, or "#A000") or a register name ("A", "HL", "E'")
    lua.set_function("ttd_cause_chain", [this](sol::this_state ts, sol::object target,
                                               sol::optional<sol::table> argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        std::vector<ttd::TTDCauseReg> regs;
        const std::string name = TtdOptionText(target);
        if (target.get_type() == sol::type::string && ttd::TTDCauseRegsByName(name, regs))
            options["reg"] = name;
        else
            options["addr"] = name;
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("cause-chain", options)));
    });

    // ttd_backtrace({frame=, tinframe=, max_depth=, max_slots=, text=}): the call stack at a moment (default: the
    // current position) - each return address above SP with the CALL / RST / interrupt that pushed it and when
    lua.set_function("ttd_backtrace", [this](sol::this_state ts, sol::optional<sol::table> argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("backtrace", options)));
    });

    // ttd_find_event(kind, {cond=, addr=, addr_to=, phys_page=, size=, value=, bit=, exact=, min_changed=, dir=,
    // port=, port_mask=, value_mask=, key=, pressed=, order=, from_frame=, to_frame=, limit=, max_frames=, text=}):
    // the first (or last) moments an event happened. kind: mem, screen, exec, port, border, key
    lua.set_function("ttd_find_event", [this](sol::this_state ts, const std::string& kind,
                                              sol::optional<sol::table> argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        options["kind"] = kind;
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("find-event", options)));
    });

    // ttd_explain_frame([frame], {sections = "routines,memory", limit = N, text = true}): what one frame did -
    // time split, routines by time, interrupts, paging, ports, memory changed with its writers, screen, keys
    lua.set_function("ttd_explain_frame", [this](sol::this_state ts, sol::optional<sol::object> frame,
                                                 sol::optional<sol::table> argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        sol::optional<sol::table> table = argsTable;
        if (frame && frame->get_type() == sol::type::table)
            table = frame->as<sol::table>();
        else if (frame && frame->get_type() != sol::type::lua_nil)
            options["frame"] = TtdOptionText(*frame);
        if (table)
            for (const auto& [key, value] : *table)
                options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("explain-frame", options)));
    });

    // ttd_diff{from_frame = F, from_tinframe = T, to_frame = G, to_tinframe = T, sections = "cpu,memory", limit = N,
    // writers = false, text = true}: what differs between two moments (the second defaults to the current one) -
    // registers, latches, mapping, memory with the last writes, screen, devices, the keys and markers in between
    lua.set_function("ttd_diff", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("diff", options)));
    });

    // ttd_replay_check{from_frame = F, to_frame = G, stride = N, limit = N, text = true}: each frame replayed from its
    // checkpoint and compared with the next checkpoint (default: the whole history) - what diverged, by part
    lua.set_function("ttd_replay_check", [this](sol::this_state ts, sol::optional<sol::table> argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        if (argsTable)
            for (const auto& [key, value] : *argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("replay-check", options)));
    });

    // ttd_seek_check(true | false | nil): every seek also replay-checks the frame it lands in (off by default);
    // nil keeps the setting. Returns {enabled, seek_checks, seek_check_divergences, last_seek_divergence}
    lua.set_function("ttd_seek_check", [this](sol::this_state ts, sol::optional<bool> enabled) -> sol::object {
        std::map<std::string, std::string> options;
        if (enabled)
            options["enabled"] = *enabled ? "true" : "false";
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("seek-check", options)));
    });

    // A memory at a past checkpoint and what changed between two, from the store (no seek):
    // ttd_memory_at{space = "vram", frame = F, offset = 0x4805, length = 16}
    // ttd_memory_diff{space = "ram5", from_frame = F1, to_frame = F2, limit = 100}
    for (const char* verb : {"memory-at", "memory-diff"})
    {
        const std::string name = std::string("ttd_") + (verb[7] == 'a' ? "memory_at" : "memory_diff");
        lua.set_function(name, [this, verb](sol::this_state ts, sol::table argsTable) -> sol::object {
            std::map<std::string, std::string> options;
            for (const auto& [key, value] : argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
            return StateNodeToLua(ts, TtdScriptValue(TtdRun(verb, options)));
        });
    }

    // Reads of the past without seeking (PLAN #122 S1, V2):
    // ttd_memory_series{ranges = "0x5B00:32, ram7:0x1800:768" | {"0x5B00:32", {space = "ram", page = 7,
    //                   offset = 0x1800, length = 768}}, from_frame = F1, to_frame = F2, step = 1,
    //                   frames = {F, ...}, changes_only = true, format = "hex", limit = 1000, max_bytes = N}
    // ttd_screen_at{frame = F, which = "shown" | "normal" | "shadow", format = "png" | "bitmap" | "attributes" |
    //               "raw", area = "full" | "screen", encoding = "base64" | "hex"}
    lua.set_function("ttd_memory_series", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
        {
            const std::string name = key.as<std::string>();
            if (name != "ranges" || value.get_type() != sol::type::table)
            {
                options[name] = TtdOptionText(value);
                continue;
            }
            // A list of ranges: strings as they are, tables as "space:offset:length" (page N: space ramN)
            const sol::table list = value.as<sol::table>();
            std::string text;
            for (size_t i = 1; i <= list.size(); ++i)
            {
                const sol::object item = list[i];
                std::string one = TtdOptionText(item);
                if (item.get_type() == sol::type::table)
                {
                    const sol::table r = item.as<sol::table>();
                    const sol::object page = r["page"];
                    const sol::object space = r["space"];
                    const sol::object address = r["address"];
                    const sol::object offset = r["offset"];
                    const sol::object length = r["length"];
                    const std::string where = page.valid()    ? "ram" + TtdOptionText(page)
                                              : space.valid() ? TtdOptionText(space)
                                                              : std::string("cpu");
                    one = where + ":" + TtdOptionText(address.valid() ? address : offset) + ":" +
                          TtdOptionText(length);
                }
                text += (text.empty() ? "" : ",") + one;
            }
            options[name] = text;
        }
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("memory-series", options)));
    });
    lua.set_function("ttd_screen_at", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
        std::map<std::string, std::string> options;
        for (const auto& [key, value] : argsTable)
            options[key.as<std::string>()] = TtdOptionText(value);
        return StateNodeToLua(ts, TtdScriptValue(TtdRun("screen-at", options)));
    });

    // ====================================================================
    // Phase-2 analysis capabilities — parity with WebAPI/MCP/CLI:
    // step out, skip until, memory find, screen digest, beam, frame cost,
    // coverage, AY log, audio capture, assembler, label resolve, listings.
    // ====================================================================

    // Step out of the current subroutine (emulation ends paused)
    lua.set_function("step_out", [this](sol::optional<std::string> cpuName, sol::this_state ts) -> sol::table {
        sol::state_view lua_view(ts);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
            return CardStep(lua_view, cpu, "out", 1);

        try
        {
            emulator->StepOut();
            Z80State* z80 = emulator->GetZ80State();
            if (z80)
            {
                result["pc"] = z80->pc;
                result["sp"] = z80->sp;
            }
            result["ok"] = true;
        }
        catch (const std::exception& e)
        {
            result["ok"] = false;
            result["error"] = e.what();
        }
        return result;
    });

    // card_run_to(until, [value], [cpu]): a card CPU's run to a moment (RunControl::CardRunTo; cpu "gs" by default):
    // "cycle" value, "int", "command", "main_t" value. The reply as a table with ok, or ok = false and error
    lua.set_function("card_run_to", [this](const std::string& until, sol::optional<uint64_t> value,
                                           sol::optional<std::string> cpuName, sol::this_state ts) -> sol::table {
        const DebugCpuId cpu = ScriptCpu(cpuName ? cpuName : sol::optional<std::string>(std::string("gs")));
        if (!IsCardCpu(cpu))
            throw std::runtime_error("cpu must be a card's: gs or ms");
        return CardRun(sol::state_view(ts), [&](execution::RunControl& control, const execution::Requester& requester) {
            return control.CardRunTo(requester, cpu, until, value.value_or(0));
        });
    });

    // Fast-forward until PC reaches the target (breakpoints skipped). skip_until(pc, [max_tstates], [cpu]) or
    // skip_until(pc, cpu): "gs" / "ms" runs that sound card CPU to the cursor (CardStep "to"; breakpoints end it)
    lua.set_function("skip_until", [this](sol::object pcValue, sol::object second,
                                          sol::optional<std::string> cpuName, sol::this_state ts) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::optional<unsigned> maxTStatesOpt;
        if (second.is<std::string>() && !cpuName)
            cpuName = second.as<std::string>();
        else if (second.is<unsigned>())
            maxTStatesOpt = second.as<unsigned>();
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }

        uint32_t target32 = 0;
        if (pcValue.is<std::string>())
        {
            try { target32 = static_cast<uint32_t>(std::stoul(pcValue.as<std::string>(), nullptr, 0)); }
            catch (...) { result["error"] = "invalid pc"; return result; }
        }
        else if (pcValue.is<int>())
        {
            target32 = static_cast<uint32_t>(pcValue.as<int>());
        }
        else
        {
            result["error"] = "pc must be a number or hex string";
            return result;
        }

        if (target32 > 0xFFFF) { result["error"] = "pc out of 16-bit range"; return result; }
        const uint16_t target = static_cast<uint16_t>(target32);
        if (const DebugCpuId cpu = ScriptCpu(cpuName); IsCardCpu(cpu))
            return CardStep(sol::state_view(ts), cpu, "to", 1, target);

        // Safety budget: default 100 frames of emulated time, hard cap 200 s
        EmulatorContext* context = emulator->GetContext();
        unsigned maxTStates = maxTStatesOpt.value_or(0);
        if (maxTStates == 0 && context)
            maxTStates = context->config.frame * 100;
        if (maxTStates == 0)
            maxTStates = 6988800;
        if (maxTStates > 700000000u)
            maxTStates = 700000000u;

        emulator->RunUntilCondition([target](const Z80State& state) { return state.pc == target; }, maxTStates);

        Z80State* z80 = emulator->GetZ80State();
        result["hit"] = z80 && z80->pc == target;
        result["max_tstates"] = maxTStates;
        if (z80)
        {
            result["pc"] = z80->pc;
            result["sp"] = z80->sp;
        }
        return result;
    });

    // Search the CPU view of memory for a byte pattern (hex string or byte table)
    // mem_find(pattern [, start, end, alignment, max, space, mask]) - MemorySearch: hex text with ?? / A?
    // wildcards or a byte table; space "cpu" (default), "ram" (every RAM page) or a page ("ram5", "rom2")
    lua.set_function("mem_find",
                     [this](sol::this_state s, sol::object patternValue, sol::optional<uint32_t> startOpt,
                            sol::optional<uint32_t> endOpt, sol::optional<unsigned> alignOpt,
                            sol::optional<unsigned> maxOpt, sol::optional<std::string> spaceOpt,
                            sol::optional<std::string> maskOpt) -> sol::object {
        StateNode error = StateNode::Object();
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            error["error"] = "no emulator";
            return StateNodeToLua(s, error);
        }
        std::vector<uint8_t> bytes;
        const bool table = patternValue.is<sol::table>();
        if (table)
            for (auto& pair : patternValue.as<sol::table>())
                bytes.push_back(static_cast<uint8_t>(pair.second.as<int>() & 0xFF));
        MemorySearchRequest request;
        std::string message;
        std::string text;
        if (patternValue.get_type() == sol::type::number)
            text = MemorySearch::NumberPattern(static_cast<uint64_t>(patternValue.as<int64_t>()));
        else if (patternValue.is<std::string>())
            text = patternValue.as<std::string>();
        if (!MemorySearch::BuildRequest(text, table ? &bytes : nullptr,
                                        maskOpt.value_or(""), spaceOpt.value_or(""), startOpt.value_or(0),
                                        endOpt.value_or(0xFFFFFFFFu), maxOpt.value_or(64), alignOpt.value_or(1),
                                        request, message))
        {
            error["error"] = message;
            return StateNodeToLua(s, error);
        }
        return StateNodeToLua(s, MemorySearch::ToState(request, MemorySearch::Search(emulator->GetContext(), request)));
    });

    // Screen-area FNV-1a-64 digest — change detection without pixel transfer.
    // screen_digest()                            -> default banks (both screen pages on 128K)
    // screen_digest(start, end)                  -> explicit Z80 range
    // screen_digest(nil, nil, nil, "active")     -> banks of the video mode actually
    //                                               displayed (P1-3; explicit range wins)
    lua.set_function("screen_digest", [this](sol::optional<unsigned> startOpt,
                                             sol::optional<unsigned> endOpt,
                                             sol::optional<bool> includeBorderOpt,
                                             sol::optional<std::string> modeOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }

        EmulatorContext* context = emulator->GetContext();
        if (!context || !context->pMemory || !context->pScreen)
        {
            result["error"] = "context not initialized";
            return result;
        }

        // One computation for every interface (ScreenDigestCompute): the same pages, surface and change tracking
        ScreenDigestQuery query;
        if (modeOpt.has_value())
        {
            if (*modeOpt == "active")
                query.active = true;
            else if (*modeOpt != "default")
            {
                result["error"] = "mode must be 'default' or 'active'";
                return result;
            }
        }
        if (startOpt.has_value() || endOpt.has_value())
        {
            query.range = true;
            query.start = static_cast<uint16_t>(startOpt.value_or(0x4000));
            query.end = static_cast<uint16_t>(endOpt.value_or(0x7FFF));
            if (query.start > query.end)
            {
                result["error"] = "start must be <= end";
                return result;
            }
        }
        query.includeBorder = includeBorderOpt.value_or(true);
        const ScreenDigestResult r = ScreenDigestCompute::Compute(context, query);
        if (!r.ok)
        {
            result["error"] = r.error;
            return result;
        }
        if (r.range)
        {
            result["range_start"] = r.start;
            result["range_end"] = r.end;
            result["range_digest"] = r.rangeDigest;
        }
        else if (r.deviceSurface)
        {
            // A picture outside the RAM pages (the Sprinter's video RAM)
            sol::table activeSurface = lua_view.create_table();
            activeSurface["video_mode"] = r.videoMode;
            activeSurface["memory"] = r.surface.name;
            activeSurface["bytes"] = r.surface.bytes;
            activeSurface["digest"] = r.surface.digest;
            result["active_surface"] = activeSurface;
        }
        else
        {
            if (r.activeSurface)
            {
                sol::table pages = lua_view.create_table();
                for (uint16_t page : r.activePages)
                    pages.add(page);
                sol::table activeSurface = lua_view.create_table();
                activeSurface["video_mode"] = r.videoMode;
                activeSurface["pages"] = pages;
                result["active_surface"] = activeSurface;
            }
            sol::table perBank = lua_view.create_table();
            for (const auto& [page, digest] : r.banks)
                perBank[page] = digest;
            result["banks"] = perBank;
        }
        if (r.includeBorder)
            result["border_color"] = r.border;
        result["combined"] = r.combined;
        result["frame"] = r.frame;
        result["algorithm"] = "fnv1a-64";
        result["changed"] = r.changed;
        result["previous_digest"] = r.previousDigest;
        if (r.previousFrame != 0)
            result["previous_digest_frame"] = r.previousFrame;
        return result;
    });

    // Static port-map introspection (P1-5): which devices answer which I/O
    // ports on this model, under which gating conditions, plus the live
    // routing flags. Mirrors GET /api/v1/emulator/{id}/ports
    // (PortDecoder::getPortMapEntries / GetMouseRoutingState, single source).
    lua.set_function("ports_map", [this](sol::this_state s) -> sol::variadic_results {
        sol::variadic_results results;
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return mouseError(s, "no emulator");

        EmulatorContext* context = emulator->GetContext();
        if (!context || !context->pPortDecoder)
            return mouseError(s, "context not initialized");

        auto portHex = [](uint16_t value) {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%04X", value);
            return std::string(text);
        };

        sol::state_view lua(s);
        sol::table result = lua.create_table();
        result["model"] = Config::GetModelFullName(context->config.mem_model);

        sol::table entries = lua.create_table();
        for (const PortMapEntry& entry : context->pPortDecoder->getPortMapEntries())
        {
            sol::table item = lua.create_table();
            item["port"] = portHex(entry.port);
            item["mask"] = portHex(entry.mask);
            item["match"] = portHex(entry.match);
            item["device"] = entry.device;
            if (entry.gate)  // absent key = ungated (WebAPI sends null)
                item["gate"] = entry.gate;

            // Tagged registry fields (P1-2): names from the core single
            // source - identical strings on WebAPI /ports, MCP and Python
            sol::table tagNames = lua.create_table();
            for (const std::string& tagName : PortTagSetToStrings(entry.tags))
                tagNames.add(tagName);
            item["tags"] = tagNames;  // empty table = untagged row
            const char* latchName = PagingLatchToString(entry.latch);
            if (latchName)  // absent key = no live-value binding
            {
                item["latch"] = latchName;
                // The latch's value now, and decoded (PortDecoder::ReadPagingLatch / DecodePagingLatch)
                const uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, context->emulatorState);
                item["latch_value"] = value;
                sol::table fields = lua.create_table();
                for (const DecodedLatchField& field :
                     DecodePagingLatch(entry.latch, value, context->config.mem_model, context->config.ramsize))
                {
                    if (field.isBool)
                        fields[field.key] = field.boolValue;
                    else
                        fields[field.key] = field.intValue;
                }
                item["latch_fields"] = fields;
            }

            entries.add(item);
        }
        result["entries"] = entries;

        bool mouseDecoded = false;
        std::string mouseNote;
        context->pPortDecoder->GetMouseRoutingState(mouseDecoded, mouseNote);

        const CONFIG& config = context->config;
        const EmulatorState& state = context->emulatorState;
        sol::table live = lua.create_table();
        live["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;
        live["mouse_ports_decoded"] = mouseDecoded;
        live["mouse_routing_note"] = mouseNote;
        const bool scorpion = IsScorpionModel(config.mem_model);
        if (scorpion)
            live["shadow_monitor_paged"] = (state.p1FFD & 0x02) != 0;
        // else: key absent = the #1FFD latch does not exist on this model
        // Sprinter: the map / DOS / PN5 the port table is read with now (as WebAPI /ports)
        if (config.mem_model == MM_SPRINTER)
        {
            const StateNode sprinter = DeviceState::Sprinter(context);
            if (const StateNode* decoderNode = sprinter.find("decoder"))
                live["sprinter_port_table"] = StateNodeToLua(s, *decoderNode);
        }
        result["live"] = live;

        results.push_back(sol::make_object(s, result));
        return results;
    });

    // Tagged paging latches + bank table (P1-2 design).
    // Mirrors GET /api/v1/emulator/{id}/state/paging.
    lua.set_function("paging_state", [this](sol::this_state s) -> sol::variadic_results {
        sol::variadic_results results;
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return mouseError(s, "no emulator");

        EmulatorContext* context = emulator->GetContext();
        if (!context || !context->pPortDecoder || !context->pMemory)
            return mouseError(s, "context not initialized");

        auto hexByte = [](uint32_t value) {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%02X", value);
            return std::string(text);
        };
        auto hexWord = [](uint16_t value) {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%04X", value);
            return std::string(text);
        };

        sol::state_view lua(s);
        sol::table result = lua.create_table();
        const CONFIG& config = context->config;
        const EmulatorState& state = context->emulatorState;
        Memory& memory = *context->pMemory;
        PortDecoder* decoder = context->pPortDecoder;
        ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;

        result["model"] = Config::GetModelFullName(config.mem_model);
        if (IsProfiModel(config.mem_model))
        {
            // The board, its sync PROM and the keyboard on its connector (as GET /state/paging)
            result["profi_board"] = config.mem_model == MM_PROFI3 ? "v3" : "v5";
            result["profi_sync_prom"] = ProfiSyncPromName(
                ProfiResolveSyncProm(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model));
            result["profi_keyboard"] = ProfiKeyboardName(ProfiKeyboardInForce(context));
            // The hi-res clocks (design-hires.md): the CPU clock there (no turbo), the v5's ZQ3 and SB7
            result["profi_hires_cpu_hz"] = ProfiHiresCpuHz(config.mem_model == MM_PROFI, config.profi_zq3_mhz);
            result["profi_zq3_mhz"] = static_cast<int>(ProfiClampZq3(config.profi_zq3_mhz));
            result["profi_ay_clock"] = (config.mem_model == MM_PROFI && config.profi_ay_clock_new) ? "new" : "old";
        }
        result["paging_locked"] = (state.p7FFD & PORT_7FFD_LOCK) != 0;
        result["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;

        // Latches array
        sol::table latches = lua.create_table();
        for (const PortMapEntry& entry : decoder->GetPagingLatches(Tags(PortTag::Memory)))
        {
            sol::table latch = lua.create_table();
            latch["port"] = hexWord(entry.port);
            latch["device"] = entry.device ? entry.device : "";
            if (entry.gate) latch["gate"] = entry.gate;

            // Tag names + latch binding from the core single source -
            // identical strings on /state/paging, MCP, CLI and Python
            sol::table tagNames = lua.create_table();
            for (const std::string& tagName : PortTagSetToStrings(entry.tags))
                tagNames.add(tagName);
            latch["tags"] = tagNames;
            const char* latchName = PagingLatchToString(entry.latch);
            if (latchName)
                latch["latch"] = latchName;

            uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, state);
            latch["value"] = hexByte(value);

            // Decoded bits: core §5.1 dictionary (DecodePagingLatch) with
            // native ints/bools, matching /state/paging verbatim
            sol::table decoded = lua.create_table();
            for (const DecodedLatchField& field : DecodePagingLatch(entry.latch, value, config.mem_model, config.ramsize))
            {
                decoded[field.key] = field.isBool ? sol::make_object(s, field.boolValue)
                                                  : sol::make_object(s, field.intValue);
            }
            if (decoded.size() > 0)
                latch["decoded"] = decoded;
            latches.add(latch);
        }
        result["latches"] = latches;

        // Banks array
        sol::table banks = lua.create_table();
        for (int i = 0; i < 4; ++i)
        {
            sol::table bank = lua.create_table();
            bank["bank"] = i;
            const char* ranges[] = {"0x0000-0x3FFF", "0x4000-0x7FFF", "0x8000-0xBFFF", "0xC000-0xFFFF"};
            bank["address_range"] = ranges[i];

            if (i == 0 && memory.IsBank0ROM())
            {
                bank["type"] = "ROM";
                uint8_t romPage = memory.GetROMPage();
                bank["page"] = static_cast<int>(romPage);
                if (rom)
                {
                    uint8_t* pagePtr = memory.ROMPageHostAddress(romPage);
                    if (pagePtr)
                    {
                        std::string sig = rom->CalculateSignature(pagePtr, 0x4000);
                        // GetROMPageTitle carries the "Unknown ROM, <digest>"
                        // fallback and names blank pages and copies; role =
                        // core layout table (§5.2) - a role/name mismatch is
                        // the wrong-ROM signal
                        bank["name"] = rom->GetROMPageTitle(romPage);
                        bank["signature"] = sig;
                    }
                    bank["role"] = rom->GetROMPageRole(romPage);
                }
            }
            else
            {
                bank["type"] = "RAM";
                switch (i) {
                    case 0: bank["page"] = static_cast<int>(memory.GetRAMPageForBank0()); break;
                    case 1: bank["page"] = static_cast<int>(memory.GetRAMPageForBank1()); break;
                    case 2: bank["page"] = static_cast<int>(memory.GetRAMPageForBank2()); break;
                    case 3: bank["page"] = static_cast<int>(memory.GetRAMPageForBank3()); break;
                }
            }
            // The CPU waits for the video logic there (Core::IsSlotContended)
            bank["contended"] = context->pCore && context->pCore->IsSlotContended(static_cast<uint8_t>(i));
            // A write reaches the page (the mapper's view: ROM, TS-Conf W0_WE, ...)
            bank["writable"] = memory.IsWindowWritable(static_cast<uint8_t>(i));
            banks.add(bank);
        }

        // Sprinter: the PLD maps the windows - kind and physical page from DeviceState::SprinterPaging
        // (the /state/paging view), and the whole view as `sprinter`
        if (config.mem_model == MM_SPRINTER)
        {
            const StateNode sprinter = DeviceState::SprinterPaging(context);
            if (const StateNode* windows = sprinter.find("windows"))
            {
                for (size_t i = 0; i < windows->items.size() && i < 4; i++)
                {
                    sol::table bank = banks[i + 1];
                    if (const StateNode* kind = windows->items[i].find("kind"))
                        bank["type"] = kind->s;
                    if (const StateNode* page = windows->items[i].find("page"))
                        bank["page"] = page->i;
                }
            }
            result["sprinter"] = StateNodeToLua(s, sprinter);
        }
        // GMX: the card's register file (the /state/paging `gmx` view)
        if (config.mem_model == MM_GMX)
            result["gmx"] = StateNodeToLua(s, DeviceState::GmxPaging(context));
        // Pentagon 2.666 LE: the window maps and the board latches (the /state/paging `p2666` view)
        if (config.mem_model == MM_P2666)
            result["p2666"] = StateNodeToLua(s, DeviceState::P2666Paging(context));
        result["banks"] = banks;

        results.push_back(sol::make_object(s, result));
        return results;
    });

    // Beam and video debug translation (PLAN #42): the same DeviceState reports every interface returns
    lua.set_function("beam_position", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoBeam(ctx));
    });
    lua.set_function("video_layout", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoLayout(ctx));
    });
    lua.set_function("video_pixel", [this](sol::this_state s, unsigned x, unsigned y, sol::optional<unsigned> layer) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoPixel(ctx, layer.value_or(0), x, y));
    });
    lua.set_function("video_pixel_at", [this](sol::this_state s, unsigned t) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoPixelAtBeam(ctx, t));
    });
    lua.set_function("video_address", [this](sol::this_state s, unsigned page, unsigned offset) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoAddress(ctx, page, offset));
    });
    // video_address_in(space, offset[, page]): "ram", "sprite_ram" or "palette"
    lua.set_function("video_address_in", [this](sol::this_state s, const std::string& space, unsigned offset,
                                                sol::optional<unsigned> page) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoAddressIn(ctx, space, page.value_or(0), offset));
    });
    lua.set_function("video_address_z80", [this](sol::this_state s, unsigned address) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoAddressZ80(ctx, address));
    });
    // video_changes([frames]): the video change log (DeviceState::VideoChanges): 1 = the last completed frame,
    // 2 (default) = the current one too while paused
    lua.set_function("video_changes", [this](sol::this_state s, sol::optional<unsigned> frames) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::VideoChanges(emulator->GetContext(), frames.value_or(2)));
    });
    lua.set_function("video_text", [this](sol::this_state s, sol::optional<unsigned> layer) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, DeviceState::VideoText(ctx, layer.value_or(0)));
    });
    // The operation log (machine state coordinator): reset, snapshot load, media changes, refusals included - the
    // OperationsStatus report every interface returns. since: entries after that seq; limit: the newest N (0: all)
    lua.set_function("operations", [this](sol::this_state s, sol::optional<uint64_t> since,
                                           sol::optional<uint64_t> limit) -> sol::object {
        return StateNodeToLua(s, OperationsStatus::Report(effectiveEmulator(), since.value_or(0),
                                                          static_cast<size_t>(limit.value_or(0))));
    });
    // Temporal effects (ZX DLSS de-flicker): status and switch, the TemporalStatus report every interface returns
    lua.set_function("video_temporal", [this](sol::this_state s) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        return StateNodeToLua(s, TemporalStatus::Report(ctx));
    });
    lua.set_function("video_temporal_set", [this](sol::this_state s, const std::string& name) -> sol::object {
        EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
        if (!TemporalStatus::Set(ctx, name))
        {
            StateNode error = StateNode::Object();
            error["ok"] = false;
            error["error"] = ctx ? "Unknown temporal algorithm '" + name + "'. Valid: " + TemporalStatus::OfferedList() + ", off"
                                 : std::string("No emulator selected");
            return StateNodeToLua(s, error);
        }
        return StateNodeToLua(s, TemporalStatus::Report(ctx));
    });
    // GUI window display settings (GPU, CRT effects / profile, temporal blending): the WindowDisplay report every
    // interface returns. window_display() reads; window_display_set({gpu = true, crt_profile = "basic",
    // blending = true, blending_frames = 3, blending_weights = "exponential", crt = false}) changes any of them the
    // way the View menu does. Headless: {ok = false, error, kind = "no-window"}
    lua.set_function("window_display", [](sol::this_state s) -> sol::object {
        const WindowDisplay::Result r = WindowDisplay::Get();
        return StateNodeToLua(s, r.Ok() ? WindowDisplay::Report(r.settings) : WindowDisplay::ErrorNode(r));
    });
    lua.set_function("window_display_set", [](sol::this_state s, sol::table changes) -> sol::object {
        WindowDisplayChange change;
        WindowDisplay::Result bad;
        bad.status = WindowDisplay::Status::BadParameter;
        for (const auto& [k, v] : changes)
        {
            const std::string key = k.is<std::string>() ? k.as<std::string>() : std::string("?");
            std::string word;
            // hud_notices = {["ram-page"] = false}, next_layers_shown = {sprites = false}: one key each
            const char* prefix = key == "hud_notices" ? WindowDisplay::kHudNoticePrefix
                                 : key == "next_layers_shown" ? WindowDisplay::kNextLayerPrefix
                                                              : nullptr;
            if (prefix != nullptr && v.get_type() == sol::type::table)
            {
                for (const auto& [id, flag] : v.as<sol::table>())
                {
                    const std::string name = id.is<std::string>() ? id.as<std::string>() : std::string("?");
                    const std::string onOff = flag.get_type() == sol::type::boolean ? (flag.as<bool>() ? "on" : "off")
                                                                                    : std::string("?");
                    if (!WindowDisplay::ApplyWord(change, prefix + name, onOff, bad.error))
                        return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
                }
                continue;
            }
            if (v.get_type() == sol::type::boolean)
                word = v.as<bool>() ? "on" : "off";
            else if (v.get_type() == sol::type::number)
                word = WindowDisplay::NumberWord(v.as<double>());  // 0.3 stays 0.3, whatever the locale
            else if (v.get_type() == sol::type::string)
                word = v.as<std::string>();
            if (word.empty())
                bad.error = "'" + key + "' takes a boolean, a number or a string";
            if (!bad.error.empty() || !WindowDisplay::ApplyWord(change, key, word, bad.error))
                return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
        }
        const WindowDisplay::Result r = WindowDisplay::Set(change);
        return StateNodeToLua(s, r.Ok() ? WindowDisplay::Report(r.settings) : WindowDisplay::ErrorNode(r));
    });
    // The active CRT profile's parameters: window_crt_params() reads {profile, kind, parameters = {{name, value,
    // initial, minimum, maximum, step, description}, ...}}; window_crt_params_set({CRTgamma = 2.4}, {steps =
    // {CRTgamma = -1}, reset = true | "name"}) sets values, moves by steps, resets. Headless: {ok = false, ...}
    lua.set_function("window_crt_params", [](sol::this_state s) -> sol::object {
        const WindowDisplay::ParametersResult r = WindowDisplay::GetCrtParameters();
        return StateNodeToLua(s, r.Ok() ? WindowDisplay::CrtParametersReport(r.parameters) : WindowDisplay::ErrorNode(r));
    });
    lua.set_function("window_crt_params_set", [](sol::this_state s, sol::optional<sol::table> values,
                                                 sol::optional<sol::table> options) -> sol::object {
        CrtParameterChange change;
        WindowDisplay::ParametersResult bad;
        bad.status = WindowDisplay::Status::BadParameter;
        auto numbers = [&](const sol::table& table, std::vector<std::pair<std::string, double>>& out) {
            for (const auto& [k, v] : table)
            {
                if (!k.is<std::string>() || v.get_type() != sol::type::number)
                {
                    bad.error = "A parameter takes a number: {name = value}";
                    return false;
                }
                out.emplace_back(k.as<std::string>(), v.as<double>());
            }
            return true;
        };
        if (values && !numbers(*values, change.values))
            return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
        if (options)
        {
            const sol::object steps = (*options)["steps"];
            if (steps.get_type() == sol::type::table && !numbers(steps.as<sol::table>(), change.steps))
                return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
            const sol::object reset = (*options)["reset"];
            if (reset.get_type() == sol::type::boolean)
                change.resetAll = reset.as<bool>();
            else if (reset.get_type() == sol::type::string)
                change.reset.push_back(reset.as<std::string>());
        }
        const WindowDisplay::ParametersResult r = WindowDisplay::SetCrtParameters(change);
        return StateNodeToLua(s, r.Ok() ? WindowDisplay::CrtParametersReport(r.parameters) : WindowDisplay::ErrorNode(r));
    });
    // The GUI window recording (the picture as drawn, CRT and all, the on-screen keyboard, the machine's sound):
    // window_record(path, {keyboard = "default", codec = "h264", hardware = true, fit = true, audio = true, scale = 2, picture_height = 1080,
    // width = 3840, height = 2160, layout = "side", transfer = "nv12"}) starts, window_record_frames(n) runs a paused machine n frames
    // with every one recorded, window_record_stop() finishes the file, window_record_status() reads. The
    // WindowRecording report every interface returns; refused: {ok = false, error, kind}
    lua.set_function("window_record", [this](sol::this_state s, const std::string& path,
                                             sol::optional<sol::table> options) -> sol::object {
        WindowRecording::Result bad;
        bad.status = WindowRecording::Status::BadParameter;
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            bad.error = "No emulator selected";
            return StateNodeToLua(s, WindowRecording::ErrorNode(bad));
        }
        WindowRecordingRequest request;
        request.path = path;
        if (options)
        {
            for (const auto& [k, v] : *options)
            {
                const std::string key = k.is<std::string>() ? k.as<std::string>() : std::string("?");
                std::string word;
                if (v.get_type() == sol::type::boolean)
                    word = v.as<bool>() ? "on" : "off";
                else if (v.get_type() == sol::type::number)
                    word = WindowDisplay::NumberWord(v.as<double>());
                else if (v.get_type() == sol::type::string)
                    word = v.as<std::string>();
                else
                    bad.error = "'" + key + "' takes a boolean, a number or a string";
                if (!bad.error.empty() || !WindowRecording::ApplyWord(request, key, word, bad.error))
                    return StateNodeToLua(s, WindowRecording::ErrorNode(bad));
            }
        }
        const WindowRecording::Result r = WindowRecording::Start(emulator->GetId(), request);
        return StateNodeToLua(s, r.Ok() ? WindowRecording::Report(r.recording) : WindowRecording::ErrorNode(r));
    });
}
