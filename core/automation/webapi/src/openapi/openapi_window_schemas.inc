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
        field("blending_decay", "number", "Exponential weights: each older frame's share of the newer one, 0.1..0.9");
        field("fullscreen", "boolean", "The window fills the display");
        field("scale", "integer", "The whole zoom the window is fitted to, 1..4; 0 when resized by hand, maximized or full screen");
        field("toolbar", "boolean", "View > Toolbar");
        field("status_bar", "boolean", "View > Status Bar");
        field("hud", "boolean", "The HUD overlay shown");
        field("hud_theme", "string", "The HUD's theme");
        s["properties"]["hud_themes"]["type"] = "array";
        s["properties"]["hud_themes"]["items"]["type"] = "string";
        s["properties"]["hud_themes"]["description"] = "Themes the HUD offers";
        field("hud_notices", "object", "{id: shown} of the HUD's notice categories (ram-page, tape-events, breakpoints, ...)");
        s["properties"]["hud_notices"]["additionalProperties"]["type"] = "boolean";
        field("osd_keyboard", "boolean", "The on-screen keyboard window shown");
        field("osd_keyboard_picture", "string", "The keyboard picture it shows (\"\" none for this machine)");
        s["properties"]["osd_keyboard_pictures"]["type"] = "array";
        s["properties"]["osd_keyboard_pictures"]["items"]["type"] = "string";
        s["properties"]["osd_keyboard_pictures"]["description"] = "The pictures the shown machine offers";
        field("osd_keyboard_style", "string", "How the pictures are drawn: photo, schematic, hybrid (with the hints)");
        s["properties"]["osd_keyboard_styles"]["type"] = "array";
        s["properties"]["osd_keyboard_styles"]["items"]["type"] = "string";
        s["properties"]["osd_keyboard_styles"]["description"] = "Every style the pictures come in";
        field("next_layers", "boolean", "The ZX Next layers window shown");
        field("next_layers_mode", "string", "layers, composed or winner");
        field("next_layers_shown", "object", "{ula, layer2, tilemap, sprites}: the layer ticks");
        s["properties"]["next_layers_shown"]["additionalProperties"]["type"] = "boolean";
    }
    // The active CRT profile's parameters - GET / PUT /api/v1/emulator/{id}/video/window/crt_params
    {
        Json::Value& s = schemas["CrtParameters"];
        s["type"] = "object";
        s["properties"]["profile"]["type"] = "string";
        s["properties"]["profile"]["description"] = "The active profile (as crt_profile)";
        s["properties"]["kind"]["type"] = "string";
        s["properties"]["kind"]["description"] = "preset (a libretro preset's own), profile (the built-in ones), none";
        Json::Value& item = s["properties"]["parameters"]["items"];
        s["properties"]["parameters"]["type"] = "array";
        item["type"] = "object";
        item["properties"]["name"]["type"] = "string";
        item["properties"]["description"]["type"] = "string";
        for (const char* number : {"value", "initial", "minimum", "maximum", "step"})
            item["properties"][number]["type"] = "number";
    }
    // The overscan frame's viewport - GET / PUT /api/v1/emulator/{id}/video/viewport
    {
        Json::Value& s = schemas["VideoViewport"];
        s["type"] = "object";
        s["properties"]["overscan"]["type"] = "boolean";
        s["properties"]["overscan_available"]["type"] = "boolean";
        s["properties"]["overscan_available"]["description"] = "The machine has the frame (a Pentagon)";
        s["properties"]["viewport"]["type"] = "string";
        s["properties"]["viewport"]["description"] = "full, symmetric, standard, screen_only (custom: a crop no preset makes)";
        s["properties"]["viewports"]["type"] = "array";
        s["properties"]["viewports"]["items"]["type"] = "string";
        s["properties"]["framebuffer"]["type"] = "string";
        s["properties"]["display"]["type"] = "string";
        s["properties"]["video_mode"]["type"] = "string";
        s["properties"]["changed"]["type"] = "boolean";
    }
