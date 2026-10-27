    // The operation log - GET /api/v1/emulator/{id}/operations
    {
        Json::Value& e = schemas["OperationLogEntry"];
        e["type"] = "object";
        auto field = [&e](const char* name, const char* type, const char* description) {
            e["properties"][name]["type"] = type;
            e["properties"][name]["description"] = description;
        };
        field("seq", "integer", "1, 2, ... in log order; never reused, also after the ring wraps");
        field("frame", "integer", "Machine frame when the operation finished");
        field("t_in_frame", "integer", "T-state in that frame");
        field("wall_ms", "integer", "Host wall clock, ms since the Unix epoch");
        field("kind", "string",
              "reset, autostart, snapshot-load, media-insert, media-eject, media-discard, disk-create, network-change, "
              "ide-drive-kind, rzx-restore, model-switch (refused only: a switch replaces the instance), timeline-cut "
              "(Rec From Here, with what was decided about a medium that could not go back), rom-load, gs-card-switch, "
              "speed-change, state-transfer, slot-change (refused only), cd-front-panel (refused only), tool-edit (a "
              "memory, register, port or sector edit by a tool; summary: which), ttd-start, ttd-stop, ttd-discard, "
              "timeline-seek (a seek, a step, a reverse run), ttd-load");
        field("source", "string", "Who asked: qt, webapi, cli, lua, python, gdb, dezog, mcp, machine, core, test, unknown");
        field("mode", "string", "Machine mode the rules saw: live, recording, black-box, replaying, detached");
        field("status", "string", "done, refused (nothing happened), failed (the owner could not do it)");
        field("effect", "string",
              "TTD session effect applied: none, end-keep-history, media-changed, stop-then-media-changed, "
              "machine-changed, timing-changed");
        field("summary", "string", "What: soft / hard, the snapshot path, the slot and the medium");
        field("detail", "string", "refused / failed: the reason; done: \"no change\" when there was nothing to do");
        field("repeats", "integer",
              "How many times in a row: the same tool edit again (a script's pokes) counts in the newest entry, which "
              "takes the new seq");

        Json::Value& s = schemas["OperationLog"];
        s["type"] = "object";
        s["properties"]["mode"]["type"] = "string";
        s["properties"]["mode"]["description"] = "What the machine is doing now (live, recording, black-box, replaying, detached)";
        s["properties"]["last_seq"]["type"] = "integer";
        s["properties"]["last_seq"]["description"] = "The newest entry's seq (0: nothing logged); pass it as since next time";
        s["properties"]["capacity"]["type"] = "integer";
        s["properties"]["capacity"]["description"] = "Entries kept (the oldest go first)";
        s["properties"]["entries"]["type"] = "array";
        s["properties"]["entries"]["items"]["$ref"] = "#/components/schemas/OperationLogEntry";
        s["properties"]["entries"]["description"] = "Oldest first";
    }
