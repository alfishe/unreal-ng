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
    }
