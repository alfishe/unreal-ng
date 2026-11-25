    // Device histories (core/src/debugger/history/devicehistory.h, historycontrol.h): the history of each device's own
    // state machine, by registry id (GET /slots): off (zero cost), ring (standby), capture (any limit), and a stream to
    // a JSON Lines file
    {
        auto pathParameter = [](Json::Value& operation, int index, const char* name, const char* description) {
            operation["parameters"][index]["name"] = name;
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] = description;
        };
        const char* idText = "Emulator UUID, symbolic id or index";
        const char* deviceText = "The registry id of the device: a built-in (beta128) or a card's slot (zxbus.1)";

        Json::Value& list = paths["/api/v1/emulator/{id}/history"]["get"];
        list["summary"] = "Every device history of the machine";
        list["description"] = "device, what it records, mode (off / ring / capture), limit, written, kept, dropped, "
                              "and the stream (path, limit, written, error) when one runs";
        list["tags"].append("History");
        pathParameter(list, 0, "id", idText);
        list["responses"]["200"]["description"] = "{histories: [...]}";

        Json::Value& read = paths["/api/v1/emulator/{id}/history/{device}"]["get"];
        read["summary"] = "One device's history: its stats and records, oldest first";
        read["tags"].append("History");
        pathParameter(read, 0, "id", idText);
        pathParameter(read, 1, "device", deviceText);
        read["parameters"][2]["name"] = "last";
        read["parameters"][2]["in"] = "query";
        read["parameters"][2]["schema"]["type"] = "integer";
        read["parameters"][2]["description"] = "The newest N records (default 0: all kept); with since: the first N "
                                               "after the cursor";
        read["parameters"][3]["name"] = "since";
        read["parameters"][3]["in"] = "query";
        read["parameters"][3]["schema"]["type"] = "integer";
        read["parameters"][3]["description"] =
            "A cursor: the records numbered after it, oldest first, each with its seq (1 = the first record written "
            "since the last clear). The reply adds next_since (the cursor of the next read), missed (records after "
            "the cursor no longer kept: evicted from the ring unread) and restarted (the cursor was past the newest "
            "record, the history was cleared or configured: read from the oldest kept). A poller pages forward with "
            "no gap";
        read["responses"]["200"]["description"] =
            "the stats and records[]; with since also next_since, missed, restarted and each record's seq";
        read["responses"]["400"]["description"] = "since is not a record number";
        read["responses"]["404"]["description"] = "no such emulator, or the device has no history";

        Json::Value& config = paths["/api/v1/emulator/{id}/history/{device}/config"]["post"];
        config["summary"] = "Configure a device's history";
        config["description"] = "Fields not given keep their value. A config starts afresh: the records, the counts "
                                "and the stream file (opened again, truncated)";
        config["tags"].append("History");
        pathParameter(config, 0, "id", idText);
        pathParameter(config, 1, "device", deviceText);
        Json::Value& schema = config["requestBody"]["content"]["application/json"]["schema"];
        schema["type"] = "object";
        schema["properties"]["mode"]["type"] = "string";
        schema["properties"]["mode"]["enum"].append("off");
        schema["properties"]["mode"]["enum"].append("ring");
        schema["properties"]["mode"]["enum"].append("capture");
        schema["properties"]["limit"]["type"] = "integer";
        schema["properties"]["limit"]["description"] = "ring: its size (0: the device's default); capture: the cap (0: none)";
        schema["properties"]["stream"]["type"] = "string";
        schema["properties"]["stream"]["description"] = "A file every record is written to as well (JSON Lines); \"\": none";
        schema["properties"]["stream_limit"]["type"] = "integer";
        schema["properties"]["stream_limit"]["description"] = "Records to the file at most (0: no limit)";
        config["responses"]["200"]["description"] = "the stats after the change";
        config["responses"]["400"]["description"] = "a bad mode, or a stream file that cannot be opened";
        config["responses"]["404"]["description"] = "no such emulator, or the device has no history";

        Json::Value& clear = paths["/api/v1/emulator/{id}/history/{device}/clear"]["post"];
        clear["summary"] = "Drop a device history's records and counts (the configuration stays)";
        clear["tags"].append("History");
        pathParameter(clear, 0, "id", idText);
        pathParameter(clear, 1, "device", deviceText);
        clear["responses"]["200"]["description"] = "the stats after the clear";
        clear["responses"]["404"]["description"] = "no such emulator, or the device has no history";
        Json::Value& pause = paths["/api/v1/emulator/{id}/history/{device}/pause"]["post"];
        pause["summary"] = "Hold a device history: nothing recorded, the records kept";
        pause["tags"].append("History");
        pathParameter(clear, 0, "id", idText);
        pathParameter(clear, 1, "device", deviceText);
        pause["responses"]["200"]["description"] = "the stats after the pause";
        pause["responses"]["404"]["description"] = "no such emulator, or the device has no history";
        Json::Value& resume = paths["/api/v1/emulator/{id}/history/{device}/resume"]["post"];
        resume["summary"] = "Go on recording into a held device history";
        resume["tags"].append("History");
        pathParameter(clear, 0, "id", idText);
        pathParameter(clear, 1, "device", deviceText);
        resume["responses"]["200"]["description"] = "the stats after the resume";
        resume["responses"]["404"]["description"] = "no such emulator, or the device has no history";
    }
