    // The operation log of the machine state coordinator (docs/inprogress/2026-10-09-machine-state-coordinator/
    // architecture.md §7): every operation on the machine as a whole - reset, disk autostart, snapshot load, media
    // insert / eject / discard, blank disk - with its frame, its result (done, refused with the reason, failed) and
    // what it did to the TTD session. Always on, the last 1024. Shared by CLI `ops`, Lua / Python operations() and
    // the MCP emulator_manage action operations.
    {
        const char* kPath = "/api/v1/emulator/{id}/operations";
        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "Operation log: who did what to the machine";
        get["tags"].append("Emulator Control");
        get["description"] =
            "The last operations on the machine as a whole (reset, autostart, snapshot load, media insert / eject / "
            "discard, disk create), oldest first: frame and T-state, wall time, the surface that asked, the machine "
            "mode the rules saw (live, recording, black-box, replaying, detached), the result (done, refused with "
            "the reason every surface shows, failed with the error) and the TTD session effect applied. Refusals are "
            "logged too. Poll with since=<last_seq> to get only what is new.";
        Json::Value id;
        id["name"] = "id";
        id["in"] = "path";
        id["required"] = true;
        id["schema"]["type"] = "string";
        get["parameters"].append(id);
        Json::Value since;
        since["name"] = "since";
        since["in"] = "query";
        since["required"] = false;
        since["schema"]["type"] = "integer";
        since["schema"]["minimum"] = 0;
        since["description"] = "Only entries with a larger seq (the last_seq of a previous answer)";
        get["parameters"].append(since);
        Json::Value limit;
        limit["name"] = "limit";
        limit["in"] = "query";
        limit["required"] = false;
        limit["schema"]["type"] = "integer";
        limit["schema"]["minimum"] = 0;
        limit["description"] = "Only the newest N entries (0 or absent: all kept)";
        get["parameters"].append(limit);
        get["responses"]["200"]["description"] = "The operation log";
        get["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/OperationLog";
        get["responses"]["400"]["description"] = "since or limit is not a non-negative integer";
        get["responses"]["404"]["description"] = "Emulator not found";
    }
