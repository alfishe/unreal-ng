    // The machine area (2026-10-21): recent files, the recording presets and the video record options that came with
    // them, the TTD clip as a video. Included after openapi_capture.inc / openapi_ttd.inc fragments of the same paths
    // are merged by jsoncpp

    // GET / DELETE /api/v1/recent — the files opened lately (RecentFiles)
    {
        for (const char* method : {"get", "delete"})
        {
            Json::Value& op = paths["/api/v1/recent"][method];
            op["tags"].append("Media");
            op["parameters"][0]["name"] = "kind";
            op["parameters"][0]["in"] = "query";
            op["parameters"][0]["required"] = false;
            op["parameters"][0]["schema"]["type"] = "string";
            for (const char* kind : {"all", "snapshot", "tape", "disk", "rzx", "image", "cd"})
                op["parameters"][0]["schema"]["enum"].append(kind);
            op["parameters"][0]["description"] = "One kind (default all)";
            op["responses"]["400"]["description"] = "An unknown kind";
        }
        Json::Value& get = paths["/api/v1/recent"]["get"];
        get["summary"] = "The files opened lately";
        get["description"] =
            "Ten per kind, newest first: snapshot, tape, disk, rzx, image (hard disk / card images), cd. Noted when a "
            "file opened on any surface (a snapshot / RZX / tape / disk load, a media insert or swap of a file). "
            "{revision, kinds: {snapshot: [{index, kind, name, path, time}], ...}, all: [...]}; with kind: {revision, "
            "kind, files}. Open one with POST /api/v1/controllers/actions/media.open_recent {\"params\": {\"index\": 0, "
            "\"kind\": \"tape\"}}. The WebSocket topic media.recent pushes the changes.";
        get["responses"]["200"]["description"] = "The list";
        Json::Value& del = paths["/api/v1/recent"]["delete"];
        del["summary"] = "Forget the recent files";
        del["description"] = "One kind, or all; answers the list as it is now and cleared";
        del["responses"]["200"]["description"] = "Cleared";
    }

    // GET /api/v1/recording/presets — the core's recording presets (capture::RecordingPresets)
    {
        Json::Value& get = paths["/api/v1/recording/presets"]["get"];
        get["summary"] = "Recording presets";
        get["tags"].append("Capture");
        get["description"] =
            "The presets POST /video/record (preset), the CLI (videorecord start --preset), Lua / Python video_record, "
            "MCP capture_media and the action cap.record_toggle take: gameplay (H.264 + AAC mp4, 2x), music (FLAC, "
            "audio only), audio (WAV PCM, audio only), gif, hq (H.265 + FLAC mkv, 2x), multitrack (H.264 + a FLAC "
            "track per sound source mkv). {presets: [{id, title, description, mode, format, container, audio, "
            "video_bitrate, audio_bitrate, fps, scale}]}";
        get["responses"]["200"]["description"] = "The presets";
    }

    // POST /video/record: the options of capture::RecordingControl beyond the ones openapi_capture.inc names
    {
        Json::Value& post = paths["/api/v1/emulator/{id}/video/record"]["post"];
        Json::Value& props = post["requestBody"]["content"]["application/json"]["schema"]["properties"];
        props["action"]["enum"].append("toggle");
        props["action"]["enum"].append("status");
        props["action"]["description"] = "start (default), stop, pause, resume, toggle (stop a running one, else "
                                         "start), status";
        props["preset"]["type"] = "string";
        for (const char* id : {"gameplay", "music", "audio", "gif", "hq", "multitrack"})
            props["preset"]["enum"].append(id);
        props["preset"]["description"] =
            "start: a recording preset (GET /api/v1/recording/presets; its title works too); the other fields win";
        props["mode"]["type"] = "string";
        for (const char* mode : {"single", "multi", "audio", "split"})
            props["mode"]["enum"].append(mode);
        props["mode"]["description"] =
            "start: single (default: one mixed audio track), multi (an audio track per fitted sound source, master "
            "first; needs an audio codec), audio (audio only: format none, a wav / flac / mp3 / ogg / m4a container), "
            "split (a track per AY channel: 501, not implemented yet)";
        props["container"]["type"] = "string";
        props["container"]["description"] =
            "start: the file's extension when no filename is given (mp4, mov, mkv, webm, avi, gif; wav, flac, mp3, "
            "ogg, m4a with mode audio); the default folder is the system temp directory's unreal-mcp";
        post["responses"]["501"]["description"] = "mode split, or a build without recording support";
    }

    // POST /ttd/export-clip: a video file instead of the lossless folder
    {
        Json::Value& post = paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"];
        Json::Value& props = post["requestBody"]["content"]["application/json"]["schema"]["properties"];
        props["path"]["description"] =
            "An absolute output directory (created if missing): the lossless clip; or a .gif / .mp4 / .mkv / .webm / "
            ".mov / .avi file: the frames as a video through the recorder (the TTD engine; video only)";
        props["codec"]["type"] = "string";
        props["codec"]["description"] = "A video path: the codec (default: the container's - gif, h264, vp9 for "
                                        ".webm, rawvideo for .avi)";
        props["scale"]["type"] = "integer";
        props["scale"]["description"] = "A video path: 1..4 (default 1)";
    }
