    // TS-Conf VDAC2 card (FT812): bus capture to an .evr replay stream, line budget metrics
    // Design: docs/inprogress/2026-10-01-tsconf-vdac2/ (vdac2-test-corpus.md §4)
    {
        auto addIdParam = [&paths](const std::string& path, const char* verb) {
            paths[path][verb]["parameters"][0]["name"] = "id";
            paths[path][verb]["parameters"][0]["in"] = "path";
            paths[path][verb]["parameters"][0]["required"] = true;
            paths[path][verb]["parameters"][0]["schema"]["type"] = "string";
        };
        const char* statusText =
            "Capture status: capturing, path, bytes, selects, exchanges, frames, start_clock, last_clock "
            "(FT812 system clocks), format \"evr\"";

        const std::string startPath = "/api/v1/emulator/{id}/vdac2/capture/start";
        paths[startPath]["post"]["summary"] = "Start the VDAC2 bus capture";
        paths[startPath]["post"]["tags"].append("VDAC2");
        paths[startPath]["post"]["description"] =
            "Write everything on the VDAC2 card's FT812 bus to an .evr replay stream on the server: every chip "
            "select change and every byte with the chip's answer, stamped with FT812 system clocks, plus a "
            "record at each FT812 frame end with a hash of the picture. Started on a running chip, the stream "
            "begins with the chip's whole state, so it replays from that point. Replaces a capture in progress. "
            "Body: {\"path\": \"/tmp/game.evr\"}. Format: vdac2-test-corpus.md §4.";
        addIdParam(startPath, "post");
        paths[startPath]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        paths[startPath]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["type"] =
            "string";
        paths[startPath]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"].append("path");
        paths[startPath]["post"]["responses"]["200"]["description"] = statusText;
        paths[startPath]["post"]["responses"]["400"]["description"] = "No path";
        paths[startPath]["post"]["responses"]["409"]["description"] =
            "No VDAC2 card (not TS-Conf, not the VDAC2 build, or a build without VDAC2 support)";
        paths[startPath]["post"]["responses"]["500"]["description"] = "The file cannot be written";

        const std::string stopPath = "/api/v1/emulator/{id}/vdac2/capture/stop";
        paths[stopPath]["post"]["summary"] = "Stop the VDAC2 bus capture";
        paths[stopPath]["post"]["tags"].append("VDAC2");
        paths[stopPath]["post"]["description"] = "Finish the running capture: the end record is written and the file closed.";
        addIdParam(stopPath, "post");
        paths[stopPath]["post"]["responses"]["200"]["description"] = statusText;
        paths[stopPath]["post"]["responses"]["409"]["description"] = "No VDAC2 card, or no capture running";

        const std::string statusPath = "/api/v1/emulator/{id}/vdac2/capture/status";
        paths[statusPath]["get"]["summary"] = "VDAC2 bus capture status";
        paths[statusPath]["get"]["tags"].append("VDAC2");
        paths[statusPath]["get"]["description"] = "The running capture, or the last one when none runs.";
        addIdParam(statusPath, "get");
        paths[statusPath]["get"]["responses"]["200"]["description"] = statusText;
        paths[statusPath]["get"]["responses"]["409"]["description"] = "No VDAC2 card";

        // FT812 line budget metrics (line-budget-metrics.md, line-budget-model.md)
        const char* metricsText =
            "The line budget of the last finished FT812 frame: valid (every visible line drawn and measured), frame "
            "(REG_FRAMES), lines, hard_budget (clocks per line: HCYCLE x PCLK), soft_budget (hard minus margin), "
            "worst_line, worst_clocks, total_clocks, lines_over_soft, lines_over_hard (broken on a real card), "
            "margin, measure_always; with lines=1 line_clocks (cost per line, saturating at 65535); with "
            "in_flight=1 in_flight {known, reason, lines_passed, line_clocks (-1 = passed without drawing)}";
        const std::string metricsPath = "/api/v1/emulator/{id}/vdac2/metrics";
        Json::Value& get = paths[metricsPath]["get"];
        get["summary"] = "VDAC2 FT812 line budget metrics";
        get["tags"].append("VDAC2");
        get["description"] =
            "What each FT812 screen line cost against the clocks it has (display list commands, one clock each on "
            "every line, plus pixel filling by format and filter). The block is replaced at each FT812 frame end and "
            "is part of the chip state, so after a TTD seek it describes that moment. A frame that was not drawn "
            "(the monitor showed the Evo, or turbo skipped it) is not valid unless measure_always is on. The frame "
            "in flight (in_flight=1) is read only on a paused machine: reading it brings the chip up to the "
            "machine's position.";
        addIdParam(metricsPath, "get");
        get["parameters"][1]["name"] = "lines";
        get["parameters"][1]["in"] = "query";
        get["parameters"][1]["schema"]["type"] = "boolean";
        get["parameters"][1]["description"] = "Include the cost of every line";
        get["parameters"][2]["name"] = "in_flight";
        get["parameters"][2]["in"] = "query";
        get["parameters"][2]["schema"]["type"] = "boolean";
        get["parameters"][2]["description"] = "Include the frame in flight (paused machine)";
        get["responses"]["200"]["description"] = metricsText;
        get["responses"]["409"]["description"] = "No VDAC2 card";

        Json::Value& put = paths[metricsPath]["put"];
        put["summary"] = "Set the VDAC2 line budget margin / measure-always";
        put["tags"].append("VDAC2");
        put["description"] =
            "Body: {\"margin\": 0..50, \"measure_always\": bool}, each optional. margin: the soft budget, percent "
            "below the line period (default from [VDAC2] LineBudgetMargin, 10); used from the next finished frame. "
            "measure_always: draw and measure every frame, also while not shown (costs the drawing time; nothing "
            "the guest sees changes). Answers with the metrics (without lines).";
        addIdParam(metricsPath, "put");
        Json::Value& body = put["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["margin"]["type"] = "integer";
        body["properties"]["margin"]["minimum"] = 0;
        body["properties"]["margin"]["maximum"] = 50;
        body["properties"]["measure_always"]["type"] = "boolean";
        put["responses"]["200"]["description"] = metricsText;
        put["responses"]["400"]["description"] = "No JSON body, margin out of 0..50, or measure_always not a boolean";
        put["responses"]["409"]["description"] = "No VDAC2 card";
    }
