    // Temporal effects (ZX DLSS de-flicker) status - GET / PUT /api/v1/emulator/{id}/video/temporal
    {
        Json::Value& s = schemas["TemporalStatus"];
        s["type"] = "object";
        auto field = [&s](const char* name, const char* type, const char* description) {
            s["properties"][name]["type"] = type;
            s["properties"][name]["description"] = description;
        };
        field("algorithm", "string", "Selected algorithm, \"\" when off");
        field("active", "boolean", "The algorithm processes frames now");
        field("inactive_reason", "string", "Why a selected algorithm is not active (e.g. a mode without a ZX raster)");
        field("video_delay_frames", "integer", "Frames the picture is shown after emulation (the A/V delay, more while active)");
        field("video_delay_ms", "number", "The same delay in milliseconds");
        field("audio_extra_delay_frames", "integer", "Frames added to the sound so it stays in sync with the delayed picture");
        field("processed", "integer", "Frames the algorithm processed");
        field("written", "integer", "Outputs shown in place of their frame");
        field("late", "integer", "Outputs ready after their frame had already been shown");
        field("restarts", "integer", "Algorithm restarts (seek, size change, overflow)");
        field("last_ms", "number", "Processing time of the last frame, ms");
        field("average_ms", "number", "Running average processing time per frame, ms");
        s["properties"]["algorithms"]["type"] = "array";
        s["properties"]["algorithms"]["items"]["type"] = "string";
        s["properties"]["algorithms"]["description"] = "Algorithms that can be switched on";
        field("default_algorithm", "string", "Recommended algorithm (mod-tpgwafsd)");
    }
