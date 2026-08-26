    // Stepping and Execution Control endpoints
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
         ["count"]["description"] = "Number of frames to run (max: 10000). Alias key 'frames' is also accepted.";
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
