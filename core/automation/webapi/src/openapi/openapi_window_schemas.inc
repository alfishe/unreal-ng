    // GUI window display settings - GET / PUT /api/v1/emulator/{id}/video/window
    {
        Json::Value& s = schemas["WindowDisplay"];
        s["type"] = "object";
        auto field = [&s](const char* name, const char* type, const char* description) {
            s["properties"][name]["type"] = type;
            s["properties"][name]["description"] = description;
        };
        field("backend", "string", "How the window draws: gpu or software");
        field("gpu_available", "boolean", "The host can draw with the GPU (OpenGL)");
        field("gpu", "boolean", "GPU acceleration on");
        field("crt", "boolean", "CRT effects on (with profile none they show nothing)");
        field("crt_profile", "string", "The CRT profile: one of crt_profiles (none, basic, ..., shader:<name> for a libretro preset or a custom shader)");
        s["properties"]["crt_profiles"]["type"] = "array";
        s["properties"]["crt_profiles"]["items"]["type"] = "string";
        s["properties"]["crt_profiles"]["description"] = "Profiles the window offers";
        field("blending", "boolean", "Temporal blending (gigascreen smoothing) on");
        field("blending_frames", "integer", "Frames blended, 2..5");
        field("blending_weights", "string", "equal or exponential");
        field("emulator", "string", "The emulator the window shows, \"\" when none");
    }
