    // Execution arbiter (docs/inprogress/2026-10-09-execution-arbiter) - DeviceState::Execution: the same report CLI
    // `state execution`, Lua / Python execution_state() and the MCP aspect `execution` use
    {
        const char* kReport = "/api/v1/emulator/{id}/state/execution";
        paths[kReport]["get"]["summary"] = "Get execution arbiter state";
        paths[kReport]["get"]["tags"].append("Execution");
        paths[kReport]["get"]["description"] =
            "Who decides whether the Z80 runs, now. phase (not_started / running / stopping / stopped); intent "
            "(run / pause) with intent_set_by (origin: gui, webapi, cli, mcp, lua, python, gdb, dezog, debugger, ttd, "
            "rzx, lifecycle, internal; client), intent_reason, intent_seq and intent_age_ms; paused (the intent while "
            "running); main_loop (attached, parked); may_run; parked (nothing executes: the loop parked, a command "
            "holds the CPU, or no loop); serving (the command holding the CPU: seq, tag, by, age_ms, drives_z80 - "
            "a direct run; null when none); queue (requests waiting behind it: intents and commands in order); claim "
            "(the run-control claim's holder or null: another surface's run is refused while it is held); counters "
            "(applied, coalesced, refused, busy, aborted, edges = Paused / Resumed broadcasts); driver_violations "
            "(moments two threads executed the Z80 at once: 0 on a correct run). Peeked: reading changes nothing.";
        paths[kReport]["get"]["parameters"][0]["name"] = "id";
        paths[kReport]["get"]["parameters"][0]["in"] = "path";
        paths[kReport]["get"]["parameters"][0]["required"] = true;
        paths[kReport]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kReport]["get"]["responses"]["200"]["description"] = "Execution arbiter state";
        paths[kReport]["get"]["responses"]["404"]["description"] = "Emulator not found";

        const char* kActive = "/api/v1/emulator/state/execution";
        paths[kActive]["get"]["summary"] = "Get execution arbiter state (active emulator)";
        paths[kActive]["get"]["tags"].append("Execution");
        paths[kActive]["get"]["description"] =
            "Same as /api/v1/emulator/{id}/state/execution for the only or selected emulator.";
        paths[kActive]["get"]["responses"]["200"]["description"] = "Execution arbiter state";
        paths[kActive]["get"]["responses"]["400"]["description"] = "Several emulators running: use the {id} form";
        paths[kActive]["get"]["responses"]["404"]["description"] = "No emulator";
    }
