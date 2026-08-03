    schemas["EmulatorList"]["type"] = "object";
    schemas["EmulatorList"]["properties"]["emulators"]["type"] = "array";
    schemas["EmulatorList"]["properties"]["emulators"]["items"]["$ref"] = "#/components/schemas/EmulatorInfo";
    schemas["EmulatorList"]["properties"]["count"]["type"] = "integer";

    schemas["EmulatorInfo"]["type"] = "object";
    schemas["EmulatorInfo"]["properties"]["id"]["type"] = "string";
    schemas["EmulatorInfo"]["properties"]["id"]["description"] = "Emulator UUID";
    schemas["EmulatorInfo"]["properties"]["state"]["type"] = "string";
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("initialized");
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("running");
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("paused");
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("stopped");
    schemas["EmulatorInfo"]["properties"]["is_running"]["type"] = "boolean";
    schemas["EmulatorInfo"]["properties"]["is_paused"]["type"] = "boolean";
    schemas["EmulatorInfo"]["properties"]["is_debug"]["type"] = "boolean";

    schemas["CreateEmulatorRequest"]["type"] = "object";
    schemas["CreateEmulatorRequest"]["properties"]["model"]["type"] = "string";
    schemas["CreateEmulatorRequest"]["properties"]["model"]["description"] = "Emulator model (e.g., ZX48, ZX128)";

    // Settings Management schemas
    schemas["SettingsResponse"]["type"] = "object";
    schemas["SettingsResponse"]["description"] = "List of emulator settings";
    schemas["SettingsResponse"]["properties"]["settings"]["type"] = "object";
    schemas["SettingsResponse"]["properties"]["settings"]["additionalProperties"]["type"] = "string";

    // Feature Management schemas
    schemas["FeaturesResponse"]["type"] = "object";
    schemas["FeaturesResponse"]["description"] = "List of all features";
    schemas["FeaturesResponse"]["properties"]["emulator_id"]["type"] = "string";
    schemas["FeaturesResponse"]["properties"]["features"]["type"] = "array";
    schemas["FeaturesResponse"]["properties"]["features"]["items"]["$ref"] = "#/components/schemas/FeatureInfo";

    schemas["FeatureInfo"]["type"] = "object";
    schemas["FeatureInfo"]["description"] = "Feature information";
    schemas["FeatureInfo"]["properties"]["id"]["type"] = "string";
    schemas["FeatureInfo"]["properties"]["enabled"]["type"] = "boolean";
    schemas["FeatureInfo"]["properties"]["description"]["type"] = "string";
    schemas["FeatureInfo"]["properties"]["mode"]["type"] = "string";

    // Debug Commands schemas

    // Breakpoints List Response
    schemas["BreakpointsListResponse"]["type"] = "object";
    schemas["BreakpointsListResponse"]["description"] = "List of breakpoints";
    schemas["BreakpointsListResponse"]["properties"]["count"]["type"] = "integer";
    schemas["BreakpointsListResponse"]["properties"]["breakpoints"]["type"] = "array";
    schemas["BreakpointsListResponse"]["properties"]["breakpoints"]["items"]["$ref"] =
        "#/components/schemas/BreakpointInfo";

    // Memory Breakpoint Info
    schemas["MemoryBreakpointInfo"]["type"] = "object";
    schemas["MemoryBreakpointInfo"]["description"] = "Memory breakpoint (execute/read/write)";
    schemas["MemoryBreakpointInfo"]["properties"]["id"]["type"] = "integer";
    schemas["MemoryBreakpointInfo"]["properties"]["type"]["type"] = "string";
    schemas["MemoryBreakpointInfo"]["properties"]["type"]["enum"].append("memory");
    schemas["MemoryBreakpointInfo"]["properties"]["address"]["type"] = "integer";
    schemas["MemoryBreakpointInfo"]["properties"]["execute"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["read"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["write"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["active"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["note"]["type"] = "string";
    schemas["MemoryBreakpointInfo"]["properties"]["group"]["type"] = "string";

    // Port Breakpoint Info
    schemas["PortBreakpointInfo"]["type"] = "object";
    schemas["PortBreakpointInfo"]["description"] = "Port breakpoint (in/out)";
    schemas["PortBreakpointInfo"]["properties"]["id"]["type"] = "integer";
    schemas["PortBreakpointInfo"]["properties"]["type"]["type"] = "string";
    schemas["PortBreakpointInfo"]["properties"]["type"]["enum"].append("port");
    schemas["PortBreakpointInfo"]["properties"]["address"]["type"] = "integer";
    schemas["PortBreakpointInfo"]["properties"]["address"]["description"] = "Port number";
    schemas["PortBreakpointInfo"]["properties"]["in"]["type"] = "boolean";
    schemas["PortBreakpointInfo"]["properties"]["out"]["type"] = "boolean";
    schemas["PortBreakpointInfo"]["properties"]["active"]["type"] = "boolean";
    schemas["PortBreakpointInfo"]["properties"]["note"]["type"] = "string";
    schemas["PortBreakpointInfo"]["properties"]["group"]["type"] = "string";

    // Keyboard Breakpoint Info
    schemas["KeyboardBreakpointInfo"]["type"] = "object";
    schemas["KeyboardBreakpointInfo"]["description"] = "Keyboard breakpoint (press/release)";
    schemas["KeyboardBreakpointInfo"]["properties"]["id"]["type"] = "integer";
    schemas["KeyboardBreakpointInfo"]["properties"]["type"]["type"] = "string";
    schemas["KeyboardBreakpointInfo"]["properties"]["type"]["enum"].append("keyboard");
    schemas["KeyboardBreakpointInfo"]["properties"]["address"]["type"] = "integer";
    schemas["KeyboardBreakpointInfo"]["properties"]["press"]["type"] = "boolean";
    schemas["KeyboardBreakpointInfo"]["properties"]["release"]["type"] = "boolean";
    schemas["KeyboardBreakpointInfo"]["properties"]["active"]["type"] = "boolean";
    schemas["KeyboardBreakpointInfo"]["properties"]["note"]["type"] = "string";
    schemas["KeyboardBreakpointInfo"]["properties"]["group"]["type"] = "string";

    // Generic BreakpointInfo (oneOf the above)
    schemas["BreakpointInfo"]["oneOf"][0]["$ref"] = "#/components/schemas/MemoryBreakpointInfo";
    schemas["BreakpointInfo"]["oneOf"][1]["$ref"] = "#/components/schemas/PortBreakpointInfo";
    schemas["BreakpointInfo"]["oneOf"][2]["$ref"] = "#/components/schemas/KeyboardBreakpointInfo";
    schemas["BreakpointInfo"]["discriminator"]["propertyName"] = "type";
    schemas["BreakpointInfo"]["discriminator"]["mapping"]["memory"] = "#/components/schemas/MemoryBreakpointInfo";
    schemas["BreakpointInfo"]["discriminator"]["mapping"]["port"] = "#/components/schemas/PortBreakpointInfo";
    schemas["BreakpointInfo"]["discriminator"]["mapping"]["keyboard"] = "#/components/schemas/KeyboardBreakpointInfo";

    // Breakpoint Status Response (last triggered)
    schemas["BreakpointStatusResponse"]["type"] = "object";
    schemas["BreakpointStatusResponse"]["description"] = "Last triggered breakpoint information";
    schemas["BreakpointStatusResponse"]["properties"]["is_paused"]["type"] = "boolean";
    schemas["BreakpointStatusResponse"]["properties"]["breakpoints_count"]["type"] = "integer";
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_id"]["type"] = "integer";
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_id"]["nullable"] = true;
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["type"] = "string";
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["enum"].append("memory");
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["enum"].append("port");
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["enum"].append("keyboard");
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_address"]["type"] = "integer";
    schemas["BreakpointStatusResponse"]["properties"]["paused_by_breakpoint"]["type"] = "boolean";

    // Add Breakpoint Request
    schemas["AddBreakpointRequest"]["type"] = "object";
    schemas["AddBreakpointRequest"]["description"] = "Request to add a breakpoint";
    schemas["AddBreakpointRequest"]["required"].append("type");
    schemas["AddBreakpointRequest"]["required"].append("address");
    schemas["AddBreakpointRequest"]["properties"]["type"]["type"] = "string";
    schemas["AddBreakpointRequest"]["properties"]["type"]["description"] = "Breakpoint type";
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("execution");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("read");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("write");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("port_in");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("port_out");
    schemas["AddBreakpointRequest"]["properties"]["address"]["type"] = "integer";
    schemas["AddBreakpointRequest"]["properties"]["address"]["description"] =
        "Z80 address (0-65535) or port number (0-255)";
    schemas["AddBreakpointRequest"]["properties"]["note"]["type"] = "string";
    schemas["AddBreakpointRequest"]["properties"]["note"]["description"] = "Optional annotation";
    schemas["AddBreakpointRequest"]["properties"]["group"]["type"] = "string";
    schemas["AddBreakpointRequest"]["properties"]["group"]["description"] = "Optional group name";

    // Add Breakpoint Response
    schemas["AddBreakpointResponse"]["type"] = "object";
    schemas["AddBreakpointResponse"]["description"] = "Response after adding a breakpoint";
    schemas["AddBreakpointResponse"]["properties"]["status"]["type"] = "string";
    schemas["AddBreakpointResponse"]["properties"]["id"]["type"] = "integer";
    schemas["AddBreakpointResponse"]["properties"]["id"]["description"] = "Assigned breakpoint ID";
    schemas["AddBreakpointResponse"]["properties"]["type"]["type"] = "string";
    schemas["AddBreakpointResponse"]["properties"]["address"]["type"] = "integer";
    schemas["AddBreakpointResponse"]["properties"]["message"]["type"] = "string";

    // Memory State schemas
    schemas["MemoryStateResponse"]["type"] = "object";
    schemas["MemoryStateResponse"]["description"] = "Memory state overview";
    schemas["MemoryStateResponse"]["properties"]["total_ram"]["type"] = "integer";
    schemas["MemoryStateResponse"]["properties"]["total_rom"]["type"] = "integer";

    schemas["RAMStateResponse"]["type"] = "object";
    schemas["RAMStateResponse"]["description"] = "RAM state details";
    schemas["RAMStateResponse"]["properties"]["size"]["type"] = "integer";
    schemas["RAMStateResponse"]["properties"]["banks"]["type"] = "array";

    schemas["ROMStateResponse"]["type"] = "object";
    schemas["ROMStateResponse"]["description"] = "ROM state details";
    schemas["ROMStateResponse"]["properties"]["size"]["type"] = "integer";
    schemas["ROMStateResponse"]["properties"]["type"]["type"] = "string";

    // Screen State schemas
    schemas["ScreenStateResponse"]["type"] = "object";
    schemas["ScreenStateResponse"]["description"] = "Screen state overview";
    schemas["ScreenStateResponse"]["properties"]["mode"]["type"] = "string";
    schemas["ScreenStateResponse"]["properties"]["flash_enabled"]["type"] = "boolean";

    schemas["ScreenModeResponse"]["type"] = "object";
    schemas["ScreenModeResponse"]["description"] = "Screen mode information";
    schemas["ScreenModeResponse"]["properties"]["mode"]["type"] = "string";
    schemas["ScreenModeResponse"]["properties"]["resolution"]["type"] = "string";

    schemas["FlashStateResponse"]["type"] = "object";
    schemas["FlashStateResponse"]["description"] = "Flash state information";
    schemas["FlashStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["FlashStateResponse"]["properties"]["phase"]["type"] = "integer";

    // Audio State schemas
    schemas["AYChipsResponse"]["type"] = "object";
    schemas["AYChipsResponse"]["description"] = "AY chips overview";
    schemas["AYChipsResponse"]["properties"]["chip_count"]["type"] = "integer";
    schemas["AYChipsResponse"]["properties"]["chips"]["type"] = "array";

    schemas["AYChipResponse"]["type"] = "object";
    schemas["AYChipResponse"]["description"] = "AY chip details";
    schemas["AYChipResponse"]["properties"]["chip_index"]["type"] = "integer";
    schemas["AYChipResponse"]["properties"]["registers"]["type"] = "array";

    schemas["AYRegisterResponse"]["type"] = "object";
    schemas["AYRegisterResponse"]["description"] = "AY chip register details";
    schemas["AYRegisterResponse"]["properties"]["register"]["type"] = "integer";
    schemas["AYRegisterResponse"]["properties"]["value"]["type"] = "integer";

    schemas["BeeperStateResponse"]["type"] = "object";
    schemas["BeeperStateResponse"]["description"] = "Beeper state";
    schemas["BeeperStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["BeeperStateResponse"]["properties"]["value"]["type"] = "integer";

    schemas["GSStateResponse"]["type"] = "object";
    schemas["GSStateResponse"]["description"] = "General Sound state";
    schemas["GSStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["GSStateResponse"]["properties"]["channels"]["type"] = "integer";

    schemas["CovoxStateResponse"]["type"] = "object";
    schemas["CovoxStateResponse"]["description"] = "Covox state";
    schemas["CovoxStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["CovoxStateResponse"]["properties"]["value"]["type"] = "integer";

    schemas["AudioChannelsResponse"]["type"] = "object";
    schemas["AudioChannelsResponse"]["description"] = "Audio channels overview";
    schemas["AudioChannelsResponse"]["properties"]["channel_count"]["type"] = "integer";
    schemas["AudioChannelsResponse"]["properties"]["channels"]["type"] = "array";

    // Tape/Disk/Snapshot schemas
    schemas["TapeInfoResponse"]["type"] = "object";
    schemas["TapeInfoResponse"]["description"] = "Tape status information";
    schemas["TapeInfoResponse"]["properties"]["loaded"]["type"] = "boolean";
    schemas["TapeInfoResponse"]["properties"]["playing"]["type"] = "boolean";

    schemas["DiskInfoResponse"]["type"] = "object";
    schemas["DiskInfoResponse"]["description"] = "Disk drive status";
    schemas["DiskInfoResponse"]["properties"]["inserted"]["type"] = "boolean";
    schemas["DiskInfoResponse"]["properties"]["write_protected"]["type"] = "boolean";

    schemas["SnapshotInfoResponse"]["type"] = "object";
    schemas["SnapshotInfoResponse"]["description"] = "Snapshot status";
    schemas["SnapshotInfoResponse"]["properties"]["loaded"]["type"] = "boolean";
    schemas["SnapshotInfoResponse"]["properties"]["filename"]["type"] = "string";

    // Opcode Profiler schemas
    schemas["ProfilerStatusResponse"]["type"] = "object";
    schemas["ProfilerStatusResponse"]["description"] = "Opcode profiler status";
    schemas["ProfilerStatusResponse"]["properties"]["emulator_id"]["type"] = "string";
    schemas["ProfilerStatusResponse"]["properties"]["capturing"]["type"] = "boolean";
    schemas["ProfilerStatusResponse"]["properties"]["total_executions"]["type"] = "integer";
    schemas["ProfilerStatusResponse"]["properties"]["trace_size"]["type"] = "integer";
    schemas["ProfilerStatusResponse"]["properties"]["trace_capacity"]["type"] = "integer";
    schemas["ProfilerStatusResponse"]["properties"]["feature_enabled"]["type"] = "boolean";

    schemas["ProfilerCountersResponse"]["type"] = "object";
    schemas["ProfilerCountersResponse"]["description"] = "Opcode execution counters";
    schemas["ProfilerCountersResponse"]["properties"]["emulator_id"]["type"] = "string";
    schemas["ProfilerCountersResponse"]["properties"]["total_executions"]["type"] = "integer";
    schemas["ProfilerCountersResponse"]["properties"]["limit"]["type"] = "integer";
    schemas["ProfilerCountersResponse"]["properties"]["count"]["type"] = "integer";
    schemas["ProfilerCountersResponse"]["properties"]["counters"]["type"] = "array";
    schemas["ProfilerCountersResponse"]["properties"]["counters"]["items"]["$ref"] =
        "#/components/schemas/OpcodeCounter";

    schemas["OpcodeCounter"]["type"] = "object";
    schemas["OpcodeCounter"]["description"] = "Single opcode counter entry";
    schemas["OpcodeCounter"]["properties"]["prefix"]["type"] = "integer";
    schemas["OpcodeCounter"]["properties"]["prefix"]["description"] =
        "Prefix code (0=none, 0xCB, 0xDD, 0xED, 0xFD, 0xDDCB, 0xFDCB)";
    schemas["OpcodeCounter"]["properties"]["prefix_name"]["type"] = "string";
    schemas["OpcodeCounter"]["properties"]["opcode"]["type"] = "integer";
    schemas["OpcodeCounter"]["properties"]["count"]["type"] = "integer";

    schemas["ProfilerTraceResponse"]["type"] = "object";
    schemas["ProfilerTraceResponse"]["description"] = "Opcode execution trace";
    schemas["ProfilerTraceResponse"]["properties"]["emulator_id"]["type"] = "string";
    schemas["ProfilerTraceResponse"]["properties"]["trace_size"]["type"] = "integer";
    schemas["ProfilerTraceResponse"]["properties"]["requested_count"]["type"] = "integer";
    schemas["ProfilerTraceResponse"]["properties"]["returned_count"]["type"] = "integer";
    schemas["ProfilerTraceResponse"]["properties"]["trace"]["type"] = "array";
    schemas["ProfilerTraceResponse"]["properties"]["trace"]["items"]["$ref"] = "#/components/schemas/TraceEntry";

    schemas["TraceEntry"]["type"] = "object";
    schemas["TraceEntry"]["description"] = "Single trace entry";
    schemas["TraceEntry"]["properties"]["pc"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["prefix"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["opcode"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["flags"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["a"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["frame"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["tstate"]["type"] = "integer";

    // TTD (Time-Travel Debug) schemas — parent TDD §10.4.
    // Full Phase 2 surface: 10 endpoints across recording, seeking, stepping,
    // resume, and marker inspection. All time points use the TTDTimePoint
    // schema; responses that surface the session lifecycle state reuse the
    // state enum (idle|recording|detached).

    // Reusable time point — frame counter + intra-frame T-state offset.
    schemas["TTDTimePoint"]["type"] = "object";
    schemas["TTDTimePoint"]["description"] =
        "Lexicographic (frame, tInFrame) pair identifying a position in the "
        "recorded timeline. tInFrame is the T-state offset within the frame.";
    schemas["TTDTimePoint"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDTimePoint"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDTimePoint"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDTimePoint"]["properties"]["tinframe"]["format"] = "uint32";
    schemas["TTDStatusResponse"]["type"] = "object";
    schemas["TTDStatusResponse"]["description"] =
        "Snapshot of the TTD recorder session for the requested emulator instance";
    schemas["TTDStatusResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDStatusResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDStatusResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDStatusResponse"]["properties"]["state"]["enum"].append("detached");
    schemas["TTDStatusResponse"]["properties"]["state"]["description"] =
        "Session lifecycle state (parent TDD §4.2)";
    schemas["TTDStatusResponse"]["properties"]["session_start_frame"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["session_start_frame"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["current_end_frame"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["current_end_frame"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["checkpoint_count"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["checkpoint_count"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["page_store_bytes"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["page_store_bytes"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["page_store_bytes"]["description"] =
        "Capacity (allocated) of the COW page store — for budget checks";
    schemas["TTDStatusResponse"]["properties"]["page_store_used_bytes"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["page_store_used_bytes"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["page_store_used_bytes"]["description"] =
        "Live slot bytes currently held by the COW page store";
    schemas["TTDStatusResponse"]["properties"]["baseline_frames_captured"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["baseline_frames_captured"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["baseline_frames_captured"]["description"] =
            "Count of distinct 16 KB RAM page snapshots currently held in the "
            "COW page store (live slots). Grows when new pages are captured, "
            "shrinks when old pages are released via session invalidation or "
            "future thinning. Multiply by 16384 to compare against "
            "page_store_used_bytes.";
    schemas["TTDStatusResponse"]["properties"]["session_heap_bytes"]["type"] = "integer";
    schemas["TTDStatusResponse"]["properties"]["session_heap_bytes"]["format"] = "uint64";
    schemas["TTDStatusResponse"]["properties"]["session_heap_bytes"]["description"] =
            "Real heap footprint of the recorded session in bytes. Sums every "
            "allocation the session owns: page store backing vector, "
            "per-checkpoint metadata (CPU/chipset snapshot + page-ref vector + "
            "peripheral blobs), input journal, external-event journal, and "
            "session-scope dirty-page scratch. This is the number to display "
            "for 'session size' — distinct from page_store_bytes (just the "
            "page-store vector) and page_store_used_bytes (live slots only, "
            "always ~100% of capacity because the COW store auto-grows).";
    schemas["TTDStatusResponse"]["properties"]["ttd_available"]["type"] = "boolean";
    schemas["TTDStatusResponse"]["properties"]["ttd_available"]["description"] =
        "False when the manager is missing (minimal build); state will be \"idle\"";

    // ---------------------------------------------------------------------
    // POST /ttd/start response
    // ---------------------------------------------------------------------
    schemas["TTDStartResponse"]["type"] = "object";
    schemas["TTDStartResponse"]["description"] =
        "Response from POST /ttd/start. started=false when already recording; "
        "already_active=true if recording was in progress before the call.";
    schemas["TTDStartResponse"]["properties"]["started"]["type"] = "boolean";
    schemas["TTDStartResponse"]["properties"]["already_active"]["type"] = "boolean";
    schemas["TTDStartResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDStartResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDStartResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDStartResponse"]["properties"]["state"]["enum"].append("detached");
    schemas["TTDStartResponse"]["properties"]["write_journal_enabled"]["type"] = "boolean";
    schemas["TTDStartResponse"]["properties"]["write_journal_enabled"]["description"] =
        "Whether write journal capture is enabled. false = gaming mode (smaller files), "
        "true = development mode (fast reverse-watchpoint queries).";

    // ---------------------------------------------------------------------
    // POST /ttd/stop response
    // ---------------------------------------------------------------------
    schemas["TTDStopResponse"]["type"] = "object";
    schemas["TTDStopResponse"]["description"] =
        "Response from POST /ttd/stop. stopped=false if recording was not "
        "active; the session still transitions to detached when history exists.";
    schemas["TTDStopResponse"]["properties"]["stopped"]["type"] = "boolean";
    schemas["TTDStopResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDStopResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDStopResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDStopResponse"]["properties"]["state"]["enum"].append("detached");

    // ---------------------------------------------------------------------
    // POST /ttd/invalidate request + response
    // ---------------------------------------------------------------------
    schemas["TTDInvalidateRequest"]["type"] = "object";
    schemas["TTDInvalidateRequest"]["description"] =
        "Optional request body for POST /ttd/invalidate. Carries a "
        "human-readable reason that is logged and surfaced in subsequent "
        "status queries. Entirely optional — defaults to "
        "\"WebAPI invalidate\".";
    schemas["TTDInvalidateRequest"]["properties"]["reason"]["type"] = "string";

    schemas["TTDInvalidateResponse"]["type"] = "object";
    schemas["TTDInvalidateResponse"]["description"] =
        "Response from POST /ttd/invalidate. invalidated is always true on "
        "200; the session returns to state=\"idle\".";
    schemas["TTDInvalidateResponse"]["properties"]["invalidated"]["type"] = "boolean";
    schemas["TTDInvalidateResponse"]["properties"]["reason"]["type"] = "string";
    schemas["TTDInvalidateResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDInvalidateResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDInvalidateResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDInvalidateResponse"]["properties"]["state"]["enum"].append("detached");

    // ---------------------------------------------------------------------
    // POST /ttd/seek request + response
    // ---------------------------------------------------------------------
    schemas["TTDSeekRequest"]["type"] = "object";
    schemas["TTDSeekRequest"]["description"] =
        "Request body for POST /ttd/seek. frame is required; tinframe "
        "defaults to 0 (frame boundary).";
    schemas["TTDSeekRequest"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDSeekRequest"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDSeekRequest"]["required"].append("frame");
    schemas["TTDSeekRequest"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDSeekRequest"]["properties"]["tinframe"]["format"] = "uint32";
    schemas["TTDSeekRequest"]["properties"]["tinframe"]["default"] = 0;

    schemas["TTDSeekResponse"]["type"] = "object";
    schemas["TTDSeekResponse"]["description"] =
        "Response from POST /ttd/seek. reached=true only when the engine "
        "arrived at the requested target. halt_reason explains why the "
        "engine halted where it did: target (success), external_event "
        "(stopped at a replay barrier — see blocking_marker), out_of_range "
        "(target lies outside the recorded timeline).";
    schemas["TTDSeekResponse"]["properties"]["reached"]["type"] = "boolean";
    schemas["TTDSeekResponse"]["properties"]["arrived_at"]["$ref"] =
        "#/components/schemas/TTDTimePoint";
    schemas["TTDSeekResponse"]["properties"]["halt_reason"]["type"] = "string";
    schemas["TTDSeekResponse"]["properties"]["halt_reason"]["enum"].append("target");
    schemas["TTDSeekResponse"]["properties"]["halt_reason"]["enum"].append("external_event");
    schemas["TTDSeekResponse"]["properties"]["halt_reason"]["enum"].append("out_of_range");
    schemas["TTDSeekResponse"]["properties"]["blocking_marker"]["$ref"] =
        "#/components/schemas/TTDMarker";
    schemas["TTDSeekResponse"]["properties"]["blocking_marker"]["description"] =
        "Present only when halt_reason == external_event";
    schemas["TTDSeekResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDSeekResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDSeekResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDSeekResponse"]["properties"]["state"]["enum"].append("detached");

    // ---------------------------------------------------------------------
    // Reusable marker — external event that acts as a replay barrier.
    // Surfaced in TTDSeekResponse.blocking_marker and TTDMarkersResponse.
    // ---------------------------------------------------------------------
    schemas["TTDMarker"]["type"] = "object";
    schemas["TTDMarker"]["description"] =
        "External-event marker (replay barrier). Per ttd::TTDExternalEventKind "
        "the kind is one of: tape_control, disk_write, debugger_edit, "
        "hardware_reset, other.";
    schemas["TTDMarker"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDMarker"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDMarker"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDMarker"]["properties"]["tinframe"]["format"] = "uint32";
    schemas["TTDMarker"]["properties"]["kind"]["type"] = "string";
    schemas["TTDMarker"]["properties"]["kind"]["enum"].append("tape_control");
    schemas["TTDMarker"]["properties"]["kind"]["enum"].append("disk_write");
    schemas["TTDMarker"]["properties"]["kind"]["enum"].append("debugger_edit");
    schemas["TTDMarker"]["properties"]["kind"]["enum"].append("hardware_reset");
    schemas["TTDMarker"]["properties"]["kind"]["enum"].append("other");
    schemas["TTDMarker"]["properties"]["reason"]["type"] = "string";

    // ---------------------------------------------------------------------
    // POST /ttd/step-back and /ttd/step-forward share this response.
    // ---------------------------------------------------------------------
    schemas["TTDStepResponse"]["type"] = "object";
    schemas["TTDStepResponse"]["description"] =
        "Response from POST /ttd/step-back and /ttd/step-forward. stepped=false "
        "when the step would move outside the recorded timeline; frame and "
        "tinframe always reflect the current position after the attempt.";
    schemas["TTDStepResponse"]["properties"]["stepped"]["type"] = "boolean";
    schemas["TTDStepResponse"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDStepResponse"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDStepResponse"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDStepResponse"]["properties"]["tinframe"]["format"] = "uint32";

    // ---------------------------------------------------------------------
    // POST /ttd/resume request + response
    // ---------------------------------------------------------------------
    schemas["TTDResumeRequest"]["type"] = "object";
    schemas["TTDResumeRequest"]["description"] =
        "Optional request body for POST /ttd/resume. Omit to resume from "
        "the current position; include frame (and optionally tinframe) to "
        "resume from a specific time point.";
    schemas["TTDResumeRequest"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDResumeRequest"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDResumeRequest"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDResumeRequest"]["properties"]["tinframe"]["format"] = "uint32";

    schemas["TTDResumeResponse"]["type"] = "object";
    schemas["TTDResumeResponse"]["description"] =
        "Response from POST /ttd/resume. resumed=true on success. frame "
        "and tinframe echo the effective resume point.";
    schemas["TTDResumeResponse"]["properties"]["resumed"]["type"] = "boolean";
    schemas["TTDResumeResponse"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDResumeResponse"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDResumeResponse"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDResumeResponse"]["properties"]["tinframe"]["format"] = "uint32";
    schemas["TTDResumeResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDResumeResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDResumeResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDResumeResponse"]["properties"]["state"]["enum"].append("detached");

    // ---------------------------------------------------------------------
    // GET /ttd/position response
    // ---------------------------------------------------------------------
    schemas["TTDPositionResponse"]["type"] = "object";
    schemas["TTDPositionResponse"]["description"] =
        "Response from GET /ttd/position. current is where the engine is "
        "now; session_end is the maximum reachable point in the timeline.";
    schemas["TTDPositionResponse"]["properties"]["current"]["$ref"] =
        "#/components/schemas/TTDTimePoint";
    schemas["TTDPositionResponse"]["properties"]["session_end"]["$ref"] =
        "#/components/schemas/TTDTimePoint";
    schemas["TTDPositionResponse"]["properties"]["state"]["type"] = "string";
    schemas["TTDPositionResponse"]["properties"]["state"]["enum"].append("idle");
    schemas["TTDPositionResponse"]["properties"]["state"]["enum"].append("recording");
    schemas["TTDPositionResponse"]["properties"]["state"]["enum"].append("detached");

    // ---------------------------------------------------------------------
    // GET /ttd/markers response
    // ---------------------------------------------------------------------
    schemas["TTDMarkersResponse"]["type"] = "object";
    schemas["TTDMarkersResponse"]["description"] =
        "Response from GET /ttd/markers. count is len(markers); markers is "
        "the full journal of external events recorded in this session.";
    schemas["TTDMarkersResponse"]["properties"]["count"]["type"] = "integer";
    schemas["TTDMarkersResponse"]["properties"]["count"]["format"] = "uint64";
    schemas["TTDMarkersResponse"]["properties"]["markers"]["type"] = "array";
    schemas["TTDMarkersResponse"]["properties"]["markers"]["items"]["$ref"] =
        "#/components/schemas/TTDMarker";

    // ---------------------------------------------------------------------
    // Phase 4 — reverse-search + dump + instruction-step schemas
    // ---------------------------------------------------------------------

    // POST /ttd/dump
    schemas["TTDDumpRequest"]["type"] = "object";
    schemas["TTDDumpRequest"]["description"] =
        "Request body for POST /ttd/dump.";
    schemas["TTDDumpRequest"]["properties"]["path"]["type"] = "string";
    schemas["TTDDumpRequest"]["properties"]["path"]["description"] =
        "Filesystem path where the .ttd file will be written.";
    schemas["TTDDumpRequest"]["required"].append("path");

    schemas["TTDDumpResponse"]["type"] = "object";
    schemas["TTDDumpResponse"]["description"] =
        "Response from POST /ttd/dump.";
    schemas["TTDDumpResponse"]["properties"]["ok"]["type"] = "boolean";
    schemas["TTDDumpResponse"]["properties"]["path"]["type"] = "string";
    schemas["TTDDumpResponse"]["properties"]["bytes"]["type"] = "integer";
    schemas["TTDDumpResponse"]["properties"]["bytes"]["format"] = "int64";
    schemas["TTDDumpResponse"]["properties"]["error"]["type"] = "string";

    // POST /ttd/find-last
    schemas["TTDFindLastRequest"]["type"] = "object";
    schemas["TTDFindLastRequest"]["description"] =
        "Request body for POST /ttd/find-last.";
    schemas["TTDFindLastRequest"]["properties"]["addr"]["type"] = "integer";
    schemas["TTDFindLastRequest"]["properties"]["addr"]["format"] = "uint16";
    schemas["TTDFindLastRequest"]["properties"]["addr"]["description"] =
        "Memory address to search for (required).";
    schemas["TTDFindLastRequest"]["required"].append("addr");
    schemas["TTDFindLastRequest"]["properties"]["access"]["type"] = "string";
    schemas["TTDFindLastRequest"]["properties"]["access"]["enum"].append("write");
    schemas["TTDFindLastRequest"]["properties"]["access"]["enum"].append("read");
    schemas["TTDFindLastRequest"]["properties"]["access"]["enum"].append("execute");
    schemas["TTDFindLastRequest"]["properties"]["access"]["enum"].append("io");
    schemas["TTDFindLastRequest"]["properties"]["access"]["default"] = "write";
    schemas["TTDFindLastRequest"]["properties"]["value"]["type"] = "integer";
    schemas["TTDFindLastRequest"]["properties"]["value"]["format"] = "uint8";
    schemas["TTDFindLastRequest"]["properties"]["pc_from"]["type"] = "integer";
    schemas["TTDFindLastRequest"]["properties"]["pc_from"]["format"] = "uint16";
    schemas["TTDFindLastRequest"]["properties"]["pc_to"]["type"] = "integer";
    schemas["TTDFindLastRequest"]["properties"]["pc_to"]["format"] = "uint16";
    schemas["TTDFindLastRequest"]["properties"]["before_frame"]["type"] = "integer";
    schemas["TTDFindLastRequest"]["properties"]["before_frame"]["format"] = "uint64";
    schemas["TTDFindLastRequest"]["properties"]["before_tin"]["type"] = "integer";
    schemas["TTDFindLastRequest"]["properties"]["before_tin"]["format"] = "uint32";

    schemas["TTDFindLastResponse"]["type"] = "object";
    schemas["TTDFindLastResponse"]["description"] =
        "Response from POST /ttd/find-last.";
    schemas["TTDFindLastResponse"]["properties"]["found"]["type"] = "boolean";
    schemas["TTDFindLastResponse"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDFindLastResponse"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["tinframe"]["format"] = "uint32";
    schemas["TTDFindLastResponse"]["properties"]["pc"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["pc"]["format"] = "uint16";
    schemas["TTDFindLastResponse"]["properties"]["value"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["value"]["format"] = "uint8";
    schemas["TTDFindLastResponse"]["properties"]["phys_page"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["phys_page"]["format"] = "uint16";
    schemas["TTDFindLastResponse"]["properties"]["access"]["type"] = "string";
    schemas["TTDFindLastResponse"]["properties"]["blocked"]["type"] = "boolean";
    schemas["TTDFindLastResponse"]["properties"]["marker_frame"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["marker_frame"]["format"] = "uint64";
    schemas["TTDFindLastResponse"]["properties"]["marker_tinframe"]["type"] = "integer";
    schemas["TTDFindLastResponse"]["properties"]["marker_tinframe"]["format"] = "uint32";
    schemas["TTDFindLastResponse"]["properties"]["marker_kind"]["type"] = "string";
    schemas["TTDFindLastResponse"]["properties"]["marker_reason"]["type"] = "string";

    // POST /ttd/step-instruction
    schemas["TTDStepInstructionRequest"]["type"] = "object";
    schemas["TTDStepInstructionRequest"]["description"] =
        "Request body for POST /ttd/step-instruction.";
    schemas["TTDStepInstructionRequest"]["properties"]["dir"]["type"] = "string";
    schemas["TTDStepInstructionRequest"]["properties"]["dir"]["enum"].append("back");
    schemas["TTDStepInstructionRequest"]["properties"]["dir"]["enum"].append("forward");
    schemas["TTDStepInstructionRequest"]["properties"]["dir"]["enum"].append("fwd");
    schemas["TTDStepInstructionRequest"]["properties"]["dir"]["default"] = "back";

    schemas["TTDStepInstructionResponse"]["type"] = "object";
    schemas["TTDStepInstructionResponse"]["description"] =
        "Response from POST /ttd/step-instruction.";
    schemas["TTDStepInstructionResponse"]["properties"]["stepped"]["type"] = "boolean";
    schemas["TTDStepInstructionResponse"]["properties"]["dir"]["type"] = "string";
    schemas["TTDStepInstructionResponse"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDStepInstructionResponse"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDStepInstructionResponse"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDStepInstructionResponse"]["properties"]["tinframe"]["format"] = "uint32";

    // POST /ttd/reverse-step
    schemas["TTDReverseStepRequest"]["type"] = "object";
    schemas["TTDReverseStepRequest"]["description"] =
        "Request body for POST /ttd/reverse-step. Exactly one of count or "
        "tstates must be specified.";
    schemas["TTDReverseStepRequest"]["properties"]["count"]["type"] = "integer";
    schemas["TTDReverseStepRequest"]["properties"]["count"]["format"] = "uint32";
    schemas["TTDReverseStepRequest"]["properties"]["count"]["description"] =
        "Number of instructions to step back (M1 boundaries).";
    schemas["TTDReverseStepRequest"]["properties"]["tstates"]["type"] = "integer";
    schemas["TTDReverseStepRequest"]["properties"]["tstates"]["format"] = "uint64";
    schemas["TTDReverseStepRequest"]["properties"]["tstates"]["description"] =
        "Number of T-states to step back (lands at nearest M1 <= target).";

    schemas["TTDReverseStepResponse"]["type"] = "object";
    schemas["TTDReverseStepResponse"]["description"] =
        "Response from POST /ttd/reverse-step.";
    schemas["TTDReverseStepResponse"]["properties"]["reached"]["type"] = "boolean";
    schemas["TTDReverseStepResponse"]["properties"]["mode"]["type"] = "string";
    schemas["TTDReverseStepResponse"]["properties"]["mode"]["enum"].append("count");
    schemas["TTDReverseStepResponse"]["properties"]["mode"]["enum"].append("tstates");
    schemas["TTDReverseStepResponse"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDReverseStepResponse"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDReverseStepResponse"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDReverseStepResponse"]["properties"]["tinframe"]["format"] = "uint32";

    // POST /ttd/reverse-continue
    schemas["TTDReverseContinueRequest"]["type"] = "object";
    schemas["TTDReverseContinueRequest"]["description"] =
        "Request body for POST /ttd/reverse-continue.";
    schemas["TTDReverseContinueRequest"]["properties"]["pcs"]["type"] = "array";
    schemas["TTDReverseContinueRequest"]["properties"]["pcs"]["items"]["type"] = "integer";
    schemas["TTDReverseContinueRequest"]["properties"]["pcs"]["items"]["format"] = "uint16";
    schemas["TTDReverseContinueRequest"]["properties"]["pcs"]["description"] =
        "Non-empty list of reverse breakpoint PCs (required).";
    schemas["TTDReverseContinueRequest"]["required"].append("pcs");

    schemas["TTDReverseContinueResponse"]["type"] = "object";
    schemas["TTDReverseContinueResponse"]["description"] =
        "Response from POST /ttd/reverse-continue.";
    schemas["TTDReverseContinueResponse"]["properties"]["matched"]["type"] = "boolean";
    schemas["TTDReverseContinueResponse"]["properties"]["pc"]["type"] = "integer";
    schemas["TTDReverseContinueResponse"]["properties"]["pc"]["format"] = "uint16";
    schemas["TTDReverseContinueResponse"]["properties"]["frame"]["type"] = "integer";
    schemas["TTDReverseContinueResponse"]["properties"]["frame"]["format"] = "uint64";
    schemas["TTDReverseContinueResponse"]["properties"]["tinframe"]["type"] = "integer";
    schemas["TTDReverseContinueResponse"]["properties"]["tinframe"]["format"] = "uint32";
