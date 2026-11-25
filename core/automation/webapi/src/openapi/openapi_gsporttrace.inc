    // General Sound port trace: the GS-coprocessor triage tool (activity counters
    // that always run, plus an event trace session). Same data as CLI 'gsporttrace'
    // and the MCP / Lua / Python bindings. Handlers: api/state_audio_api.cpp
    {
        auto idParameter = [](Json::Value& operation) {
            operation["parameters"][0]["name"] = "id";
            operation["parameters"][0]["in"] = "path";
            operation["parameters"][0]["required"] = true;
            operation["parameters"][0]["schema"]["type"] = "string";
        };

        Json::Value& get = paths["/api/v1/emulator/{id}/state/audio/gs/porttrace"]["get"];
        get["summary"] = "General Sound activity counters and port trace";
        get["description"] =
            "Always-on counters (cpu_steps, interrupts_accepted / periods / coalesced, nmis_accepted, dac_fetches, "
            "volume_latch_writes, host_commands_received / dropped, host_data_written / dropped / read, "
            "last_dac_fetch_gs_cycle, last_dac_fetch_frame), the trace session (capturing, armed, event_count, "
            "total_produced, total_evicted) and the GS CPU (pc, halted). With events=N also the last N buffered "
            "events: timestamp, frame, side (host, gs, dac, interrupt), direction, port, value, pc, channel, nmi";
        get["tags"].append("Audio State");
        idParameter(get);
        get["parameters"][1]["name"] = "events";
        get["parameters"][1]["in"] = "query";
        get["parameters"][1]["required"] = false;
        get["parameters"][1]["schema"]["type"] = "integer";
        get["parameters"][1]["description"] = "Also return the last N buffered events";
        get["responses"]["200"]["description"] = "Counters, trace status, CPU state (and events)";
        get["responses"]["404"]["description"] = "Emulator not found, or no General Sound card fitted";

        Json::Value& post = paths["/api/v1/emulator/{id}/control/audio/gs/porttrace"]["post"];
        post["summary"] = "Control the General Sound port trace";
        post["description"] = "start (clears the buffer), stop and pause (keep the events), resume, clear";
        post["tags"].append("Audio State");
        idParameter(post);
        Json::Value& body = post["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["required"].append("action");
        body["properties"]["action"]["type"] = "string";
        for (const char* action : {"start", "stop", "pause", "resume", "clear"})
            body["properties"]["action"]["enum"].append(action);
        post["responses"]["200"]["description"] = "The trace session status after the action";
        post["responses"]["400"]["description"] = "Missing or unknown action";
        post["responses"]["404"]["description"] = "Emulator not found, or no General Sound card fitted";

        // The GS slot's statistics block (gsslotstats.h, gs-debugger N7); CLI 'gs stats', MCP inspect_state
        // aspect audio_gs_stats, Lua / Python gs_stats
        const auto statsQuery = [](Json::Value& operation) {
            Json::Value& since = operation["parameters"][operation["parameters"].size()];
            since["name"] = "since";
            since["in"] = "query";
            since["required"] = false;
            since["schema"]["type"] = "integer";
            since["description"] = "A baseline id from POST control/audio/gs reset_counters: counters since then";
            Json::Value& fresh = operation["parameters"][operation["parameters"].size()];
            fresh["name"] = "fresh";
            fresh["in"] = "query";
            fresh["required"] = false;
            fresh["schema"]["type"] = "integer";
            fresh["description"] = "1: a block from a frame end after this read (a running machine)";
        };
        Json::Value& stats = paths["/api/v1/emulator/{id}/state/audio/gs/stats"]["get"];
        stats["summary"] = "General Sound slot statistics";
        stats["description"] =
            "What the fitted card did, one block for every card (each fills the sections it has): card (resets), "
            "mailbox (both sides: host commands / data / status polls, card reads / acks, overwrites), interrupts, "
            "cpu, audio; on NeoGS also dma (zx / sd / mp3 transactions), spi, sd (commands by index, errors), mp3, "
            "flash. rates: per second over the last 50 frames; errors: the error counters and their total. "
            "capabilities lists the sections. A running machine publishes the block at frame ends while it is read "
            "(for 2 s after a read): the answer comes from the next frame end, age_frames says how old it is; a "
            "paused machine is read now. replaying: collected while TTD replays";
        stats["tags"].append("Audio State");
        idParameter(stats);
        statsQuery(stats);
        stats["responses"]["200"]["description"] = "The statistics block";
        stats["responses"]["404"]["description"] = "Emulator not found, no General Sound card, or no such baseline";
        stats["responses"]["503"]["description"] = "No frame ended within 500 ms; read again";

        Json::Value& section = paths["/api/v1/emulator/{id}/state/audio/gs/stats/{section}"]["get"];
        section["summary"] = "One section of the General Sound slot statistics";
        section["description"] = "card, mailbox, interrupts, cpu, audio, dma, spi, sd, mp3, flash, rates or errors "
                                 "(as the card has them: 404 names the card's sections)";
        section["tags"].append("Audio State");
        idParameter(section);
        section["parameters"][1]["name"] = "section";
        section["parameters"][1]["in"] = "path";
        section["parameters"][1]["required"] = true;
        section["parameters"][1]["schema"]["type"] = "string";
        statsQuery(section);
        section["responses"]["200"]["description"] = "The section with implementation, frame, age_frames, since";
        section["responses"]["404"]["description"] = "No such section on this card, no card, or no such baseline";
        section["responses"]["503"]["description"] = "No frame ended within 500 ms; read again";

        // The firmware objects through the card's profile (gsfirmwareobjects.h, gs-debugger §8A.9); CLI 'gs firmware',
        // MCP inspect_state aspect audio_gs_firmware, Lua / Python gs_firmware
        Json::Value& objects = paths["/api/v1/emulator/{id}/state/audio/gs/firmware"]["get"];
        objects["summary"] = "General Sound firmware objects";
        objects["description"] =
            "The card's firmware objects as its profile declares them: instances (a struct at a symbol or address, "
            "bound to a physical page, mapped or not) as rows of fields, variables (memory, a register valid while "
            "the card PC is in a range, an instruction's operand), and the built-in tables' fixed-window variables. "
            "text: the same as lines";
        objects["tags"].append("Audio State");
        idParameter(objects);
        for (const char* name : {"profile", "object"})
        {
            Json::Value& param = objects["parameters"][objects["parameters"].size()];
            param["name"] = name;
            param["in"] = "query";
            param["required"] = false;
            param["schema"]["type"] = "string";
            param["description"] = std::string(name) == "profile" ? "A profile id (default: the card's firmware)"
                                                                  : "One instance or variable by name; 'dispatch', "
                                                                    "'unknown_commands', or 'commands' (the profile's "
                                                                    "command set with typed parameters and replies, "
                                                                    "only when asked)";
        }
        objects["responses"]["200"]["description"] = "{profile, title, source, instances, variables, window_variables, text}";
        objects["responses"]["404"]["description"] = "Emulator not found, no card, no profile, or no such object";

        Json::Value& profiles = paths["/api/v1/emulator/{id}/state/audio/gs/firmware/profiles"]["get"];
        profiles["summary"] = "General Sound firmware profiles";
        profiles["description"] = "The profiles known: the built-in v1.04 / v1.05a / v1.05b and the JSON files of "
                                  "<symbols>/gs/profiles, with their matches (ROM and upload digests)";
        profiles["tags"].append("Audio State");
        idParameter(profiles);
        profiles["responses"]["200"]["description"] = "{profiles: [{id, title, source, matches, commands, ...}], text}";
        profiles["responses"]["404"]["description"] = "Emulator not found";

        // The NeoGS ZX-DMA trace (gszxdmatrace.h, gs-debugger N6); CLI 'gs zxdma', MCP inspect_state aspect
        // audio_gs_zxdma, Lua / Python gs_zxdma
        Json::Value& zxdma = paths["/api/v1/emulator/{id}/state/audio/gs/zxdma"]["get"];
        zxdma["summary"] = "NeoGS ZX-DMA trace";
        zxdma["description"] =
            "The bytes the host read from / wrote to the NeoGS card RAM through the ZX-DMA window, one row a byte, "
            "oldest first: seq (the port trace history's number), frame and host_t (the host frame and T-state at the "
            "access end, after the wait), card_time (card CPU cycles), direction (read / write), card_address, page "
            "(address >> 14), value, wait (/WAIT T-states, capped at 255), host_pc, dropped (the pending byte an access "
            "of the other direction dropped). Recorded while the GS port trace captures (POST "
            "control/audio/gs/porttrace {action: start}); the DAC fetches share its ring (evicted). Also capturing, "
            "armed, total_bytes_kept, last_seq (pass it back as since), restarted (since was past last_seq: a new "
            "session), counters (the module's bytes_read / written / dropped, wait_t_states), text";
        zxdma["tags"].append("Audio State");
        idParameter(zxdma);
        for (const char* name : {"last", "since"})
        {
            Json::Value& param = zxdma["parameters"][zxdma["parameters"].size()];
            param["name"] = name;
            param["in"] = "query";
            param["required"] = false;
            param["schema"]["type"] = "integer";
            param["description"] = std::string(name) == "last" ? "The newest N bytes (default 256, 0: every one kept)"
                                                               : "Only the rows after this seq (a poller's cursor)";
        }
        zxdma["responses"]["200"]["description"] = "{available, card, capturing, total_bytes_kept, last_seq, rows, text}";
        zxdma["responses"]["400"]["description"] = "last or since is not a number";
        zxdma["responses"]["404"]["description"] = "Emulator not found, or no NeoGS fitted (the classic GS has no ZX-DMA)";

        // The card CPU's profiler (gsprofilereport.h, gs-debugger X9 / X2 / X20); CLI 'gs profile' / 'gs events', MCP
        // inspect_state aspects audio_gs_profile / audio_gs_events, Lua / Python gs_profile / gs_events
        Json::Value& profile = paths["/api/v1/emulator/{id}/state/audio/gs/profile"]["get"];
        profile["summary"] = "General Sound card CPU profiler";
        profile["description"] =
            "The card CPU's profiler (classic GS, the ZX-MultiSound's GS, NeoGS; off by default, POST "
            "control/audio/gs {action: profile_start}). Card CPU cycles throughout. isr.last_frame / isr.since_start: "
            "count, min, avg, max of the ISR length (INT acknowledge start to the boundary after its return, nested "
            "interrupts included), used_pct / headroom_pct of period_cycles, overruns (ended after the period's end), "
            "latency_avg / latency_max (period start to acknowledge), nmis, lost (requests merged). commands: per host "
            "command code count / avg / min / max / total of the card's busy cycles (from its read of the command or a "
            "data byte to its next status poll, interrupts apart, summed until the next command); current_command. "
            "routines: the top N by inclusive cycles (address, page_kind, page, label from the card CPU's symbols, "
            "calls, interrupts, inclusive, exclusive, avg, min, max, inclusive_pct); an interrupt handler is its own "
            "root, interrupts never count in a routine's time. activity: per host frame cycles, isr_pct, idle_pct "
            "(the idle poll loop slept through), halt_pct, busy_pct, commands, interrupts, overruns, lost, isr_max. text";
        profile["tags"].append("Audio State");
        idParameter(profile);
        for (const char* name : {"cpu", "routines", "activity"})
        {
            Json::Value& param = profile["parameters"][profile["parameters"].size()];
            param["name"] = name;
            param["in"] = "query";
            param["required"] = false;
            param["schema"]["type"] = std::string(name) == "cpu" ? "string" : "integer";
            param["description"] = std::string(name) == "cpu"        ? "gs (default): the GS slot's card; ms: the ZX-MultiSound's"
                                   : std::string(name) == "routines" ? "The top N routines (default 32, 0: all)"
                                                                     : "The newest N frames of activity (default 256, 0: none)";
        }
        profile["responses"]["200"]["description"] =
            "{available, card, cpu, on, frames, cycles, clock_hz, period_cycles, isr, commands, current_command, "
            "routines, routines_total, activity, stack_overflows, routines_dropped, resyncs, text}";
        profile["responses"]["400"]["description"] = "routines or activity is not a number";
        profile["responses"]["404"]["description"] = "Emulator not found, no card, or a card without a CPU (the lightweight player)";

        Json::Value& events = paths["/api/v1/emulator/{id}/state/audio/gs/events"]["get"];
        events["summary"] = "General Sound frame event map";
        events["description"] =
            "The last complete host frame, one row per card interrupt period (period, start in cycles from the "
            "frame's start): isr bars [{accept, end, length, entry, nmi}] in cycles from the period's start (end null: "
            "still running at the frame's end) and events [[cycle, kind, value, detail]] (columns), kinds "
            "host_data_write, host_data_read, host_command, host_status, host_control, dac (detail: channel), page "
            "(detail: port), volume (detail: channel), zxdma_read, zxdma_write. The ISR bars need the profiler on, the "
            "other events the GS port trace capturing. Also frame, card_start, cycles, period_cycles, periods, first, "
            "count, totals, truncated, text";
        events["tags"].append("Audio State");
        idParameter(events);
        for (const char* name : {"cpu", "first", "count", "rows"})
        {
            Json::Value& param = events["parameters"][events["parameters"].size()];
            param["name"] = name;
            param["in"] = "query";
            param["required"] = false;
            param["schema"]["type"] = std::string(name) == "cpu" ? "string" : "integer";
            param["description"] = std::string(name) == "cpu"     ? "gs (default) or ms"
                                   : std::string(name) == "first" ? "The first period row (default 0)"
                                   : std::string(name) == "count" ? "Rows from first (default 0: to the frame's end)"
                                                                  : "Rows in the text (default 16)";
        }
        events["responses"]["200"]["description"] = "{available, frame, period_cycles, periods, rows, totals, text}";
        events["responses"]["400"]["description"] = "first, count or rows is not a number";
        events["responses"]["404"]["description"] = "Emulator not found, no card, or a card without a CPU";
    }
