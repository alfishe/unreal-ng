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
            "blending_weights, blending_decay, the emulator the window shows, fullscreen, scale, toolbar, status_bar, "
            "the HUD (hud, hud_theme, hud_themes, hud_notices), the on-screen keyboard (osd_keyboard, "
            "osd_keyboard_picture, osd_keyboard_pictures, osd_keyboard_style, osd_keyboard_styles) and the ZX Next layers window (next_layers, "
            "next_layers_mode, next_layers_shown). The settings are the window's, whichever instance it shows, and "
            "persist across sessions. Screenshot source=window shows their effect.";
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
            field("blending_decay", "number", "Exponential weights: each older frame's share of the newer one, 0.1..0.9");
            field("fullscreen", "boolean", "Full screen on / off (follows after the window system's transition)");
            field("scale", "integer", "Fit the window to the picture at 1..4x (leaves full screen)");
            field("toolbar", "boolean", "View > Toolbar");
            field("status_bar", "boolean", "View > Status Bar");
            field("hud", "boolean", "The HUD overlay");
            field("hud_theme", "string", "One of hud_themes: dark_glass, retro_zx, cyberpunk, amber_crt, emerald_crt, light_modern");
            field("hud_notices", "object", "{id: bool} for the HUD's notice categories (ids of hud_notices); also hud_notice.<id>: bool");
            body["properties"]["hud_notices"]["additionalProperties"]["type"] = "boolean";
            field("osd_keyboard", "boolean", "The on-screen keyboard window");
            field("osd_keyboard_picture", "string", "One of osd_keyboard_pictures (the shown machine's keyboard pictures)");
            field("osd_keyboard_style", "string", "One of osd_keyboard_styles: photo, schematic, hybrid");
            field("next_layers", "boolean", "The ZX Next layers window");
            field("next_layers_mode", "string", "layers, composed or winner");
            field("next_layers_shown", "object", "{ula|layer2|tilemap|sprites: bool}; also next_layer.<name>: bool");
            body["properties"]["next_layers_shown"]["additionalProperties"]["type"] = "boolean";
            body["properties"]["crt_profile"]["example"] = "basic";
            settingsRef(op, "Display settings after the change");
            op["responses"]["400"]["description"] = "{error, kind: bad-parameter}: an unknown key or value";
        };
        describeSet(paths[kPath]["put"]);
        describeSet(paths[kPath]["post"]);
    }
    // The active CRT profile's parameters: a libretro preset's own (its preset.json), basic's and a custom shader's
    // built-in ones; the beam models have none. CLI `video window crt_params`, Lua / Python window_crt_params(),
    // MCP capture_media window_crt_params / window_crt_params_set, the catalogue's video.crt_param_by.
    {
        const char* kPath = "/api/v1/emulator/{id}/video/window/crt_params";
        auto idParam = [](Json::Value& op) {
            Json::Value p;
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            op["parameters"].append(p);
        };
        auto paramsRef = [](Json::Value& op, const char* description) {
            op["responses"]["200"]["description"] = description;
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/CrtParameters";
            op["responses"]["404"]["description"] = "Emulator not found";
            op["responses"]["409"]["description"] =
                "{error, kind}: no-window (headless) or unavailable (a profile without parameters)";
        };
        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "The active CRT profile's parameters";
        get["tags"].append("Screen State");
        get["description"] = "profile, kind (preset | profile | none) and each parameter with its value, initial value, "
                             "range and the step a dial moves it by.";
        idParam(get);
        paramsRef(get, "CRT parameters");

        auto describeSet = [&](Json::Value& op) {
            op["summary"] = "Change the active CRT profile's parameters";
            op["tags"].append("Screen State");
            op["description"] = "reset first (true: all, a name or a list), then values (clamped to the range), then "
                                "steps (a dial's, the parameter's own step). A preset's values are kept per preset.";
            idParam(op);
            Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
            body["type"] = "object";
            body["properties"]["values"]["type"] = "object";
            body["properties"]["values"]["additionalProperties"]["type"] = "number";
            body["properties"]["values"]["description"] = "{name: value}";
            body["properties"]["steps"]["type"] = "object";
            body["properties"]["steps"]["additionalProperties"]["type"] = "number";
            body["properties"]["steps"]["description"] = "{name: steps}";
            body["properties"]["reset"]["description"] = "true (all), a name, or a list of names";
            paramsRef(op, "CRT parameters after the change");
            op["responses"]["400"]["description"] = "{error, kind: bad-parameter}: an unknown parameter or field";
        };
        describeSet(paths[kPath]["put"]);
        describeSet(paths[kPath]["post"]);

        Json::Value& reset = paths["/api/v1/emulator/{id}/video/window/crt_params/reset"]["post"];
        reset["summary"] = "Reset the active CRT profile's parameters";
        reset["tags"].append("Screen State");
        reset["description"] = "Every parameter back to its initial value.";
        idParam(reset);
        paramsRef(reset, "CRT parameters after the reset");
    }
    // The overscan frame's viewport (the machine's: answers headless too). CLI `video viewport`, Lua / Python
    // video_viewport(), MCP capture_media viewport / viewport_set, the catalogue's video.viewport.
    {
        const char* kPath = "/api/v1/emulator/{id}/video/viewport";
        auto idParam = [](Json::Value& op) {
            Json::Value p;
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            op["parameters"].append(p);
        };
        auto viewportRef = [](Json::Value& op, const char* description) {
            op["responses"]["200"]["description"] = description;
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/VideoViewport";
            op["responses"]["404"]["description"] = "Emulator not found";
        };
        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "Overscan and its viewport";
        get["tags"].append("Screen State");
        get["description"] = "Whether the Pentagon's 384x304 overscan frame is on, the viewport that crops it, the frame "
                             "and display size.";
        idParam(get);
        viewportRef(get, "Viewport");

        auto describeSet = [&](Json::Value& op) {
            op["summary"] = "Crop the overscan frame";
            op["tags"].append("Screen State");
            op["description"] = "full (384x304), symmetric (352x304), standard (352x288), screen_only (256x192), next or "
                                "prev. A crop other than full needs overscan on (PUT /state/screen/overscan or the feature "
                                "overscan). The picture, not the machine: no control session.";
            idParam(op);
            Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
            body["type"] = "object";
            body["required"].append("viewport");
            body["properties"]["viewport"]["type"] = "string";
            body["properties"]["viewport"]["example"] = "standard";
            viewportRef(op, "Viewport after the change, with changed");
            op["responses"]["400"]["description"] = "{error, kind: bad-parameter}: an unknown viewport";
            op["responses"]["409"]["description"] = "{error, kind: unavailable}: no overscan on";
        };
        describeSet(paths[kPath]["put"]);
        describeSet(paths[kPath]["post"]);
    }
    // GUI window recording: the picture as the window draws it (CRT shader and all), optionally with the on-screen
    // keyboard, to a full-colour file through ffmpeg; a paused machine stepped with record/frames records every frame.
    // Shared by the MCP capture_media actions window_record_start / _frames / _stop / _status.
    {
        auto idParam = [](Json::Value& op) {
            Json::Value p;
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            op["parameters"].append(p);
        };
        auto statusRef = [](Json::Value& op, const char* description) {
            op["tags"].append("Capture");
            op["responses"]["200"]["description"] = description;
            Json::Value& schema = op["responses"]["200"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["recording"]["type"] = "boolean";
            schema["properties"]["path"]["type"] = "string";
            schema["properties"]["keyboard"]["type"] = "string";
            schema["properties"]["keyboard"]["description"] = "The keyboard picture in the file; empty = the picture only";
            schema["properties"]["frames"]["type"] = "integer";
            schema["properties"]["width"]["type"] = "integer";
            schema["properties"]["height"]["type"] = "integer";
            schema["properties"]["fps"]["type"] = "number";
            schema["properties"]["audio"]["type"] = "boolean";
            schema["properties"]["sample_rate"]["type"] = "integer";
            schema["properties"]["audio_samples"]["type"] = "integer";
            schema["properties"]["audio_samples"]["description"] = "Sample pairs in the sound track so far";
            op["responses"]["404"]["description"] = "Emulator not found";
            op["responses"]["409"]["description"] =
                "{error, kind}: no-window (headless) or unavailable (not recording, no ffmpeg, another instance shown)";
        };

        Json::Value& start = paths["/api/v1/emulator/{id}/video/window/record"]["post"];
        start["summary"] = "Record the GUI window (CRT effects, on-screen keyboard)";
        start["description"] =
            "Starts writing the picture as the window draws it (no HUD), with the on-screen keyboard lighting the keys "
            "the machine holds and the machine's sound: one video frame per emulated frame at the machine's rate, "
            "each with its own samples (a frame the window did not draw repeats the picture). Pause the machine and step it with record/frames "
            "for every frame whatever the host's load. Needs ffmpeg on the host.";
        idParam(start);
        Json::Value& body = start["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["required"].append("path");
        auto field = [&body](const char* name, const char* type, const char* description) {
            body["properties"][name]["type"] = type;
            body["properties"][name]["description"] = description;
        };
        field("path", "string", "The file to write, server side (.mov for prores, .mp4 for h264)");
        field("keyboard", "string",
              "default (the keyboard the OSD shows), a picture id of the machine (pentagon2plus, ...) or of another "
              "with the same matrix (zxnext on a Pentagon), or true; left out or false = the picture only");
        field("codec", "string", "prores (ProRes 4444, the default) or h264 (H.264 4:4:4)");
        field("audio", "boolean",
              "The machine's sound, frame for frame with the picture (default true; PCM in .mov, AAC in .mp4)");
        field("fps", "integer", "Left out or 0 = the machine's frame rate (needed with audio); 1..100 with audio false");
        field("scale", "integer", "1..4: the picture doubled in blocks, default 1");
        field("picture_height", "integer", "0..4320: the CRT drawn at that height (GPU only), 0 = the window's");
        field("width", "integer", "The file's frame width (with height); left out = picture + keyboard");
        field("height", "integer", "The file's frame height (with width)");
        field("layout", "string", "below (default) or side: where the keyboard goes in a given frame");
        statusRef(start, "Recording status");
        start["responses"]["400"]["description"] = "{error, kind: bad-parameter}: a value out of range";

        Json::Value& status = paths["/api/v1/emulator/{id}/video/window/record"]["get"];
        status["summary"] = "GUI window recording status";
        idParam(status);
        statusRef(status, "Recording status");

        Json::Value& frames = paths["/api/v1/emulator/{id}/video/window/record/frames"]["post"];
        frames["summary"] = "Run frames into the GUI window recording";
        frames["description"] =
            "A paused machine runs {frames} frames (1..100000), each one drawn by the window and written before the "
            "next runs. Run control held by another surface: 409 run-control-held.";
        idParam(frames);
        Json::Value& framesBody = frames["requestBody"]["content"]["application/json"]["schema"];
        framesBody["type"] = "object";
        framesBody["required"].append("frames");
        framesBody["properties"]["frames"]["type"] = "integer";
        statusRef(frames, "Recording status after the frames, plus ran");
        frames["responses"]["400"]["description"] = "{error, kind: bad-parameter}: frames out of range";

        Json::Value& stop = paths["/api/v1/emulator/{id}/video/window/record/stop"]["post"];
        stop["summary"] = "Stop the GUI window recording";
        stop["description"] = "Closes the file: waits for ffmpeg, then muxes the sound in (the picture copied).";
        idParam(stop);
        statusRef(stop, "The finished recording");
    }
