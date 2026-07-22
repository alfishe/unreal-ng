    // Time-Travel Debug endpoints (parent TDD §10.4 — full Phase 2 surface).
    //
    // All 10 endpoints exposed by core/automation/webapi/src/api/ttd_api.cpp.
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
        "if recording was already in progress.";
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
    paths["/api/v1/emulator/{id}/ttd/invalidate"]["post"]["responses"]["501"]["description"] =
        "TTD engine not available in this build";

    // =====================================================================
    // POST /ttd/seek — seek to (frame, tinframe)
    // =====================================================================
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["summary"] =
        "Seek to a point in the timeline (parent TDD §10.4)";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["description"] =
        "Restores the checkpoint on or before the target time, then replays "
        "forward. The session transitions to state=\"detached\" on success. "
        "Replay stops at the first external-event marker on or after the "
        "target — the response carries halt_reason=\"external_event\" and "
        "a blocking_marker payload describing the barrier. Use resume to "
        "continue past a barrier by truncating history at the current "
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
        "Missing required field: frame";
    paths["/api/v1/emulator/{id}/ttd/seek"]["post"]["responses"]["404"]["description"] =
        "Emulator instance not found";
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
        "it. Markers cover TapeControl, DiskWrite, DebuggerEdit, and Other "
        "(per ttd::TTDExternalEventKind).";
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
