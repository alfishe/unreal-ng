    // Port trace (PDR) endpoints — runtime feature "porttrace"
    // Design: docs/inprogress/2026-08-24-diagnostic-observability/
    {
        auto addIdParam = [&paths](const std::string& path, const char* verb) {
            paths[path][verb]["parameters"][0]["name"] = "id";
            paths[path][verb]["parameters"][0]["in"] = "path";
            paths[path][verb]["parameters"][0]["required"] = true;
            paths[path][verb]["parameters"][0]["schema"]["type"] = "string";
        };

        auto addSimplePost = [&paths, &addIdParam](const std::string& path, const std::string& summary,
                                                   const std::string& description) {
            paths[path]["post"]["summary"] = summary;
            paths[path]["post"]["tags"].append("Port Trace");
            paths[path]["post"]["description"] = description;
            addIdParam(path, "post");
            paths[path]["post"]["responses"]["200"]["description"] = "Session status";
            paths[path]["post"]["responses"]["409"]["description"] = "porttrace feature disabled";
        };

        addSimplePost("/api/v1/emulator/{id}/profiler/porttrace/start", "Start port trace capture",
                      "Start capturing structured port I/O events (clears the ring buffer). "
                      "Requires the 'porttrace' runtime feature.");
        addSimplePost("/api/v1/emulator/{id}/profiler/porttrace/stop", "Stop port trace capture",
                      "Stop capturing; buffered events are preserved.");
        addSimplePost("/api/v1/emulator/{id}/profiler/porttrace/pause", "Pause port trace capture",
                      "Suspend capturing; buffered events are preserved.");
        addSimplePost("/api/v1/emulator/{id}/profiler/porttrace/resume", "Resume port trace capture",
                      "Resume a paused capture session.");
        addSimplePost("/api/v1/emulator/{id}/profiler/porttrace/clear", "Clear port trace buffer",
                      "Purge the event ring buffer.");

        const std::string statusPath = "/api/v1/emulator/{id}/profiler/porttrace/status";
        paths[statusPath]["get"]["summary"] = "Port trace status";
        paths[statusPath]["get"]["tags"].append("Port Trace");
        paths[statusPath]["get"]["description"] =
            "Session state, buffer counters, active filter, and per-frame activity summary.";
        addIdParam(statusPath, "get");
        paths[statusPath]["get"]["responses"]["200"]["description"] = "Session status";

        const std::string eventsPath = "/api/v1/emulator/{id}/profiler/porttrace/events";
        paths[eventsPath]["get"]["summary"] = "Retrieve port trace events";
        paths[eventsPath]["get"]["tags"].append("Port Trace");
        paths[eventsPath]["get"]["description"] =
            "Buffered events, each with raw/decoded port, decode rule, value, PC, device attribution and "
            "disposition flags. 'limit' defaults to 0 = unlimited; 'since' filters by absolute T-state.";
        addIdParam(eventsPath, "get");
        paths[eventsPath]["get"]["parameters"][1]["name"] = "limit";
        paths[eventsPath]["get"]["parameters"][1]["in"] = "query";
        paths[eventsPath]["get"]["parameters"][1]["required"] = false;
        paths[eventsPath]["get"]["parameters"][1]["schema"]["type"] = "integer";
        paths[eventsPath]["get"]["parameters"][2]["name"] = "since";
        paths[eventsPath]["get"]["parameters"][2]["in"] = "query";
        paths[eventsPath]["get"]["parameters"][2]["required"] = false;
        paths[eventsPath]["get"]["parameters"][2]["schema"]["type"] = "integer";
        paths[eventsPath]["get"]["responses"]["200"]["description"] = "Session status + event list";

        const std::string filterPath = "/api/v1/emulator/{id}/profiler/porttrace/filter";
        paths[filterPath]["get"]["summary"] = "Get port trace filter";
        paths[filterPath]["get"]["tags"].append("Port Trace");
        addIdParam(filterPath, "get");
        paths[filterPath]["get"]["responses"]["200"]["description"] = "Human-readable filter description";
        paths[filterPath]["post"]["summary"] = "Set port trace filter";
        paths[filterPath]["post"]["tags"].append("Port Trace");
        paths[filterPath]["post"]["description"] =
            "Replace the filter wholesale. Body: {\"include\": [rule...], \"exclude\": [rule...]} where each "
            "rule is a compound AND of conditions ({\"port\": \"0xFFFD\", \"direction\": \"out\", ...}); "
            "or {\"preset\": \"ay-only\"}. Include rules OR together; exclude always wins.";
        addIdParam(filterPath, "post");
        paths[filterPath]["post"]["responses"]["200"]["description"] = "Effective filter description";

        const std::string configPath = "/api/v1/emulator/{id}/profiler/porttrace/config";
        paths[configPath]["post"]["summary"] = "Configure port trace buffer";
        paths[configPath]["post"]["tags"].append("Port Trace");
        paths[configPath]["post"]["description"] =
            "Set ring buffer capacity and/or overflow mode ('ring' = evict oldest, 'stop' = stop-when-full). "
            "Only while the session is stopped.";
        addIdParam(configPath, "post");
        paths[configPath]["post"]["responses"]["200"]["description"] = "Effective configuration";
        paths[configPath]["post"]["responses"]["409"]["description"] = "Session not stopped";

        const std::string savePath = "/api/v1/emulator/{id}/profiler/porttrace/save";
        paths[savePath]["post"]["summary"] = "Save port trace to file";
        paths[savePath]["post"]["tags"].append("Port Trace");
        paths[savePath]["post"]["description"] =
            "Save the buffered events server-side. Body: {\"path\": \"/tmp/trace.json\", \"format\": "
            "\"json\"|\"csv\"|\"bin\"}. Exports embed the model's decode-rule table so traces are "
            "self-describing (tools/porttrace/porttrace_convert.py reads all three formats).";
        addIdParam(savePath, "post");
        paths[savePath]["post"]["responses"]["200"]["description"] = "Saved event count and path";
    }
