    // Time-Travel Debug endpoints (parent TDD §10.4).
    //
    // Phase 2 surface (10 endpoints) + Phase 4 reverse-search (3 endpoints)
    // + Phase 4 reverse-execution (2 endpoints) + session restore via load
    // (1 endpoint) + TD-4 agent bookmarks (3 endpoints) = 19 endpoints total,
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
        "explicit recording, which holds the machine at real speed. v1 answers 400 for black_box true";
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
            "are left as they are; the session's last frame and frames holding a v1 marker without its data are not "
            "built. About 2-4 ms per frame, like RZX playback: the request answers when done; GET /ttd/journal shows "
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
        "to continue past an external-event marker that blocked a seek.";
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
        "schema core/src/debugger/ttd/engine/ttdsession.ksy); on v1 "
        "(UNREAL_TTD_BACKEND=v1) v1's format (ttd.ksy).";
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
            op["responses"]["501"]["description"] = "Backend v1 (no piece store)";
        }
        Json::Value& at = paths[kAt]["post"];
        at["summary"] = "Read a memory as it was at a past checkpoint, without seeking";
        at["description"] =
            "Bytes of any memory the session records (machine RAM, the Sprinter's video and fast RAM, sound card RAM and "
            "flash, VDAC2, EEPROMs ...) at the newest checkpoint at or before 'frame', from the engine's store: the "
            "machine does not move. at_frame names the checkpoint read, exact whether it is the frame asked for.";
        Json::Value& atBody = at["requestBody"]["content"]["application/json"]["schema"];
        atBody["properties"]["frame"]["type"] = "integer";
        atBody["properties"]["offset"]["type"] = "string";
        atBody["properties"]["offset"]["description"] = "Byte offset in the space (number, 0x.., #..), default 0";
        atBody["properties"]["length"]["type"] = "integer";
        atBody["properties"]["length"]["description"] = "Bytes, 1..65536, default 256 (clipped at the memory's end)";
        atBody["required"].append("frame");
        at["responses"]["200"]["description"] = "{space, offset, length, frame, at_frame, exact, hex}";
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
        "raw filters. Nothing is replayed: it answers the same on a loaded .ttd file. "
        "409 when the session has no port journal (TSConf, ZX Next, NeoGS, a file "
        "without it) or while a recording is running. With 'file' it searches a .ttd on "
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
        "Step back N instructions or T t-states (Phase 4 reverse execution)";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["description"] =
        "Multi-step backward jump. Exactly one of 'count' (instructions) or "
        "'tstates' (T-states) must be specified. For small N (<=4) delegates "
        "to repeated single-step; for larger N uses a single M1 enumeration "
        "pass. T-state mode aligns the landing to the nearest M1 boundary "
        "at or before the target T-state count.";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/reverse-step"]["post"]["requestBody"]["description"] =
        "Step size — exactly one of count or tstates";
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
