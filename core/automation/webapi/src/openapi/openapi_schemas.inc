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

