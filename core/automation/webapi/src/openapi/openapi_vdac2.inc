    // TS-Conf VDAC2 card (FT812): bus capture to an .evr replay stream
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
    }
