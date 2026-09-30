    // Temporal effects (ZX DLSS de-flicker; docs/inprogress/2026-09-27-zxdlss-gigascreen/temporal-effects-manager.md):
    // an algorithm run on every emulated frame before presentation. It needs a few frames of look-ahead, so
    // while it is active the picture is shown that many frames later and the sound is delayed to match.
    // Shared by CLI `video temporal`, Lua / Python video_temporal() / video_temporal_set() and the MCP
    // capture_media actions temporal_status / temporal_set.
    {
        const char* kPath = "/api/v1/emulator/{id}/video/temporal";
        auto idParam = [](Json::Value& op) {
            Json::Value p;
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            op["parameters"].append(p);
        };
        auto statusRef = [](Json::Value& op, const char* description) {
            op["responses"]["200"]["description"] = description;
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/TemporalStatus";
            op["responses"]["404"]["description"] = "Emulator not found";
        };

        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "ZX DLSS de-flicker status";
        get["tags"].append("Screen State");
        get["description"] =
            "The selected algorithm (\"\" = off), whether it runs now (active, inactive_reason when not), the "
            "delay it causes (video_delay_frames / video_delay_ms of presentation delay, audio_extra_delay_frames "
            "added to the sound so it stays in sync), frame counters (processed, written, late, restarts), "
            "processing time (last_ms, average_ms), the algorithms that can be switched on and the default one.";
        idParam(get);
        statusRef(get, "Temporal effect status");

        auto describeSet = [&](Json::Value& op) {
            op["summary"] = "Switch the ZX DLSS de-flicker";
            op["tags"].append("Screen State");
            op["description"] =
                "Body {\"algorithm\": \"<name>\"} switches the algorithm on (features zxdlss and screenhq are "
                "switched on with it); \"\" or \"off\" switches it off and restores those features. mod-tpgwafsd "
                "(the default) delays video by 7 frames at the default A/V delay of 2 and the audio by 5 more. "
                "Returns the new status.";
            idParam(op);
            Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
            body["type"] = "object";
            body["required"].append("algorithm");
            body["properties"]["algorithm"]["type"] = "string";
            body["properties"]["algorithm"]["description"] = "Algorithm name (see algorithms in the status), \"\" or \"off\"";
            body["properties"]["algorithm"]["example"] = "mod-tpgwafsd";
            statusRef(op, "New temporal effect status");
            op["responses"]["400"]["description"] =
                "Unknown algorithm or bad body: {error, message, algorithms[]} lists the valid names";
        };
        describeSet(paths[kPath]["put"]);
        describeSet(paths[kPath]["post"]);
    }
