    // GUI window display settings: how the unreal-qt window draws the picture - GPU acceleration, CRT effects and
    // profile, temporal blending (gigascreen smoothing) - the View menu's settings. The window belongs to the front
    // end, which registers it with the core; headless there is no window and every call answers 409 no-window.
    // Shared by CLI `video window`, Lua / Python window_display() / window_display_set() and the MCP capture_media
    // actions window_display / window_display_set.
    {
        const char* kPath = "/api/v1/emulator/{id}/video/window";
        auto idParam = [](Json::Value& op) {
            Json::Value p;
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            op["parameters"].append(p);
        };
        auto settingsRef = [](Json::Value& op, const char* description) {
            op["responses"]["200"]["description"] = description;
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/WindowDisplay";
            op["responses"]["404"]["description"] = "Emulator not found";
            op["responses"]["409"]["description"] =
                "{error, kind}: kind no-window (headless, or the GUI did not answer) or unavailable (gpu on a host "
                "without OpenGL)";
        };

        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "GUI window display settings";
        get["tags"].append("Screen State");
        get["description"] =
            "How the GUI window draws the picture: backend (gpu / software), gpu_available, gpu, crt, crt_profile and "
            "the crt_profiles offered, blending (temporal blending, gigascreen smoothing), blending_frames, "
            "blending_weights, and the emulator the window shows. The settings are the window's, whichever instance "
            "it shows. Screenshot source=window shows their effect.";
        idParam(get);
        settingsRef(get, "Display settings");

        auto describeSet = [&](Json::Value& op) {
            op["summary"] = "Change the GUI window display settings";
            op["tags"].append("Screen State");
            op["description"] =
                "Any of the fields, the rest stay; applied the way the View menu does (menu checks follow). gpu "
                "re-creates the screen and carries the other settings over; a crt_profile other than none switches "
                "CRT on, none switches it off; shader:<name> is a libretro preset (shader:crt-royale, "
                "shader:guest-advanced, ...) or a custom shader, GPU only. The window's display, not the machine: no "
                "control session is opened or needed. An empty body only reads. Returns the settings after the change.";
            idParam(op);
            Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
            body["type"] = "object";
            auto field = [&body](const char* name, const char* type, const char* description) {
                body["properties"][name]["type"] = type;
                body["properties"][name]["description"] = description;
            };
            field("gpu", "boolean", "GPU acceleration (OpenGL) on / off");
            field("crt", "boolean", "CRT effects on / off");
            field("crt_profile", "string",
                  "A CRT profile from crt_profiles (the window offers them: none, basic, ..., shader:<name> for a "
                  "libretro preset or a custom shader with the GPU)");
            field("blending", "boolean", "Temporal blending (gigascreen smoothing) on / off");
            field("blending_frames", "integer", "Frames blended, 2..5");
            field("blending_weights", "string", "equal or exponential (newer frames stronger)");
            body["properties"]["crt_profile"]["example"] = "basic";
            settingsRef(op, "Display settings after the change");
            op["responses"]["400"]["description"] = "{error, kind: bad-parameter}: an unknown key or value";
        };
        describeSet(paths[kPath]["put"]);
        describeSet(paths[kPath]["post"]);
    }
