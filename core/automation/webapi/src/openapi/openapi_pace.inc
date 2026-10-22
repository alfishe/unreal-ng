    // =====================================================================
    // The paced run (api/runpace_api.cpp; core execution::RunPacer, Stream Deck use case D1)
    // =====================================================================
    {
        const char* const path = "/api/v1/emulator/{id}/run_pace";
        for (const char* method : {"get", "post"})
        {
            paths[path][method]["tags"].append("Debug Commands");
            paths[path][method]["parameters"][0]["name"] = "id";
            paths[path][method]["parameters"][0]["in"] = "path";
            paths[path][method]["parameters"][0]["required"] = true;
            paths[path][method]["parameters"][0]["schema"]["type"] = "string";
            paths[path][method]["responses"]["404"]["description"] = "Emulator not found";
        }
        paths[path]["get"]["summary"] = "The paced run: preset, interval, running, the ladder";
        paths[path]["get"]["description"] =
            "The pace of the instance: the machine steps a frame, N scanlines or an instruction per interval on the "
            "pacer's own thread and stops at a breakpoint. Presets coarse to fine: full (no pacing), 25fps, 5fps, 1fps "
            "(a frame per 40 / 200 / 1000 ms), frame, 100lines, 10lines, 1line, 1op (one unit per interval_ms, "
            "1..5000). A read of the pacer's state: never waits for the machine. The run.state topic of /api/v1/ws "
            "carries the same as `pace`.";
        Json::Value& pace = paths[path]["get"]["responses"]["200"]["content"]["application/json"]["schema"];
        paths[path]["get"]["responses"]["200"]["description"] = "The pace";
        pace["type"] = "object";
        pace["properties"]["emulator_id"]["type"] = "string";
        Json::Value& body = pace["properties"]["pace"];
        body["type"] = "object";
        for (const char* name : {"preset", "label", "unit", "stop_reason"})
            body["properties"][name]["type"] = "string";
        body["properties"]["unit"]["enum"] = Json::Value(Json::arrayValue);
        for (const char* unit : {"full", "frames", "scanlines", "instructions"})
            body["properties"]["unit"]["enum"].append(unit);
        body["properties"]["stop_reason"]["description"] =
            "\"\" while running or never run; stopped, breakpoint, resumed, held, revoked, gone, full";
        for (const char* name : {"index", "count", "interval_ms", "effective_ms", "ticks"})
            body["properties"][name]["type"] = "integer";
        body["properties"]["effective_ms"]["description"] = "The interval in force (0: full speed)";
        for (const char* name : {"adjustable", "running"})
            body["properties"][name]["type"] = "boolean";
        body["properties"]["breakpoint"]["type"] = "object";
        body["properties"]["breakpoint"]["description"] = "{id, address} when a breakpoint ended the run";
        body["properties"]["presets"]["type"] = "array";
        body["properties"]["presets"]["items"]["type"] = "object";
        body["properties"]["presets"]["description"] = "The ladder: {id, label, index, unit, count, interval_ms (null: adjustable / none), adjustable}";

        paths[path]["post"]["summary"] = "Choose, start, stop or step the paced run";
        paths[path]["post"]["description"] =
            "op: set (preset and / or interval_ms; off full speed on a running machine it pauses it and starts the "
            "pace, onto full speed it stops the pace and resumes the machine), next / prev (a finer / coarser "
            "preset), by (delta presets), start, stop (the machine stays paused), toggle (the dial's push: a running "
            "pace stops, a machine at full speed pauses, else the pace starts), step (one unit now). Scope run; "
            "409 Run-control held while another surface holds the claim. Runs on the long-call pool.";
        Json::Value& request = paths[path]["post"]["requestBody"]["content"]["application/json"]["schema"];
        request["type"] = "object";
        request["required"].append("op");
        request["properties"]["op"]["type"] = "string";
        for (const char* op : {"set", "next", "prev", "by", "start", "stop", "toggle", "step"})
            request["properties"]["op"]["enum"].append(op);
        request["properties"]["preset"]["description"] = "set: an id (5fps), a label (5 fps) or an index (0..8)";
        request["properties"]["preset"]["oneOf"][0]["type"] = "string";
        request["properties"]["preset"]["oneOf"][1]["type"] = "integer";
        request["properties"]["interval_ms"]["type"] = "integer";
        request["properties"]["interval_ms"]["minimum"] = 1;
        request["properties"]["interval_ms"]["maximum"] = 5000;
        request["properties"]["delta"]["type"] = "integer";
        request["properties"]["delta"]["description"] = "by: presets to move, + finer";
        paths[path]["post"]["responses"]["200"]["description"] =
            "{status, message, pace, emulator_id, state}; a step adds stop {reason, breakpoint_id?, address?}, pc, sp";
        paths[path]["post"]["responses"]["400"]["description"] = "Unknown op, preset or an interval out of 1..5000";
        paths[path]["post"]["responses"]["409"]["description"] = "Run-control held by another surface; revoked_by_user";
    }
