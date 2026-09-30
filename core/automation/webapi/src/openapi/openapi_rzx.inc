    // RZX playback endpoints (api/rzx_api.cpp; docs/inprogress/2026-09-29-rzx-replay)
    {
        const std::string play = "/api/v1/emulator/{id}/rzx/play";
        paths[play]["post"]["summary"] = "Play an RZX input recording";
        paths[play]["post"]["description"] =
            "Load the recording's start snapshot and play it: every IN returns the recorded value and the "
            "interrupts follow the recorded fetch counts until the end, a desync (strict mode) or rzx/stop; the "
            "machine then runs live. A recording made on another model switches the model first (a new emulator, "
            "media kept; answer: emulator_id, model_switched, previous_emulator_id) unless switch_model is false. "
            "Accepts a JSON body with path and options, multipart/form-data with the file, or the raw file with "
            "X-Filename (options then as query parameters). Max size: 4 MB.";
        paths[play]["post"]["tags"].append("RZX Playback");
        paths[play]["post"]["parameters"][0]["name"] = "id";
        paths[play]["post"]["parameters"][0]["in"] = "path";
        paths[play]["post"]["parameters"][0]["required"] = true;
        paths[play]["post"]["parameters"][0]["schema"]["type"] = "string";
        Json::Value& props = paths[play]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"];
        props["path"]["type"] = "string";
        props["path"]["description"] = "Path to the .rzx file";
        props["desync_mode"]["type"] = "string";
        props["desync_mode"]["enum"].append("strict");
        props["desync_mode"]["enum"].append("tolerant");
        props["desync_mode"]["default"] = "strict";
        props["desync_mode"]["description"] =
            "strict: stop at the first desync (wrong IN count, fetch overrun); tolerant: count them and go on";
        props["ei_short_frame_blocks_int"]["type"] = "boolean";
        props["ei_short_frame_blocks_int"]["default"] = false;
        props["ei_short_frame_blocks_int"]["description"] =
            "A 1-2 fetch frame right after EI means the interrupt was blocked (SkoolKit flag 2)";
        props["ld_air_parity_quirk"]["type"] = "boolean";
        props["ld_air_parity_quirk"]["default"] = false;
        props["ld_air_parity_quirk"]["description"] =
            "Apply the NMOS LD A,I / LD A,R parity quirk on the frame interrupt (SkoolKit flag 1)";
        props["ignore_later_snapshots"]["type"] = "boolean";
        props["ignore_later_snapshots"]["default"] = false;
        props["ignore_later_snapshots"]["description"] =
            "Skip snapshot blocks after the first (SkoolKit flag 4); mid-recording snapshots otherwise stop playback";
        props["switch_model"]["type"] = "boolean";
        props["switch_model"]["default"] = true;
        props["switch_model"]["description"] = "Switch to the recording's model when this machine is another one";
        paths[play]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]["properties"]["file"]["type"] =
            "string";
        paths[play]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]["properties"]["file"]["format"] =
            "binary";
        paths[play]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]["type"] = "string";
        paths[play]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]["format"] = "binary";
        paths[play]["post"]["responses"]["200"]["description"] =
            "Playing: status, emulator_id, model_switched, previous_emulator_id, rzx (the playback status)";
        paths[play]["post"]["responses"]["400"]["description"] =
            "Not a playable RZX file (error: bad_file, no_snapshot, unsupported_machine, snapshot_load_failed)";
        paths[play]["post"]["responses"]["404"]["description"] = "Emulator not found";
        paths[play]["post"]["responses"]["409"]["description"] =
            "model_mismatch with switch_model false (required_model, required_ram_kb), or refused (TTD recording)";

        const std::string stop = "/api/v1/emulator/{id}/rzx/stop";
        paths[stop]["post"]["summary"] = "Stop RZX playback";
        paths[stop]["post"]["description"] = "Stop playing; the machine continues live from the reached state";
        paths[stop]["post"]["tags"].append("RZX Playback");
        paths[stop]["post"]["parameters"][0]["name"] = "id";
        paths[stop]["post"]["parameters"][0]["in"] = "path";
        paths[stop]["post"]["parameters"][0]["required"] = true;
        paths[stop]["post"]["parameters"][0]["schema"]["type"] = "string";
        paths[stop]["post"]["responses"]["200"]["description"] = "stopped: whether a playback was running; rzx: status";
        paths[stop]["post"]["responses"]["404"]["description"] = "Emulator not found";

        const std::string seek = "/api/v1/emulator/{id}/rzx/seek";
        paths[seek]["post"]["summary"] = "Seek the RZX playback";
        paths[seek]["post"]["description"] =
            "Move to the boundary after `frame` frames (0: the start). Forward plays on at full speed; back restores "
            "the nearest keyframe (taken every keyframe_interval frames, thinned to a memory budget) and plays on. "
            "Works after the end or a desync (the playback resumes); refused while TTD records, a stopped TTD history "
            "is dropped. The machine stays paused or running as it was.";
        paths[seek]["post"]["tags"].append("RZX Playback");
        paths[seek]["post"]["parameters"][0]["name"] = "id";
        paths[seek]["post"]["parameters"][0]["in"] = "path";
        paths[seek]["post"]["parameters"][0]["required"] = true;
        paths[seek]["post"]["parameters"][0]["schema"]["type"] = "string";
        paths[seek]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["frame"]["type"] =
            "integer";
        paths[seek]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"].append("frame");
        paths[seek]["post"]["responses"]["200"]["description"] = "Moved: rzx (the status), summary";
        paths[seek]["post"]["responses"]["400"]["description"] = "No or bad frame";
        paths[seek]["post"]["responses"]["404"]["description"] = "Emulator not found";
        paths[seek]["post"]["responses"]["409"]["description"] = "Nothing to seek (no playback, stopped, TTD recording)";

        const std::string status = "/api/v1/emulator/{id}/rzx/status";
        paths[status]["get"]["summary"] = "RZX playback status";
        paths[status]["get"]["description"] =
            "loaded, active, path, creator, version, snapshot, state (playing / finished / desynced / stopped), "
            "frame, total_frames, progress, block, blocks, interrupts, desyncs, first_desync {kind, frame, block, "
            "expected, actual, pc, port}, drift / max_drift (T-states between the recorded interrupt and the "
            "machine's own), keyframes / keyframe_bytes / keyframe_interval (for seeking back), options, reason, summary";
        paths[status]["get"]["tags"].append("RZX Playback");
        paths[status]["get"]["parameters"][0]["name"] = "id";
        paths[status]["get"]["parameters"][0]["in"] = "path";
        paths[status]["get"]["parameters"][0]["required"] = true;
        paths[status]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[status]["get"]["responses"]["200"]["description"] = "Playback status";
        paths[status]["get"]["responses"]["404"]["description"] = "Emulator not found";
    }
