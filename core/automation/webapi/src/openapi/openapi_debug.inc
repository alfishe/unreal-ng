    // Debug Commands endpoints
    // Stepping
    paths["/api/v1/emulator/{id}/step"]["post"]["summary"] = "Execute single instruction";
    paths["/api/v1/emulator/{id}/step"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/step"]["post"]["responses"]["200"]["description"] =
        "Instruction executed, returns new PC";

    paths["/api/v1/emulator/{id}/steps"]["post"]["summary"] = "Execute N instructions";
    paths["/api/v1/emulator/{id}/steps"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/steps"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/steps"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Number of instructions to execute";
    paths["/api/v1/emulator/{id}/steps"]["post"]["responses"]["200"]["description"] = "Instructions executed";

    paths["/api/v1/emulator/{id}/stepover"]["post"]["summary"] = "Step over call instruction";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["responses"]["200"]["description"] = "Stepped over call";

    // Atomic stepping
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["summary"] = "Run N t-states";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["description"] =
        "Run the CPU for exactly N t-states (1 t-state = 1 ULA step / 2 pixels)";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["tstates"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["tstates"]["description"] = "Number of t-states to run";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["responses"]["200"]["description"] = "T-states executed";

    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["summary"] = "Run to specific scanline";
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["scanline"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["scanline"]["description"] = "Target scanline number";
    paths["/api/v1/emulator/{id}/run_to_scanline"]["post"]["responses"]["200"]["description"] = "Ran to scanline";

    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["summary"] = "Run N scanlines";
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Number of scanlines to run";
    paths["/api/v1/emulator/{id}/run_scanlines"]["post"]["responses"]["200"]["description"] = "Scanlines executed";

    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["summary"] = "Run to next screen pixel";
    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_to_pixel"]["post"]["responses"]["200"]["description"] = "Ran to next screen pixel";

    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["summary"] = "Run to next interrupt";
    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_to_interrupt"]["post"]["responses"]["200"]["description"] = "Ran to interrupt";

    paths["/api/v1/emulator/{id}/run_frame"]["post"]["summary"] = "Run one complete video frame";
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["description"] =
        "Run exactly one video frame worth of t-states. Stops at approximately the same beam position.";
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_frame"]["post"]["responses"]["200"]["description"] = "Frame executed";

    paths["/api/v1/emulator/{id}/run_frames"]["post"]["summary"] = "Run N complete video frames";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Number of frames to run (max: 10000)";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["responses"]["200"]["description"] = "Frames executed";

    // Debug mode
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["summary"] = "Get debug mode state";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["responses"]["200"]["description"] = "Debug mode enabled state";

    paths["/api/v1/emulator/{id}/debugmode"]["put"]["summary"] = "Enable/disable debug mode";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["responses"]["200"]["description"] = "Debug mode updated";

    // Breakpoints
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["summary"] = "List all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["responses"]["200"]["description"] = "List of breakpoints";

    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["summary"] = "Add breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/AddBreakpointRequest";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["description"] = "Breakpoint created";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/AddBreakpointResponse";

    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["summary"] = "Clear all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["responses"]["200"]["description"] = "All breakpoints cleared";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["summary"] = "Remove specific breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["responses"]["200"]["description"] =
        "Breakpoint removed";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["summary"] = "Enable breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["responses"]["200"]["description"] =
        "Breakpoint enabled";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["summary"] = "Disable breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["responses"]["200"]["description"] =
        "Breakpoint disabled";

    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["summary"] = "Get breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["description"] =
        "Returns information about the last triggered breakpoint, including type (memory/port), address, and access "
        "mode";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["description"] = "Breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["content"]["application/json"]
         ["schema"]["$ref"] = "#/components/schemas/BreakpointStatusResponse";

    // Memory inspection
    paths["/api/v1/emulator/{id}/registers"]["get"]["summary"] = "Get CPU registers";
    paths["/api/v1/emulator/{id}/registers"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/registers"]["get"]["responses"]["200"]["description"] = "CPU register values";

    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["summary"] = "Read memory";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["name"] = "addr";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["description"] =
        "Start address (hex or decimal)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["name"] = "len";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["description"] =
        "Number of bytes to read (default: 16, max: 256)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["description"] = "Memory content";

    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["summary"] = "Write memory";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["description"] = "Write bytes to Z80 memory address space";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["name"] = "addr";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["description"] =
        "Start address (hex or decimal)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["type"] = "array";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["items"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["description"] = "Byte values (0-255)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["hex"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["hex"]["description"] = "Space-separated hex bytes";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["responses"]["200"]["description"] = "Memory written";

    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["summary"] = "Read from physical page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["description"] =
        "Read bytes from physical RAM/ROM/cache/misc page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["description"] =
        "Memory type: ram|rom|cache|misc";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["description"] = "Page number";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["name"] = "offset";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["description"] =
        "Offset within page (0-16383)";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["name"] = "len";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["description"] =
        "Number of bytes to read (default: 128, max: 16384)";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["description"] =
        "Page content";

    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["summary"] = "Write to physical page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["description"] =
        "Write bytes to physical page. ROM write requires force flag.";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["name"] = "offset";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["data"]["type"] = "array";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["data"]["items"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["hex"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["force"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["force"]["description"] = "Required for ROM write";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["responses"]["200"]["description"] =
        "Page written";

    paths["/api/v1/emulator/{id}/memory/info"]["get"]["summary"] = "Get memory configuration";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["description"] = "Get page counts and current Z80 bank mappings";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["responses"]["200"]["description"] = "Memory configuration";

    // Analysis
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["summary"] = "Get memory access counters";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["responses"]["200"]["description"] = "Memory access statistics";

    paths["/api/v1/emulator/{id}/calltrace"]["get"]["summary"] = "Get call trace";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["description"] =
        "Max entries to return (default: 50)";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["responses"]["200"]["description"] = "Call trace entries";

    // Disassembly endpoint
    paths["/api/v1/emulator/{id}/disasm"]["get"]["summary"] = "Disassemble Z80 code";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["name"] = "address";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["description"] =
        "Start address (hex or decimal, default: PC)";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["name"] = "count";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["description"] =
        "Number of instructions (default: 10, max: 100)";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["responses"]["200"]["description"] = "Disassembled instructions";

    // Physical page disassembly endpoint
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["summary"] = "Disassemble from physical RAM/ROM page";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["description"] = "Memory type: 'ram' or 'rom'";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["description"] = "Physical page number (0-255)";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["name"] = "offset";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["description"] =
        "Offset within page (default: 0)";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["name"] = "count";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["description"] =
        "Number of instructions (default: 10, max: 100)";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["responses"]["200"]["description"] =
        "Disassembled instructions from physical page";

