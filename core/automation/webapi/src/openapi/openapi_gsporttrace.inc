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
    }
