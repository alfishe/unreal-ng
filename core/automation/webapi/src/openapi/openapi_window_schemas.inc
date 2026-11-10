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
        field("smooth_motion", "string",
              "The shown instance's [VIDEO] SmoothMotion: off, blend, interpolate or auto (the picture between two "
              "frames on a fixed display; auto: View > Video Synchronization, present.sync says the step in effect)");
        field("effects_device", "string", "The shown instance's [VIDEO] EffectsDevice: gpu or cpu (the same bits)");
        field("present_policy", "string", "The shown instance's [VIDEO] PresentPolicy: auto, vrr or vsync");
        field("display_rate", "string", "The shown instance's [VIDEO] DisplayRate: keep or match");
        field("lip_sync_offset_ms", "integer", "The shown instance's [VIDEO] LipSyncOffsetMs, -200..200");
        {
            Json::Value& p = s["properties"]["present"];
            p["type"] = "object";
            p["description"] =
                "How the frames reach the screen: the presenter's on-screen timing over its last 600 presents (native "
                "presenters only) and smooth motion's counters since the screen started";
            auto pf = [&p](const char* name, const char* type, const char* description) {
                p["properties"][name]["type"] = type;
                p["properties"][name]["description"] = description;
            };
            pf("presenter", "string",
               "metal, dxgi, gl-timed (Linux: a thread swaps GL; present at the frame's time), gl (the window's own "
               "swap), software");
            pf("fixed_display", "boolean", "A fixed refresh rate (false: VRR / ProMotion range)");
            pf("vrr", "boolean",
               "Native presenters: each frame presented at its own time (true) or on the refresh grid (false), as the "
               "policy and the display decide");
            pf("vrr_support", "object",
               "{state (yes, no, unknown), reason}: can the display under the window follow the frames (X11: RandR "
               "vrr_capable and a fullscreen window; Wayland and Windows: unknown, no query)");
            pf("sync", "object",
               "View > Video Synchronization (smooth_motion auto): {mode (the step in effect: off (not asked), vrr, "
               "display_rate, interpolate), reason (why the steps before it are not; \"\" for vrr and off)}");
            pf("refresh_hz", "object", "{min, max}: the display's refresh range (min 0: a variable range whose low end the system does not report, Windows)");
            pf("clock", "string",
               "The clock the frame on screen was timed by: audio (when its sound is heard), emulated (no audio device "
               "delivering), latch (turbo, a run not paced to real time), none (untimed)");
            pf("timing", "object",
               "How the frames are timed: {delay_clamped (the frame timed last: its sound comes later than the screen "
               "holds its frames, a long audio path or a large lip-sync trim, so it is due max_delay_ms after its "
               "frame's end and the picture leads its sound), max_delay_ms, clamped_frames, early (refreshes that "
               "found no frame due yet: start up), carried (steps of the clock carried and paid back slowly: a device "
               "came or went, an underrun), cuts (breaks of the frame sequence: a pause, a seek, turbo), carry_ms (the "
               "carry being paid back now)}");
            for (const char* count : {"presented", "shown", "dropped", "late"})
                pf(count, "integer", "Presents since the start: handed over, on screen, never on screen, a refresh late");
            pf("lead_ms", "number", "The adaptive lead: how long before its time a frame is drawn and handed over");
            pf("error_ms", "object", "{p50, p99, max}: screen time minus the time asked for");
            pf("beat", "object",
               "Native presenters, since the start: {frames (pictures judged), off (on screen for a refresh count "
               "outside floor / ceil of their span over the refresh: a refresh missed, a frame held or never shown)}");
            pf("sound_offset_ms", "object",
               "{p50, p99} over the last 600 shown: screen time minus the picture's sound on the clock in use (the "
               "lip-sync trim taken out)");
            pf("gpu_wait_ms", "object", "{p50, max}: the wait for GL's draw before the native pass");
            pf("gpu_ms", "object", "{smooth, smooth_cpu, picture: {p50, max, longest, measured}}: the GPU time of smooth motion's passes, smooth motion's refresh on the CPU's clock (waits included) and the GPU time of the picture (CRT / preset), ms; measured 0 where GL has no timer (macOS)");
            pf("smooth", "object",
               "{mode (in effect: off with temporal blending or without timed frames), device (gpu / cpu), game (a "
               "game profile's cadence applies), frames, pictures, renders, blended, interpolated, motion_fields, "
               "on_device, held_flicker}");
            pf("display", "object",
               "[VIDEO] DisplayRate: {state (keep; matched: the display runs hz for the machine's machine_hz, its own "
               "mode original_hz; unchanged: match asked, nothing switched), reason (why unchanged), hz, original_hz, "
               "machine_hz}");
        }
        field("fullscreen", "boolean", "The window fills the display");
        field("view", "string", "The part of the frame shown: full, screen or content");
        s["properties"]["views"]["type"] = "array";
        s["properties"]["views"]["items"]["type"] = "string";
        s["properties"]["views"]["description"] = "The views the window offers: full, screen, content";
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
