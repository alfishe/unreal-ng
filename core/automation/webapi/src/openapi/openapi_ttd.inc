    // Time-Travel Debug endpoints (parent TDD §10.4).
    //
    // Phase 2 surface (10 endpoints) + Phase 4 reverse-search (3 endpoints)
    // + Phase 4 reverse-execution (2 endpoints) + session restore via load
    // (1 endpoint) + TD-4 agent bookmarks (5 endpoints) = 21 endpoints total,
    // all exposed by core/automation/webapi/src/api/ttd_api.cpp.
    // The status endpoint also serves as a capability probe: it returns 200
    // with state="idle" and ttd_available=false when the TTD engine is not
    // compiled in. All other endpoints return 501 in that case.

    // =====================================================================
    // GET /ttd/status — session snapshot
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["summary"] =
        "Get TTD session state (parent TDD §10.4 — test observability)";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["description"] =
        "Returns the current TTD session state, including session frame bounds, "
        "checkpoint count, and COW page-store memory usage. Doubles as a "
        "capability probe: when TTD is not compiled in or no manager is "
        "present, returns 200 with state=\"idle\" and ttd_available=false.";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["200"]["description"] =
        "TTD session snapshot";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStatusResponse";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["503"]["description"] =
        "Emulator is shutting down";

    // =====================================================================
    // POST /ttd/start — begin recording
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["summary"] =
        "Start TTD recording (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["description"] =
        "Begins recording. Captures the baseline checkpoint at the current "
        "frame, transitions the session to state=\"recording\", and starts "
        "per-frame checkpoint capture. Idempotent: returns already_active=true "
        "if recording was already in progress. A start the recorder refuses is a 409 whose message gives the reason "
        "(\"the recording did not start: ...\").\n\n"
        "**Optional JSON body:**\n"
        "- `journal`: also record the write journal (default false). \"Who wrote this address last\" then answers "
        "at once; without it the search replays one frame (same answer, slower). It can be switched at any moment "
        "(POST /ttd/journal) and built later for any span by replay (POST /ttd/journal/build)\n"
        "- `history_limit_frames`, `history_limit_bytes`: the history limit, as POST /ttd/history-limit "
        "(a missing one keeps its current value)\n"
        "- `black_box`: record as the black box (the engine; D29): keep the last `minutes` (default 5, 1..1440) "
        "and never hold turbo or the host speed - the recording stops before either takes effect, its history kept, "
        "and a new session starts once nothing accelerates; fast loaders' traps are recorded. Absent or false: an "
        "explicit recording, which holds the machine at real speed";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["responses"]["200"]["description"] =
        "Recording started (or already active)";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStartResponse";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";
    paths["/api/v1/emulator/{id}/ttd/start"]["post"]["responses"]["503"]["description"] =
        "Emulator is shutting down";

    // =====================================================================
    // /ttd/journal - the write journal on demand (D40)
    // =====================================================================
    {
        const char* path = "/api/v1/emulator/{id}/ttd/journal";
        for (const char* method : {"get", "post"})
        {
            Json::Value& op = paths[path][method];
            op["tags"].append("Time-Travel Debug");
            op["parameters"][0]["name"] = "id";
            op["parameters"][0]["in"] = "path";
            op["parameters"][0]["required"] = true;
            op["parameters"][0]["schema"]["type"] = "string";
            op["responses"]["200"]["description"] = "The write journal's state and the spans it covers";
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/TTDJournalResponse";
            op["responses"]["404"]["description"] = "Emulator instance not found";
        }
        paths[path]["get"]["summary"] = "The write journal's state and spans";
        paths[path]["get"]["description"] =
            "Whether the write journal is recorded, the spans of the session it covers, and a build in progress. "
            "Inside a span, \"who wrote this address last\" answers from the journal at once; outside, the search "
            "replays one frame (same answer).";
        paths[path]["post"]["summary"] = "Switch the write journal on or off";
        paths[path]["post"]["description"] =
            "Body: `{\"enabled\": true | false}`. Allowed at any moment, also while recording: a journal segment "
            "starts or ends at the instruction the machine is on (a running machine is paused for the switch and "
            "resumed). Off by default.";
        paths[path]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        paths[path]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["enabled"]["type"] =
            "boolean";
        paths[path]["post"]["responses"]["400"]["description"] = "The body has no boolean 'enabled'";
    }
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/journal/build"]["post"];
        op["summary"] = "Build the write journal by replay";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "Replays frames `from_frame`..`to_frame` (both optional; default: the whole session) with every memory "
            "write collected, and adds them to the write journal: the frames join its spans. Frames already covered "
            "are left as they are; the session's last frame and frames holding a barrier (a marker without the data a "
            "replay needs) are not built. About 2-4 ms per frame, like RZX playback: the request answers when done; GET /ttd/journal shows "
            "the progress meanwhile (`write_journal_build`) and POST /ttd/journal/build/cancel stops it (what was "
            "built is kept). Not while recording.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["from_frame"]["type"] = "integer";
        body["properties"]["to_frame"]["type"] = "integer";
        op["responses"]["200"]["description"] = "Built (or cancelled; see `cancelled`)";
        op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
            "#/components/schemas/TTDJournalBuildResponse";
        op["responses"]["409"]["description"] = "Refused: recording, or no complete frame to replay (`error` says which)";
        op["responses"]["404"]["description"] = "Emulator instance not found";
    }
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/journal/build/cancel"]["post"];
        op["summary"] = "Stop a running write journal build";
        op["tags"].append("Time-Travel Debug");
        op["description"] = "The build stops after its current frame; what it built is kept. `cancelled` is false "
                            "when no build was running.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["responses"]["200"]["description"] = "`{\"cancelled\": bool}`";
        op["responses"]["404"]["description"] = "Emulator instance not found";
    }

    // =====================================================================
    // POST /ttd/history-limit — keep only the newest history while recording
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/history-limit"]["post"];
        op["summary"] = "Set or read the TTD history limit";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "Bounds the recorded history: while recording, the oldest checkpoints (one per frame) are released "
            "once there are more than `frames` of them or their data exceeds `bytes`. The session start moves "
            "forward; the input, port, external-event and bookmark records before it go too, so a session saved "
            "after a release loads and replays its remaining frames exactly. Two checkpoints always stay.\n\n"
            "Body (each field optional; a missing one keeps its value; 0 = no limit; an empty body only reports): "
            "`frames` (integer), `bytes` (integer). While recording the limit applies at once, otherwise from "
            "the next recorded frame. The limit stays for later recordings of this instance. Default: no limit.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["frames"]["type"] = "integer";
        body["properties"]["frames"]["minimum"] = 0;
        body["properties"]["frames"]["description"] = "Keep at most this many checkpoints (frames); 0 = no limit";
        body["properties"]["bytes"]["type"] = "integer";
        body["properties"]["bytes"]["minimum"] = 0;
        body["properties"]["bytes"]["description"] =
            "Keep checkpoint data (RAM pages + device blobs, compressed) under this many bytes; 0 = no limit";
        op["requestBody"]["required"] = false;
        Json::Value& r = op["responses"]["200"]["content"]["application/json"]["schema"];
        op["responses"]["200"]["description"] = "The limit now in force and the history it bounds";
        r["type"] = "object";
        for (const char* name : {"history_limit_frames", "history_limit_bytes", "history_bytes", "evicted_checkpoints",
                                 "checkpoint_count", "session_start_frame", "current_end_frame"})
        {
            r["properties"][name]["type"] = "integer";
            r["properties"][name]["format"] = "uint64";
        }
        r["properties"]["history_bytes"]["description"] = "Checkpoint data held now (what `bytes` bounds)";
        r["properties"]["evicted_checkpoints"]["description"] = "Checkpoints released by the limit in this session";
        r["properties"]["state"]["type"] = "string";
        op["responses"]["400"]["description"] = "frames or bytes is not a non-negative integer";
        op["responses"]["404"]["description"] = "Emulator instance not found";
        op["responses"]["501"]["description"] = "TTD engine not available in this build";
    }

    // =====================================================================
    // POST /ttd/stop — stop recording (history retained, browsable)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["summary"] =
        "Stop TTD recording (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["description"] =
        "Stops recording. The session transitions to state=\"detached\" — "
        "history is retained and remains browsable via seek / step until "
        "InvalidateSession is called or a new recording starts. Use "
        "resume to resume recording from the current position.";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["responses"]["200"]["description"] =
        "Recording stopped (or was not active)";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStopResponse";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/stop"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/invalidate — drop all history, return to Idle
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["summary"] =
        "Invalidate TTD session — drop all history (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["description"] =
        "Drops every checkpoint, journal entry, and external-event marker. "
        "The session returns to state=\"idle\". The next StartRecording "
        "begins a fresh timeline at the current frame. Optional JSON body "
        "carries a human-readable reason that is logged and surfaced in "
        "subsequent status queries.";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["requestBody"]["description"] =
        "Optional reason string recorded in the session log";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDInvalidateRequest";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["responses"]["200"]["description"] =
        "Session invalidated";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDInvalidateResponse";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["responses"]["409"]["description"] =
        "A recording is running: stop it first, then discard the session";
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/export-clip — a frame range written to disk as a lossless clip
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["summary"] = "Export a TTD frame range as a clip";
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["description"] =
        "Writes every frame of [from, to] as its final beam-rendered picture - what positioning by frame number "
        "shows - into the directory `path`: rgba_NNNN.zst (RGBA8 frames), planeb_NNNN.zst (ZX DLSS plane B, when "
        "the 'zxdlss' feature is on), meta.jsonl (frame, #7FFD, displayed screen, border at the frame's start) and "
        "clip.json (geometry, encodings). Runs inside the core in one request; the emulator is paused and left "
        "positioned at `to`. 409 while recording.";
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["parameters"][0]["schema"]["type"] = "string";
    {
        Json::Value& body = paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["requestBody"];
        body["required"] = true;
        Json::Value& schema = body["content"]["application/json"]["schema"];
        schema["type"] = "object";
        schema["required"].append("from");
        schema["required"].append("to");
        schema["required"].append("path");
        schema["properties"]["from"]["type"] = "integer";
        schema["properties"]["to"]["type"] = "integer";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "Absolute output directory (created if missing)";
        schema["properties"]["chunk"]["type"] = "integer";
        schema["properties"]["chunk"]["description"] = "Frames per zstd chunk (default 500)";
    }
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["responses"]["200"]["description"] =
        "Export finished: ok, frames, bytes, planeb, width, height, seconds, path";
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["responses"]["400"]["description"] =
        "Missing fields, range outside the session or write failure (error)";
    paths["/api/v1/emulator/{id}/ttd/export-clip"]["post"]["responses"]["409"]["description"] =
        "Session is recording";

    // POST /ttd/seek — seek to (frame, tinframe)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["summary"] =
        "Seek to a point in the timeline (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["description"] =
        "Restores the checkpoint on or before the target time, then replays "
        "forward. The session transitions to state=\"detached\" on success. "
        "The target may be given as coordinates (frame [+ tinframe]) or as a "
        "TD-4 bookmark label (bookmark) — a bookmark is advisory, so seeking "
        "to one is a plain arrival with halt_reason=\"target\". Replay stops "
        "at the first external-event marker on or after the target — the "
        "response carries halt_reason=\"external_event\" and a blocking_marker "
        "payload describing the barrier (a bookmark never is one). Use resume "
        "to continue past a barrier by truncating history at the current "
        "position.";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["requestBody"]["description"] =
        "Target time point";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDSeekRequest";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["responses"]["200"]["description"] =
        "Seek completed (check halt_reason and reached flags)";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDSeekResponse";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["responses"]["400"]["description"] =
        "Missing required field: frame (or bookmark)";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found, or unknown bookmark label";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/step-back — step back one frame
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["summary"] =
        "Step back one frame (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["description"] =
        "Seeks to (current_frame - 1, current_tInFrame). Composition of "
        "SeekTo. Fails (stepped=false) when the resulting position would "
        "precede the session start. The session transitions to "
        "state=\"detached\" on success.";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["responses"]["200"]["description"] =
        "Step attempted (check stepped flag)";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStepResponse";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/step-back"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/step-forward — step forward one frame
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["summary"] =
        "Step forward one frame (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["description"] =
        "Seeks to (current_frame + 1, current_tInFrame). Composition of "
        "SeekTo. Fails (stepped=false) when the resulting position would "
        "exceed the session end. The session transitions to "
        "state=\"detached\" on success.";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["responses"]["200"]["description"] =
        "Step attempted (check stepped flag)";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStepResponse";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/step-forward"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/resume — resume recording from current or specified point
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["summary"] =
        "Resume recording from a past point (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["description"] =
        "Truncates the timeline at the resume point and resumes recording. "
        "If the request body is omitted, resumes from the current position. "
        "On success the session transitions to state=\"recording\". Used "
        "to continue past an external-event marker that blocked a seek. Every medium "
        "goes back to the resume point when it can (the media undo log); one that cannot "
        "is decided by media (empty by default, keep, or ask).";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["requestBody"]["description"] =
        "Optional resume point; omit to resume from current position";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDResumeRequest";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["responses"]["200"]["description"] =
        "Resume attempted (check resumed flag)";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDResumeResponse";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/resume"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // GET /ttd/position — current + session_end time points
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["summary"] =
        "Get current TTD position (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["description"] =
        "Returns the current TTDTimePoint (frame + tInFrame) and the "
        "session-end time point. Useful for slider UIs and for validating "
        "that a step/seek landed where expected.";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["responses"]["200"]["description"] =
        "Current position + session end";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDPositionResponse";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/position"]["get"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // GET /ttd/markers — list external-event markers (replay barriers)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["summary"] =
        "List TTD external-event markers (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["description"] =
        "Returns every external-event marker recorded in this session. "
        "Each marker is a replay barrier: a SeekTo whose target lies on "
        "or after a marker will halt at the marker rather than crossing "
        "it. Kinds: tape_control, disk_write, debugger_edit (a tool edit made "
        "while recording), other. hardware_reset is a reserved kind that is "
        "never written: a reset stops the recording instead.";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["responses"]["200"]["description"] =
        "Marker list";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDMarkersResponse";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/markers"]["get"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // GET /ttd/bookmarks — list agent bookmarks (TD-4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["summary"] =
        "List TTD agent bookmarks (TD-4 — advisory annotations)";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["description"] =
        "Returns every agent bookmark in this session, time-sorted. "
        "Bookmarks are advisory annotations stored beside the timeline and "
        "serialized in the .ttd session — unlike /ttd/markers they never "
        "halt a seek and never appear as a halt_reason.";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["responses"]["200"]["description"] =
        "Bookmark list";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDBookmarksResponse";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["get"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/bookmarks — add an agent bookmark (TD-4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["summary"] =
        "Add a TTD agent bookmark (TD-4 — advisory annotation)";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["description"] =
        "Adds a named bookmark at (frame, tinframe). When frame is omitted "
        "the bookmark is placed at the current position — the one-call "
        "\"mark here\". Labels are keys: non-empty, at most 63 characters, "
        "unique per session. Bookmarks are advisory and explicitly NOT replay "
        "barriers; they are serialized with the session (dump/load round-trips "
        "preserve them).";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["requestBody"]["description"] =
        "Bookmark label and optional position (defaults to the current position)";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDBookmarkAddRequest";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["responses"]["201"]["description"] =
        "Bookmark created";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDBookmarkAddedResponse";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["responses"]["400"]["description"] =
        "Label contract violation (missing / empty / over 63 characters)";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["responses"]["409"]["description"] =
        "Duplicate label, no recorded history, or position beyond the session end";
    paths["/api/v1/emulator/{id}/ttd/bookmarks"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // PUT /ttd/bookmarks/{label} — edit a bookmark's note, tags, label
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["summary"] = "Edit a TTD agent bookmark: note, tags, label";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["description"] =
        "Only the fields given change: note (append: as a new line), tags (replace), add_tags, remove_tags, "
        "rename. The bookmark's time and context stay.";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][1]["name"] = "label";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/TTDBookmarkNoteRequest";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["responses"]["200"]["description"] = "The bookmark as it is now (bookmark, text)";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["responses"]["400"]["description"] = "Nothing to change, or a bound broken";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["responses"]["404"]["description"] = "Emulator instance not found, or unknown bookmark label";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["put"]["responses"]["409"]["description"] = "rename onto an existing label";

    // =====================================================================
    // GET / POST /ttd/export-bookmarks — the bookmarks written up
    // =====================================================================
    for (const char* method : {"get", "post"})
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/export-bookmarks"][method];
        op["summary"] = "Export the TTD agent bookmarks: WALKTHROUGH section, profile comment or JSON";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "format = markdown (default: a WALKTHROUGH section, one block per bookmark with its note, context, "
            "watched bytes, tags and the seek call), profile (YAML comment lines of at most 120 columns for a game "
            "profile's header) or json; tag (only those), title, path (written there too). GET takes them as the "
            "query, POST as the body. The machine does not move.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["responses"]["200"]["description"] = "The export";
        op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
            "#/components/schemas/TTDBookmarkExportResponse";
        op["responses"]["400"]["description"] = "Unknown format";
        op["responses"]["409"]["description"] = "The file could not be written";
    }
    for (const char* name : {"format", "tag", "title", "path"})
    {
        Json::Value p;
        p["name"] = name;
        p["in"] = "query";
        p["schema"]["type"] = "string";
        paths["/api/v1/emulator/{id}/ttd/export-bookmarks"]["get"]["parameters"].append(p);
    }

    // =====================================================================
    // DELETE /ttd/bookmarks/{label} — remove an agent bookmark (TD-4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["summary"] =
        "Delete a TTD agent bookmark by label (TD-4)";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["description"] =
        "Removes the bookmark with the given label. The label becomes free "
        "for reuse. URL-encode labels containing spaces or slashes "
        "(e.g. \"umt%20entry\").";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][1]["name"] = "label";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["responses"]["200"]["description"] =
        "Bookmark removed";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDBookmarkDeletedResponse";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["responses"]["404"]["description"] =
        "Emulator instance not found, or unknown bookmark label";
    paths["/api/v1/emulator/{id}/ttd/bookmarks/{label}"]["delete"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/dump — serialize session to .ttd file (Phase 4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["summary"] =
        "Dump TTD session to .ttd file (Phase 4 reverse search)";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["description"] =
        "Serializes the current TTD session to a .ttd binary file at the "
        "given filesystem path: the engine's session file (schema 2, Kaitai "
        "schema core/src/debugger/ttd/ttd.ksy).";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["requestBody"]["description"] =
        "Output path for the .ttd file";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDDumpRequest";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["responses"]["200"]["description"] =
        "Dump attempted (check ok flag)";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDDumpResponse";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["responses"]["400"]["description"] =
        "Missing required field: path";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/dump"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/load — restore a dumped session
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["summary"] =
        "Load TTD session from .ttd file (companion of dump)";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["description"] =
        "Restores a session previously written by /ttd/dump. A session only "
        "restores into an instance of the model it was recorded on — the "
        "refusal reason names both model ids, so provision a matching "
        "instance (POST /api/v1/emulator/create) and retry. After a "
        "successful load the session is idle: use /ttd/seek to position "
        "the emulator inside the loaded timeline. The path may also be a recording's folder under "
        "~/.unreal-ng/ttd/ (or one of its segment-NNNN.ttd files): its segments are joined and loaded.";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["requestBody"]["description"] =
        "Input path of the .ttd file, or of a recording's folder";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDLoadRequest";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["responses"]["200"]["description"] =
        "Session loaded (check ok flag)";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDLoadResponse";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["responses"]["400"]["description"] =
        "Missing required field: path, or session could not be deserialized";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found or file cannot be opened";
    paths["/api/v1/emulator/{id}/ttd/load"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/find-last — reverse search: find last access at address (Phase 4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["summary"] =
        "Reverse search: find last memory access at address (Phase 4 reverse search)";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["description"] =
        "Searches backward through the recorded timeline for the most recent "
        "memory access matching the query. Returns the TTDTimePoint, PC, value, "
        "and physical page of the matching access. Optional filters: access "
        "type, value, PC range, and before-frame/tin bound. If a blocking "
        "external-event marker prevents the scan from reaching the matching "
        "access, the response carries blocked=true with the marker details.";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["requestBody"]["description"] =
        "Search query";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDFindLastRequest";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["responses"]["200"]["description"] =
        "Search completed (check found flag)";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDFindLastResponse";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["responses"]["400"]["description"] =
        "Missing search criteria, a bad number, or phys_page outside 0..255";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/find-last"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/memory-at, /ttd/memory-diff - a memory at a past checkpoint, and what changed (memory spaces)
    // =====================================================================
    {
        const char* kAt = "/api/v1/emulator/{id}/ttd/memory-at";
        const char* kDiff = "/api/v1/emulator/{id}/ttd/memory-diff";
        for (const char* route : {kAt, kDiff})
        {
            Json::Value& op = paths[route]["post"];
            op["tags"].append("Time-Travel Debug");
            op["parameters"][0]["name"] = "id";
            op["parameters"][0]["in"] = "path";
            op["parameters"][0]["required"] = true;
            op["parameters"][0]["schema"]["type"] = "string";
            op["requestBody"]["required"] = true;
            Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
            body["type"] = "object";
            body["properties"]["space"]["type"] = "string";
            body["properties"]["space"]["description"] =
                "ram, ramN (machine RAM page N), or a memory region by name or alias (GET /memory/regions: "
                "sprinter.vram / vram, sprinter.fastram / cache, neogs.ram, gs.ram, moonsound.wave, evo.flash, ...)";
            body["required"].append("space");
            op["responses"]["400"]["description"] = "Missing or bad option, a memory the session does not record, a frame "
                                                    "before the first checkpoint";
            op["responses"]["404"]["description"] = "No recorded history, or the instance is not found";
            op["responses"]["501"]["description"] = "Time travel is not available for this instance";
        }
        Json::Value& at = paths[kAt]["post"];
        at["summary"] = "Read a memory as it was at a past checkpoint, without seeking";
        at["description"] =
            "Bytes of any memory the session records (machine RAM, the Sprinter's video and fast RAM, sound card RAM and "
            "flash, VDAC2, EEPROMs ...) at the newest checkpoint at or before 'frame', from the engine's store: the "
            "machine does not move. at_frame names the checkpoint read, exact whether it is the frame asked for. ram / "
            "ramN offsets are physical. Space cpu: offset is a Z80 address (offset + length at most 65536) and the bytes "
            "are what the CPU saw at frame / tinframe through its paging then (a silent replay, the machine restored), "
            "with banks [{window, addr, kind, page, name}].";
        Json::Value& atBody = at["requestBody"]["content"]["application/json"]["schema"];
        atBody["properties"]["frame"]["type"] = "integer";
        atBody["properties"]["offset"]["type"] = "string";
        atBody["properties"]["offset"]["description"] = "Byte offset in the space (number, 0x.., #..), default 0";
        atBody["properties"]["length"]["type"] = "integer";
        atBody["properties"]["length"]["description"] = "Bytes, 1..65536, default 256 (clipped at the memory's end)";
        atBody["required"].append("frame");
        atBody["properties"]["tinframe"]["type"] = "integer";
        atBody["properties"]["tinframe"]["description"] = "A point inside the frame (ram, ramN, vram, cache, cpu)";
        at["responses"]["200"]["description"] = "{space, offset, length, frame, at_frame, exact, hex}; space cpu adds banks";
        Json::Value& diff = paths[kDiff]["post"];
        diff["summary"] = "What changed in a memory between two checkpoints";
        diff["description"] =
            "The byte ranges of a memory that differ between the checkpoints at or before from_frame and to_frame (the "
            "pieces whose stored versions differ, compared byte by byte). ramN compares that page only.";
        Json::Value& diffBody = diff["requestBody"]["content"]["application/json"]["schema"];
        diffBody["properties"]["from_frame"]["type"] = "integer";
        diffBody["properties"]["to_frame"]["type"] = "integer";
        diffBody["properties"]["limit"]["type"] = "integer";
        diffBody["properties"]["limit"]["description"] = "Ranges reported, default 100 (the rest set truncated)";
        diffBody["required"].append("from_frame");
        diffBody["required"].append("to_frame");
        diff["responses"]["200"]["description"] =
            "{space, from_frame, to_frame, at_from, at_to, changed_bytes, ranges: [{offset, length}], truncated}";
    }

    // =====================================================================
    // POST /ttd/port-events — "when did the program ..." over the port journals
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["summary"] =
        "Find when the program saw a key, the tape signal, or accessed the AY, border, a port";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["description"] =
        "Searches the session's port journals - every IN result and every OUT of the "
        "main CPU with its time and PC - for a named event: 'key' [key name] (the "
        "program saw a key down; a named key counts only in reads of its half-row "
        "alone), 'ear' (the program saw the tape signal change), 'ay-read' / "
        "'ay-write' [register] (the register selection is followed through the OUT "
        "journal), 'ay-select', 'border', 'beeper', or 'in' / 'out' narrowed with the "
        "raw filters. The search replays nothing: it answers the same on a loaded .ttd file. A read of the ULA "
        "names the half-rows it selected and the keys it saw down; with 'context' (default on, off while a "
        "recording runs) each hit carries the registers at its instruction and the word at SP (the caller), replayed "
        "with the machine put back. "
        "409 when the session has no port journal (TSConf, ZX Next, NeoGS, a file "
        "without it), or when a long run keeps a running recording's CPU busy (pause and ask again). With 'file' it searches a .ttd on "
        "disk instead, without loading it (400 when the file cannot be read or has no journals).";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/TTDPortEventsRequest";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["responses"]["200"]["description"] = "Hits in time order (newest first with 'newest')";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/TTDPortEventsResponse";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["responses"]["400"]["description"] = "Unknown event, bad argument or option";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["responses"]["404"]["description"] = "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["responses"]["409"]["description"] =
        "No port journal in the session, or a recording is running (pause or stop it)";
    paths["/api/v1/emulator/{id}/ttd/port-events"]["post"]["responses"]["501"]["description"] = "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/step-instruction — step one instruction back or forward (Phase 4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["summary"] =
        "Step one Z80 instruction back or forward (Phase 4 reverse search)";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["description"] =
        "Single-instruction step in either direction. Uses M1-cycle boundaries "
        "so the resulting position is always at an instruction start. Backward "
        "step uses the M1 probe to locate the previous M1; forward step replays "
        "forward to the next M1. Fails (stepped=false) at session boundaries.";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["requestBody"]["description"] =
        "Direction (default: back)";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStepInstructionRequest";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["responses"]["200"]["description"] =
        "Step attempted (check stepped flag)";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStepInstructionResponse";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/step-instruction"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/reverse-step — multi-step backward (Phase 4 reverse execution)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["summary"] =
        "Step back N instructions, T t-states or L scanlines (Phase 4 reverse execution)";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["description"] =
        "Multi-step backward jump. Exactly one of 'count' (instructions), "
        "'tstates' (T-states) or 'scanlines' (the machine's t_line each) must be specified. For small N (<=4) delegates "
        "to repeated single-step; for larger N uses a single M1 enumeration "
        "pass. T-state mode aligns the landing to the nearest M1 boundary "
        "at or before the target T-state count.";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["requestBody"]["description"] =
        "Step size — exactly one of count, tstates or scanlines";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDReverseStepRequest";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["responses"]["200"]["description"] =
        "Step attempted (check reached flag)";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDReverseStepResponse";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["responses"]["400"]["description"] =
        "Missing or conflicting 'count'/'tstates' fields";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/reverse-continue — run backward until PC matches (Phase 4)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["summary"] =
        "Run backward until any PC matches (Phase 4 reverse execution)";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["description"] =
        "Backward breakpoint scan. Enumerates M1 cycles from session start "
        "to the current position and finds the most recent M1 whose PC is in "
        "the supplied list. Equivalent to 'reverse-continue' in WinDbg TTD. "
        "Response carries matched=true with the matched PC and arrival time "
        "point; matched=false when no PC in the list was visited. A blocking "
        "external-event marker may also short-circuit the scan; the response then "
        "carries blocked_by_marker {kind, reason, frame, tinframe}. pcs entries "
        "may be numbers or strings (decimal, \"0x..\", \"#..\", \"$..\").";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["requestBody"]["description"] =
        "Reverse breakpoints";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDReverseContinueRequest";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["responses"]["200"]["description"] =
        "Scan completed (check matched flag)";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDReverseContinueResponse";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["responses"]["400"]["description"] =
        "Missing or empty 'pcs' array, or an entry outside 0..65535";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/reverse-continue"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/probe-frames — the frames a game profile's probes fired in
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["summary"] = "The frames a game profile's probes fired in (a replay of the window)";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["description"] =
        "Replays each complete recorded frame of [from_frame, to_frame] with the probes armed as taps of the "
        "replay's own sink, so they match as in real time (banks, pages, port masks, the `if` condition with its "
        "operands); the live haptics, the user's breakpoints and the deck hear nothing, and the machine stays where "
        "it stood. A frame the coverage index proves free of every memory probe is not replayed (pruned_frames); "
        "a frame a replay barrier cuts is skipped and listed in barrier_frames. Body: probes, an array of at most 16 "
        "objects {probe: exec|read|write|port-in|port-out, address, end?, page?, port_mask?, if?} (numbers or "
        "strings, decimal or hex), or one probe's fields at the top level; from_frame?, to_frame?, limit? (frames "
        "listed per probe, default 1000, at most 10000). A probe whose condition does not compile has error and no "
        "frames. When every probe's list is full and the window goes on, truncated is true and next_from_frame is "
        "the next call's from_frame. A recording running is paused for the query.";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["probes"]["type"] = "array";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["probes"]["items"]["type"] = "object";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["from_frame"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["to_frame"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["limit"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["responses"]["200"]["description"] =
        "{from_frame, to_frame, replayed_frames, pruned_frames, barrier_frames, blocked_by_marker?, probes: "
        "[{probe, address, end?, page?, port_mask?, if?, error?, frame_count, hit_count, frames, hits, truncated}], "
        "truncated, next_from_frame?}";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["responses"]["400"]["description"] = "A probe field is malformed, or more than 16 probes";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["responses"]["404"]["description"] = "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["responses"]["409"]["description"] = "No complete recorded frame in the window";
    paths["/api/v1/emulator/{id}/ttd/probe-frames"]["post"]["responses"]["501"]["description"] = "TTD engine not available in this build";

    // =====================================================================
    // GET /ttd/coverage/probe — probe address range containment for a frame
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["summary"] =
        "Probe coverage for a single frame (TD-7 §3.1.1)";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["description"] =
        "Boolean point query checking if a specified frame touched an address range. Frames outside the "
        "index covered range report index_available=false (touched=false). Returns 400 when 'frame' is "
        "missing, 'kind' is not executed/written/read, addr_from exceeds addr_to, or values are malformed.";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][1]["name"] = "frame";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][1]["description"] =
        "Frame number to probe (emulator-absolute frame index)";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][2]["name"] = "space";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/coverage/probe"]["get"]["parameters"][2]["description"] =
        "ram (default), vram (the Sprinter's video RAM) or cache (its fast RAM): addr_from / addr_to are then "
        "offsets in that memory, inside one 16 KB page; the answer names space, offset_from, offset_to";

    // =====================================================================
    // GET /ttd/coverage/scan — scan matching frames for address range
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["summary"] =
        "Scan frames touching address range (TD-7 §3.1.2)";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["description"] =
        "Multi-frame range scan returning frame numbers touching a given address range. The request window "
        "is clamped to the index covered range, echoed as covered_from/covered_to. Returns 400 when "
        "'kind' is not executed/written/read, addr_from exceeds addr_to, or values are malformed.";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][1]["name"] = "space";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"][1]["description"] =
        "ram (default), vram (the Sprinter's video RAM) or cache (its fast RAM): addr_from / addr_to are then "
        "offsets in that memory, inside one 16 KB page; the answer names space, offset_from, offset_to";
    {
        Json::Value& params = paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["parameters"];
        const char* const extra[][2] = {
            {"phys_page", "0..255, or rom (the bucket all ROM shares). Without it on space ram the offset matches on "
                          "every page and 16 KB window (exact: false in the answer)"},
            {"limit", "List entries, 1..10000 (default 200): frames, or runs with ranges=true"},
            {"order", "oldest (default) or newest: newest lists the newest matches first"},
            {"ranges", "true: ranges[[from, to], ...] (runs of consecutive matching frames) instead of frames[]"}};
        for (const auto& [name, text] : extra)
        {
            Json::Value param;
            param["name"] = name;
            param["in"] = "query";
            param["schema"]["type"] = "string";
            param["description"] = text;
            params.append(param);
        }
        paths["/api/v1/emulator/{id}/ttd/coverage/scan"]["get"]["responses"]["200"]["description"] =
            "{kind, addr_from, addr_to, phys_page, order, exact, frames[] or ranges[], first_match, last_match (the true "
            "ends of the window, also when the list is cut), matching_frames, scanned_frames, truncated, "
            "next_from_frame (oldest order) / next_to_frame (newest order) when cut: pass it back as from_frame / "
            "to_frame, index_available, covered_from, covered_to}";
    }

    // =====================================================================
    // GET /ttd/coverage/rare — addresses touched in at most N frames, as blocks
    // =====================================================================
    {
        Json::Value& get = paths["/api/v1/emulator/{id}/ttd/coverage/rare"]["get"];
        get["summary"] = "Rare code or data: addresses touched in at most max_frames frames, as blocks";
        get["tags"].append("Time-Travel Debug");
        get["description"] =
            "One pass over the coverage index (no replay): the keys of the address range touched in at most "
            "max_frames frames of the window, neighbouring keys of one page with the same frames joined into a "
            "block (executed: up to 4 bytes apart; written / read: adjacent), rarest first. Returns 400 like scan, "
            "and when max_frames is not 1..1000.";
        get["parameters"][0]["name"] = "id";
        get["parameters"][0]["in"] = "path";
        get["parameters"][0]["required"] = true;
        const char* const params[][2] = {
            {"from_frame", "First frame of the window (default: the session start)"},
            {"to_frame", "Last frame of the window (default: the session end)"},
            {"kind", "executed (default), written or read"},
            {"addr_from", "Range start (Z80 address; with space vram / cache an offset in that memory)"},
            {"addr_to", "Range end, inclusive"},
            {"phys_page", "0..255, or rom (the bucket all ROM shares): only that page"},
            {"space", "ram (default), vram or cache (the Sprinter's memories)"},
            {"max_frames", "A key is rare when at most this many frames touched it, 1..1000 (default 1)"},
            {"limit", "Blocks listed, 1..10000 (default 200)"}};
        for (const auto& [name, text] : params)
        {
            Json::Value param;
            param["name"] = name;
            param["in"] = "query";
            param["schema"]["type"] = "string";
            param["description"] = text;
            get["parameters"].append(param);
        }
        get["responses"]["200"]["description"] =
            "{kind, addr_from, addr_to, max_frames, scanned_frames, unindexed_frames, distinct_keys, rare_keys, "
            "rare_blocks (all, also when the list is cut), truncated, index_available, covered_from, covered_to, "
            "blocks[{phys_page (0..255 or rom) or space + offset_from, page_offset_from, page_offset_to, addr_from / "
            "addr_to (where the page was mapped at the start of the first frame, when one window showed it), keys, "
            "frame_count, frames[]}]}";
    }

    // =====================================================================
    // GET /ttd/coverage/summary — activity heatmap across frame intervals
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/coverage/summary"]["get"]["summary"] =
        "Activity heatmap over frame intervals (TD-7 §3.1.3)";
    paths["/api/v1/emulator/{id}/ttd/coverage/summary"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/coverage/summary"]["get"]["description"] =
        "Aggregated activity heatmap reporting distinct address counts per frame bucket. When the index "
        "is available the covered window is echoed as covered_from/covered_to. Returns 400 when 'kind' "
        "is not executed/written/read or values are malformed.";
    paths["/api/v1/emulator/{id}/ttd/coverage/summary"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/coverage/summary"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/coverage/summary"]["get"]["parameters"][0]["required"] = true;

    // =====================================================================
    // GET /ttd/timeline — the recording cut into phases (TD-5)
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/timeline"]["get"];
        op["summary"] = "The recording cut into phases of the same activity (TD-5)";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "Every frame's facts the session holds (coverage distinct keys, changed RAM pieces and screen frames, "
            "halted frames, port / sector / vector counts, keys and other input, per port-map device IN / OUT, per "
            "media slot sectors, border / paging / frame length changes), tagged over a window of min_phase_ms and cut "
            "at frame precision into phases of the same activity (tape, disk, net, sound, screen, burst, halt; input "
            "and paging mark a phase, never cut one); buckets are an optional overview. Reads only what the "
            "recording stores, no replay; facts the session cannot give are listed in missing[] with the reason. "
            "Reply: from_frame, to_frame, bucket_size, bucket_count, truncated, fps, devices[] {name, tags}, phases[] "
            "{frame_start, frame_end, tags[], facts..., devices[], busiest_frame}, buckets[] (buckets=true), events[] "
            "{frame, tinframe, kind, text}, missing[] {fact, reason}, cost {frames, port_records, ms}.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"from_frame", "integer", "First frame (default: the oldest held)"},
            {"to_frame", "integer", "Last frame, inclusive (default: the last complete frame)"},
            {"bucket_size", "integer", "Frames per overview bucket (default: the window over limit buckets)"},
            {"limit", "integer", "Most overview buckets, 1..500 (default 20); an explicit bucket_size cuts the window there"},
            {"buckets", "boolean", "Also return the overview buckets (default false: the phases only)"},
            {"ports", "boolean", "Classify the port records per device (default true)"},
            {"min_phase_ms", "integer", "The shortest phase in ms of machine time, also the window frames are tagged over (default 200)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }

    // =====================================================================
    // GET /ttd/cause-chain — where a value came from (the TTD cause chain)
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/cause-chain"]["get"];
        op["summary"] = "Where the value of a memory byte or a register came from (the cause chain)";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "Replays the frames it needs with the instruction and bus hooks and follows the value back: the "
            "instruction that wrote it, that instruction's inputs (registers, memory bytes) and theirs, as a tree "
            "bounded by depth and max_links. Kinds: store, load, copy, computed, constant, port, block, modify, "
            "return, interrupt, refresh, initial (held since the session start), rom, device (the byte read differs "
            "from the CPU's last write: something else wrote it), stop. The machine goes back where it stood; a live "
            "recording pauses for browsing. Reply: target {kind, name, addr, phys_page | reg, frame, tinframe}, "
            "links[] {id, parent, depth, kind, name, value, frame, tinframe, pc, code_page, text, inputs[], detail, "
            "stop}, stops, frames_traced, ms, text (text=true).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"addr", "string", "A memory byte: the Z80 address (decimal, 0x.., #..); addr or reg"},
            {"phys_page", "string", "addr: the RAM page behind it, 0..255 or rom (default: the page mapped at the moment)"},
            {"reg", "string", "A register: A F B C D E H L IXH IXL IYH IYL I R, a pair AF BC DE HL IX IY SP, or primed A' ... L', AF' ... HL'"},
            {"frame", "integer", "The moment's frame (default: the current position)"},
            {"tinframe", "integer", "The moment inside the frame, TTD time units (default 0)"},
            {"depth", "integer", "Links from the target to a leaf, 1..16 (default 4)"},
            {"max_links", "integer", "Links in the whole tree, 1..256 (default 32)"},
            {"max_frames", "integer", "Frames searched back for a register's producer, 1..1000 (default 50)"},
            {"flags", "boolean", "Follow F as an input (default false: listed, not followed)"},
            {"text", "boolean", "Also return the chain as indented text, one line per link (default false)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }

    // =====================================================================
    // GET /ttd/backtrace — the call stack at a moment
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/backtrace"]["get"];
        op["summary"] = "The call stack at a moment: the return addresses above SP and the CALL / RST / interrupt of each";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "Each stack word above SP is named by its last write before the moment (the write journal, else a pruned "
            "replay) and the instruction of that write (a replay of its frame): a CALL / RST that pushed a return "
            "address, an interrupt acknowledge that pushed the interrupted PC, or data (PUSH, locals). With the usual "
            "stack discipline the return addresses are the live frames, innermost first; a stack moved by LD SP shows "
            "the frames written where it now points. The walk stops at max_depth, max_slots, the top of memory, a word "
            "in ROM, a word not written in the history (before_session: the stack's base or frames older than the "
            "session) or a replay barrier. The machine goes back where it stood; a live recording pauses for "
            "browsing. Reply: at {frame, tinframe}, pc, code_page, sp, routine (the routine the PC is in), frames[] "
            "{depth, kind (call, rst, interrupt), slot, return_address, caller_pc, caller_page, text, frame, tinframe, "
            "callee, routine, data_words}, stop, stop_slot, slots_scanned, data_words, frames_traced, ms, text "
            "(text=true).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"frame", "integer", "The moment's frame (default: the current position)"},
            {"tinframe", "integer", "The moment inside the frame, TTD time units (default 0)"},
            {"max_depth", "integer", "Frames listed, 1..64 (default 16)"},
            {"max_slots", "integer", "Stack words looked at, 1..4096 (default 256)"},
            {"text", "boolean", "Also return the stack as text, one line per frame (default false)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }

    // =====================================================================
    // GET /ttd/find-event — the moment something happened (TTD find event)
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/find-event"]["get"];
        op["summary"] = "The first (or last) moments in a frame window where an event happened";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "One call for every 'when': kind mem (a byte or word meets cond: changed, increased, decreased, equals, "
            "becomes, differs, bit_set, bit_clear; frame boundaries, or every CPU write with exact=true), screen (cond "
            "change: at least min_changed % of the picture's bytes differ from the frame before; cleared: every pixel "
            "byte one value), exec (an address or range ran), port (IN / OUT by port, mask and value), border (the "
            "colour changed, or became value), key (a key pressed or released, from the input journal). Frames are "
            "filtered from what the session stores; only the hit frames are replayed, for the write or the "
            "instruction. The machine goes back where it stood; a live recording pauses for browsing. Reply: event "
            "{kind, cond, order, addr, phys_page}, hits[] {frame, tinframe, pc, code_page, text, before, after, "
            "device, why}, window {from_frame, to_frame}, more, next_from_frame | next_to_frame, barriers[], "
            "missing[], frames_scanned, frames_replayed, ms, text (text=true).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"kind", "string", "mem, screen, exec, port, border or key (required)"},
            {"cond", "string", "mem: changed (default), increased, decreased, equals, becomes, differs, bit_set, bit_clear; screen: change (default), cleared"},
            {"addr", "string", "mem / exec: the Z80 address (decimal, 0x.., #..)"},
            {"addr_to", "string", "exec: the range's last address (default addr)"},
            {"phys_page", "string", "mem / exec: the RAM page, 0..255 or rom (mem default: the page mapped at the current position; exec default: any)"},
            {"size", "integer", "mem: 1 or 2 bytes, little endian (default 1)"},
            {"value", "string", "mem: V of equals / becomes / differs; port: the value; border: the colour 0..7"},
            {"bit", "integer", "mem: B of bit_set / bit_clear"},
            {"exact", "boolean", "mem: every CPU write instead of frame boundaries (finds a change undone in the same frame)"},
            {"min_changed", "integer", "screen change: percent of the picture's bytes, 0..100 (default 50)"},
            {"dir", "string", "port: in or out (default out)"},
            {"port", "string", "port: the port number"},
            {"port_mask", "string", "port: the bits of port compared (default 0xFFFF)"},
            {"value_mask", "string", "port: the bits of value compared (default 0xFF)"},
            {"key", "string", "key: a ZX key name (SPACE, Q, CAPS, SYMBOL, ENTER, ...; default any key)"},
            {"pressed", "boolean", "key: presses (default true) or releases"},
            {"order", "string", "first (default: the oldest hits) or last (the newest)"},
            {"from_frame", "integer", "The window's first frame (default: the history's start)"},
            {"to_frame", "integer", "The window's last frame (default: the history's end)"},
            {"limit", "integer", "Hits, 1..100 (default 1); more + next_from_frame / next_to_frame continue"},
            {"max_frames", "integer", "exec without the coverage index: frames replayed at most (default 1000)"},
            {"text", "boolean", "Also return the hits as text, one line each and a summary (default false)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }

    // =====================================================================
    // GET /ttd/explain-frame — what one frame did (TTD explain frame)
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/explain-frame"]["get"];
        op["summary"] = "What one frame did: time split, routines, interrupt, paging, ports, memory, screen, input";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "One replay of the frame with the instruction and bus hooks answers: time (T in the interrupt routine, in "
            "HALT, in the rest), routines (from the call structure: CALL / RST / the interrupt acknowledge / RET; "
            "calls, inclusive and own T, instructions, the first caller; sorted by time), interrupts (when, IM, the "
            "vector, the routine, its T, where it returned), paging (bank changes with time and PC), ports (per "
            "direction and device: count, values, first time and PC; ULA reads with the keys seen down), memory (the "
            "bytes changed and written per range - the screen's pixels and attributes, the stack, RAM ranges - with "
            "their writers; changed bytes no CPU write explains are marked device), screen (% changed, the border "
            "colours in order), input (keys held and pressed / released) and markers. The machine goes back where it "
            "stood; a live recording pauses for browsing. Reply: frame, frame_t, end_t, partial, barrier, "
            "instructions, time {isr, halt, code}, routines[] {pc, code_page, isr, start, label, calls, t, own_t, "
            "insns, caller}, more_routines, interrupts_enabled, interrupts[] {tinframe, mode, vector, entry, t, "
            "returned, returned_to, before_frame}, paging[] {tinframe, pc, bank, page, was}, ports[] {dir, device, port, many_ports, count, "
            "tinframe, pc, values[], rows, keys[]}, memory[] {region, phys_page, offset, addr, length, changed, "
            "written, before, after, device, writers[] {pc, code_page, text, count}}, screen {bytes, changed, pixels, "
            "attributes, switched, border[] {colour, count}}, input {held[], events[] {tinframe, key, pressed}}, "
            "markers[], frames_replayed, ms, text (text=true).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"frame", "integer", "The frame (default: the current position's frame)"},
            {"sections", "string", "Comma list: time, routines, interrupt, paging, ports, memory, screen, input, markers (default all)"},
            {"limit", "integer", "Entries per list (routines, paging, ports, memory ranges, writers per range), 1..100 (default 8)"},
            {"text", "boolean", "Also return the answer as text, one block per section (default false)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }

    // =====================================================================
    // GET /ttd/replay-check — does the replay reproduce the recording
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/replay-check"]["get"];
        op["summary"] = "Replay each frame from its checkpoint and compare it with the next checkpoint";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "Every frame has a checkpoint at its start, taken from the live machine. Each frame of the range is "
            "replayed from its checkpoint to its end and the machine's whole state compared with the next checkpoint: "
            "CPU and chipset fields, the RAM per 16 KB page, every registered device's state, the engine's device "
            "regions. Frames with a barrier inside (an unrecorded change), a cut at their end (a snapshot load) or no "
            "next checkpoint are skipped and counted. The check stops after `limit` diverged frames. The machine goes "
            "back where it stood; a live recording pauses for browsing. Reply: from_frame, to_frame, checked, "
            "matched, diverged, skipped {barrier, cut, no_next_checkpoint}, stopped_after_frame, frames[] {frame, "
            "items[] {section (cpu, chipset, ram, device, region), name, recorded, replayed | bytes, differing, "
            "first_offset}, more_items}, ms, text (text=true).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"from_frame", "integer", "The first frame to check (default: the first kept frame)"},
            {"to_frame", "integer", "The last frame to check, inclusive (default: the last one with a next checkpoint)"},
            {"stride", "integer", "Check every n-th frame (default 1)"},
            {"limit", "integer", "Diverged frames to describe before the check stops, 1..200 (default 8)"},
            {"text", "boolean", "Also return the answer as text (default false)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }

    // =====================================================================
    // /ttd/seek-check — every seek replay-checks the frame it lands in
    // =====================================================================
    {
        const char* path = "/api/v1/emulator/{id}/ttd/seek-check";
        for (const char* method : {"get", "post"})
        {
            Json::Value& op = paths[path][method];
            op["tags"].append("Time-Travel Debug");
            op["parameters"][0]["name"] = "id";
            op["parameters"][0]["in"] = "path";
            op["parameters"][0]["required"] = true;
            op["parameters"][0]["schema"]["type"] = "string";
            op["responses"]["200"]["description"] =
                "enabled, seek_checks (seeks whose frame was checked), seek_check_divergences, last_seek_divergence "
                "(the last diverged check as text, null when none)";
            op["responses"]["404"]["description"] = "Emulator instance not found";
        }
        paths[path]["get"]["summary"] = "Whether seeks check their frame's replay, and the counts";
        paths[path]["post"]["summary"] = "Switch the seek check on or off";
        paths[path]["post"]["description"] =
            "Body: `{\"enabled\": true | false}`. On, every seek (any plane, the debugger's too) also runs the replay "
            "check (GET /ttd/replay-check) of the frame it lands in; the seek's answer then carries `replay_check` "
            "{diverged, text} and /ttd/status counts seek_checks and seek_check_divergences. A check costs one frame's "
            "replay. Off by default.";
        paths[path]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        paths[path]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["enabled"]["type"] =
            "boolean";
        paths[path]["post"]["responses"]["400"]["description"] = "The body has no boolean 'enabled'";
    }

    // =====================================================================
    // GET /ttd/diff — what differs between two moments (TTD diff of two moments)
    // =====================================================================
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ttd/diff"]["get"];
        op["summary"] = "What differs between two moments: registers, paging, memory with the last writes, screen, devices";
        op["tags"].append("Time-Travel Debug");
        op["description"] =
            "The machine is replayed to each moment (any T inside a frame) and its whole state taken there. The answer "
            "lists what differs: the registers, the port latches and the 8K slot mapping, the RAM changed in ranges "
            "(the screen's pixels and attributes one range each, the rest per page with gaps of 16 bytes or less; "
            "ranges of 8 bytes or less with their bytes at both moments) with the last CPU write in between (the "
            "write journal: time, PC, instruction), the screen and border, the devices whose state differs, and what "
            "happened in between (frames, T, keys held and pressed, markers, a replay barrier). The machine goes back "
            "where it stood; a live recording pauses for browsing. Reply: from {frame, tinframe}, to, backwards, "
            "registers[] {name, before, after}, latches[], slots[] {address, before, after, before_other_half, "
            "after_other_half}, memory {changed, pages, ranges[] {region, page, offset, address, length, changed, "
            "before[], after[], last_write {frame, tinframe, pc, address, text}, no_cpu_write}, more}, screen {bytes, "
            "pixels_changed, attributes_changed, switched, border_before, border_after}, devices {changed[] {name, "
            "bytes, changed}, more, same}, between {frames, t, keys_held[], keys[] {frame, tinframe, key, pressed}, "
            "more_keys, markers[], barrier}, writer_searches, ms, text (text=true).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        const struct
        {
            const char* name;
            const char* type;
            const char* description;
        } query[] = {
            {"from_frame", "integer", "The first moment's frame (required)"},
            {"from_tinframe", "integer", "T inside it (default 0)"},
            {"to_frame", "integer", "The second moment's frame (default: the current position)"},
            {"to_tinframe", "integer", "T inside it (default 0)"},
            {"sections", "string", "Comma list: cpu, paging, memory, screen, devices, between (default all)"},
            {"limit", "integer", "Entries per list (memory ranges, devices, keys), 1..200 (default 16)"},
            {"writers", "boolean", "The last write in between per memory range (default true)"},
            {"text", "boolean", "Also return the answer as text, one block per section (default false)"},
        };
        Json::ArrayIndex i = 1;
        for (const auto& q : query)
        {
            op["parameters"][i]["name"] = q.name;
            op["parameters"][i]["in"] = "query";
            op["parameters"][i]["schema"]["type"] = q.type;
            op["parameters"][i]["description"] = q.description;
            ++i;
        }
    }
