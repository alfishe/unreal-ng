    // Capture Commands endpoints
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["summary"] = "OCR text from screen";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["description"] =
        "Extract text from screen using ROM font bitmap matching (OCR). "
        "Returns 24 lines x 32 characters matched against the ZX Spectrum ROM font; in a text video mode (ATM Turbo 2+ ATMTX, ZX-Evo ATMTL) the exact 80x25 text from the character codes. rows / cols give the size.";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["description"] = "Screen OCR result";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["rows"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["cols"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["lines"]["type"] = "array";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["text"]["type"] = "string";

    // Capture screen endpoint
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/capture/screen"]["get"];
        op["summary"] = "Screenshot of the presented frame";
        op["tags"].append("Capture");
        op["description"] =
            "A screenshot of what the emulator shows: the presented (finished, tear-free) frame, as PNG (default) or "
            "GIF. `area=full` (default) is the whole frame, border included (352x288 on a Spectrum, 1024x768 for the "
            "FT812 picture); `area=screen` cuts the working picture the frame's own geometry names (the paper of a "
            "Spectrum, the graphics window of a TS-Conf, the whole picture of an FT812 card). On a hires mode the "
            "working picture can be the whole frame, then both give the same image. The answer always carries the "
            "frame's geometry (`frame`, `screen_window`) and the rectangle that was cut (`crop`), so the caller never "
            "has to guess. The image is returned base64-encoded in `data`, or written to `path` on the server. "
            "An unknown `area`, `mode`, `format` or `source` is 400 with the allowed words; a screen window that does not fit "
            "the frame is 500 with the sizes. The frame lags the machine by the present delay (2 frames by default, "
            "more with ZX DLSS); GIF has 256 colors and loses colors on true-colour pictures. "
            "Raw pixels without encoding: /capture/framebuffer.";

        struct Param
        {
            const char* name;
            const char* in;
            bool required;
            const char* description;
            std::vector<const char*> values;
        };
        const std::vector<Param> params = {
            {"id", "path", true, "Emulator id", {}},
            {"area", "query", false,
             "full (default): the whole frame with its border. screen: the working picture of the frame's geometry",
             {"full", "screen"}},
            {"format", "query", false, "png (default, lossless) or gif (256 colors)", {"png", "gif"}},
            {"source", "query", false,
             "presented (default): the finished, tear-free frame the window shows, a couple of frames behind. live: "
             "the frame as drawn right now, no delay and no ZX DLSS: a running machine's emulation thread copies it at "
             "the end of its next frame (409 no-frame if none arrives within 1 s); a paused one is read directly and "
             "answers where the beam stopped",
             {"presented", "live"}},
            {"mode", "query", false, "Deprecated alias of area (full / screen); a conflicting pair is 400",
             {"full", "screen"}},
            {"path", "query", false, "Save the image to this server-side path instead of returning it (folders are created)",
             {}},
            {"filename", "query", false, "Alias of path", {}},
        };
        for (const Param& param : params)
        {
            Json::Value p;
            p["name"] = param.name;
            p["in"] = param.in;
            p["required"] = param.required;
            p["description"] = param.description;
            p["schema"]["type"] = "string";
            for (const char* value : param.values)
                p["schema"]["enum"].append(value);
            op["parameters"].append(p);
        }

        Json::Value& ok = op["responses"]["200"];
        ok["description"] = "Screenshot taken";
        Json::Value& props = ok["content"]["application/json"]["schema"]["properties"];
        props["status"]["type"] = "string";
        props["format"]["type"] = "string";
        props["format"]["description"] = "png or gif";
        props["area"]["type"] = "string";
        props["area"]["description"] = "The area that was asked for: full or screen";
        props["source"]["type"] = "string";
        props["source"]["description"] = "The frame that was asked for: presented or live";
        props["width"]["type"] = "integer";
        props["width"]["description"] = "Width of the returned image";
        props["height"]["type"] = "integer";
        props["height"]["description"] = "Height of the returned image";
        props["size"]["type"] = "integer";
        props["size"]["description"] = "Encoded size in bytes";
        for (const char* rect : {"crop", "screen_window"})
        {
            props[rect]["type"] = "object";
            props[rect]["description"] = std::string(rect) == "crop"
                                             ? "The returned image's rectangle inside the frame, in frame pixels"
                                             : "The working picture inside the frame, in frame pixels";
            for (const char* field : {"x", "y", "width", "height"})
                props[rect]["properties"][field]["type"] = "integer";
        }
        props["frame"]["type"] = "object";
        props["frame"]["description"] = "The frame the image was cut from";
        props["frame"]["properties"]["width"]["type"] = "integer";
        props["frame"]["properties"]["height"]["type"] = "integer";
        props["frame"]["properties"]["mode"]["type"] = "string";
        props["frame"]["properties"]["mode"]["description"] = "Video mode name, or external for a video card's picture";
        props["frame"]["properties"]["source"]["type"] = "string";
        props["frame"]["properties"]["source"]["enum"].append("native");
        props["frame"]["properties"]["source"]["enum"].append("external");
        props["frame"]["properties"]["source"]["enum"].append("composed");
        props["frame"]["properties"]["source"]["description"] =
            "native: the machine's own frame; external: a video card's picture (FT812); composed: a ZX-Poly group's "
            "display frame (2x, what the window shows)";
        props["frame"]["properties"]["frame_number"]["type"] = "integer";
        props["frame"]["properties"]["frame_number"]["description"] = "Emulated frame the pixels belong to";
        props["frame"]["properties"]["partial"]["type"] = "boolean";
        props["frame"]["properties"]["partial"]["description"] =
            "Only for a live frame of a paused machine: true when the beam stopped inside a frame drawn per T-state, so "
            "the lines it has not reached are still the previous frame's";
        props["frame"]["properties"]["beam"]["type"] = "object";
        props["frame"]["properties"]["beam"]["description"] =
            "Only for a live frame of a paused machine: where the beam stopped";
        props["frame"]["properties"]["beam"]["properties"]["line"]["type"] = "integer";
        props["frame"]["properties"]["beam"]["properties"]["tstate"]["type"] = "integer";
        props["data"]["type"] = "string";
        props["data"]["description"] = "Base64 image (absent when saved to a path)";
        props["saved"]["type"] = "boolean";
        props["file"]["type"] = "string";
        props["file"]["description"] = "The path the image was written to";

        op["responses"]["400"]["description"] =
            "Unknown area, mode or format (the message lists the allowed words), or area and mode disagree; "
            "kind: bad-parameter";
        op["responses"]["404"]["description"] = "No such emulator; kind: not-found";
        op["responses"]["409"]["description"] = "The emulator has not presented a frame yet (try again); kind: no-frame";
        op["responses"]["500"]["description"] =
            "The screen window does not fit the frame (kind: bad-geometry), the encoder failed (encode-failed) or the "
            "file could not be written (io-failed); the message has the details";
        op["responses"]["503"]["description"] = "The emulator is shutting down";
        for (const char* status : {"400", "404", "409", "500", "503"})
        {
            Json::Value& eprops = op["responses"][status]["content"]["application/json"]["schema"]["properties"];
            eprops["error"]["type"] = "string";
            eprops["message"]["type"] = "string";
            eprops["kind"]["type"] = "string";
        }
    }

    // ZX DLSS plane B (binary)
    {
        Json::Value& fb = paths["/api/v1/emulator/{id}/capture/framebuffer"]["get"];
        fb["summary"] = "Capture the picture as raw pixels";
        fb["tags"].append("Capture");
        fb["description"] =
            "The picture without image encoding (core FramebufferExport; CLI `framebuffer save`, Lua / Python "
            "framebuffer(), MCP capture_media framebuffer): format=rgba (default) - the presented frame, width x height "
            "x 4 bytes R, G, B, A, border included (the same pixels /capture/screen returns for area=full); format=index - the pen of every pixel as "
            "little-endian uint16 where the machine names its palette (the Sprinter: 2 048 pens, 736 x 288, the state "
            "now; colors in /state/sprinter/palette), else 409. encoding=binary (default): application/octet-stream "
            "with X-Width, X-Height, X-Format, X-Encoding; encoding=base64: JSON {width, height, format, encoding, "
            "bytes, data}.";
        for (const char* name : {"id", "format", "encoding"})
        {
            Json::Value p;
            p["name"] = name;
            p["in"] = std::string(name) == "id" ? "path" : "query";
            p["required"] = std::string(name) == "id";
            p["schema"]["type"] = "string";
            fb["parameters"].append(p);
        }
        fb["responses"]["200"]["description"] = "Raw pixels";
        fb["responses"]["409"]["description"] = "No frame yet, or no indexed form on this machine";
    }

    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["summary"] = "Capture ZX DLSS plane B (binary)";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["description"] =
        "What the renderer drew for each pixel of the current frame, recorded in the same pass as the "
        "picture (feature 'zxdlss'): width x height little-endian uint16 in framebuffer layout. Bits 0-7: "
        "attribute byte the beam used (0 on the border); 8-11: color index 0-15; 12: ink (1) / paper (0); "
        "13-14: role (0 not drawn, 1 screen, 2 border). Geometry in X-PlaneB-Width / X-PlaneB-Height. "
        "After a TTD seek or step it is the plane of the displayed position, re-rendered with the picture.";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["responses"]["200"]["description"] =
        "Raw plane B (application/octet-stream)";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["responses"]["200"]["content"]["application/octet-stream"]
         ["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["responses"]["200"]["content"]["application/octet-stream"]
         ["schema"]["format"] = "binary";
    paths["/api/v1/emulator/{id}/capture/planeb"]["get"]["responses"]["409"]["description"] =
        "Feature 'zxdlss' is off";

    // Audio capture endpoints (sound core sample capture + DSP analysis)
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["summary"] = "Control audio capture session";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["description"] =
        "Arm/stop/clear a stereo sample capture from the sound core. start arms a target of "
        "`seconds` of emulated audio (0.01-30.0); the emulator must run to collect samples. `source` records "
        "one mixer device instead of the master mix (its own buffer, before mute / solo / volume): beeper, ay1, "
        "ay2, fm1, fm2, covox (also the Sprinter's Covox-Blaster DAC), gs, gs_mp3, moonsound_fm, moonsound_pcm "
        "(GET /audio/mixer lists the fitted ones); 400 for a device the machine does not have.";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("start");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("stop");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("clear");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["seconds"]["type"] = "number";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["source"]["type"] = "string";

    // Per-device mixer (core AudioMixer; DeviceState::AudioMixer: CLI `audio mixer`, Lua / Python audio_mixer() /
    // audio_mixer_set(), MCP aspect audio_mixer)
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["summary"] = "Per-device audio mixer";
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["description"] =
        "master (muted, sample_rate_hz) and devices[] of the machine's mixer: source (the key for PUT and for "
        "/audio/capture), name, muted, solo, audible, volume (0..1), gain_db, peak and active (the last frame), "
        "capturable; capture_source = what an armed capture records; host_output = what the host speakers got: held "
        "(true while a run not paced to real time - run_frames and the other direct runs, TTD seek / replay, "
        "turbo - withholds it), frames_delivered, frames_audible (of those, non-silent), frames_held (emulated "
        "frames since creation).";
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/mixer"]["get"]["responses"]["200"]["description"] = "The mixer";
    for (const char* method : {"put", "post"})
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/audio/mixer/{source}"][method];
        op["summary"] = "Set one mixer device (mute, solo, volume)";
        op["tags"].append("Capture");
        op["description"] =
            "Body {\"muted\": true, \"solo\": false, \"volume\": 0.5} or {\"gain_db\": -6}; every field optional. "
            "source = a key from GET /audio/mixer, or master (muted only). Answers the mixer. A monitor setting of "
            "this instance (not saved).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["parameters"][1]["name"] = "source";
        op["parameters"][1]["in"] = "path";
        op["parameters"][1]["required"] = true;
        op["parameters"][1]["schema"]["type"] = "string";
        op["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        op["responses"]["200"]["description"] = "The mixer after the change";
        op["responses"]["400"]["description"] = "Unknown or absent source, bad value";
    }
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["responses"]["200"]["description"] =
        "Capture session updated (armed/target_samples/sample_rate/channels)";

    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["summary"] = "Audio capture progress";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["description"] =
        "Captured vs target sample counts and completion flag. Poll until complete=true.";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["responses"]["200"]["description"] =
        "Progress (captured_samples/target_samples/complete)";

    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["summary"] = "Audio capture result";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["description"] =
        "DSP analysis of the captured buffer: per-channel RMS/peak, zero-crossing rate, "
        "dominant frequency. wav=true additionally exports a WAV file into the scratch "
        "directory and returns its path.";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["name"] = "wav";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["description"] =
        "Export the captured samples as a WAV file (returns wav_path)";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["schema"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["responses"]["200"]["description"] =
        "Analysis result (sample_rate/channels/dominant_hz/rms/peak per side)";

    // Video recording endpoints (RecordingManager orchestration)
    paths["/api/v1/emulator/{id}/video/record"]["post"]["summary"] = "Control video recording";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["description"] =
        "start/stop/pause/resume a recording of the emulated screen. gif is native "
        "(dependency-free); other codecs map to containers as supported by the build. "
        "start pauses the emulator while applying fps/scale/region configuration, then "
        "resumes it. The recording feature is auto-enabled if disabled. Audio is optional: "
        "\"audio\":\"aac\" adds the emulated sound track (default: none = video only). The codec must "
        "fit the container: mp4 aac/mp3/opus/flac, mov aac/mp3/pcm_s16le, mkv any, webm opus/vorbis, "
        "avi aac/mp3/pcm_s16le; gif has no audio (400). h264/hevc + aac in mp4/mov uses the native "
        "macOS encoder, other combinations need ffmpeg.";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("start");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("stop");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("pause");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("resume");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["fps"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["scale"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["region"]["enum"].append("full");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["region"]["enum"].append("screen");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["region"]["enum"].append("main");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["filename"]["type"] = "string";
    {
        Json::Value& props = paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]
                                  ["application/json"]["schema"]["properties"];
        props["audio"]["type"] = "string";
        props["audio"]["description"] =
            "start: audio codec for the sound track (aac, mp3, opus, vorbis, flac, pcm_s16le; true = aac). "
            "Omitted or \"none\" = video only";
        props["video_bitrate"]["type"] = "integer";
        props["video_bitrate"]["description"] = "start: video bitrate in kbps (0 = encoder default, 100..200000)";
        props["audio_bitrate"]["type"] = "integer";
        props["audio_bitrate"]["description"] = "start: audio bitrate in kbps (0 = encoder default, 32..512)";
    }
    paths["/api/v1/emulator/{id}/video/record"]["post"]["responses"]["200"]["description"] =
        "Recording session updated (start: output, audio/audio_codec/audio_sample_rate/audio_channels; "
        "stop: stats/output file); 400 for an audio codec the container cannot hold (gif + audio); "
        "409 when already recording or nothing to stop";

    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["summary"] = "Video recording status";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["description"] =
        "Recording/paused flags, live RecordingStats (frames/durations/output size, "
        "audio_samples_recorded), the session's video_codec/audio/audio_codec/audio_sample_rate/"
        "audio_channels/audio_duration (emulated seconds of sound), feature state and last error.";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["responses"]["200"]["description"] =
        "Recording status (recording/paused/stats/output)";

