    // Stepping and Execution Control endpoints
    paths["/api/v1/emulator/{id}/step"]["post"]["summary"] = "Execute single instruction";
    paths["/api/v1/emulator/{id}/step"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/step"]["post"]["description"] =
        "Breakpoints are honored and never block the call: an execution breakpoint stops before its instruction "
        "(executed = 0), a memory or port breakpoint after it. Stepping from the execution breakpoint the machine "
        "is stopped at runs that instruction. `stop` tells how the step ended.";
    paths["/api/v1/emulator/{id}/step"]["post"]["responses"]["200"]["description"] =
        "pc, sp, state, executed (0 or 1) and stop {cpu, reason: step | breakpoint, breakpoint_id, address, access: "
        "execute | read | write | port_in | port_out}";

    paths["/api/v1/emulator/{id}/steps"]["post"]["summary"] = "Execute N instructions";
    paths["/api/v1/emulator/{id}/steps"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/steps"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/steps"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Number of instructions to execute: 1..10000000 (more: 400, nothing runs)";
    paths["/api/v1/emulator/{id}/steps"]["post"]["description"] =
        "Runs up to `count` instructions; a breakpoint ends the run early (execution: before its instruction; "
        "memory, port: after it). `stop` tells how the run ended. A count out of 1..10000000 is refused (400 with "
        "count and max), never cut.";
    paths["/api/v1/emulator/{id}/steps"]["post"]["responses"]["200"]["description"] =
        "count (asked), executed / instructions_run (ran), frames_run, tstates_run, frame, t, message, pc, sp, state "
        "and stop {cpu, reason: step | breakpoint | stopped, breakpoint_id, address, access}";

    // The debugger events channel (OpenAPI describes the upgrade request only)
    paths["/api/v1/websocket"]["get"]["summary"] = "Debugger events (WebSocket)";
    paths["/api/v1/websocket"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/websocket"]["get"]["description"] =
        "WebSocket upgrade. Send {\"op\":\"subscribe\",\"id\":\"s1\",\"emulator\":\"<id>\",\"topics\":[\"debug\"]} "
        "(emulator and topics optional); the reply {\"op\":\"subscribed\",\"id\":\"s1\",\"seq\":N} is followed by "
        "events {\"op\":\"event\",\"topic\":\"debug\",\"seq\":N,\"event\":\"paused\" | \"resumed\" | \"step_done\" | "
        "\"breakpoints_changed\",\"emulator\":\"<id>\",\"cpu\":\"main\", reason, breakpoint_id, address, access}. "
        "seq counts per emulator. {\"op\":\"unsubscribe\",\"id\":\"s1\"} ends a subscription.";
    paths["/api/v1/websocket"]["get"]["responses"]["101"]["description"] = "Switching protocols to WebSocket";

    // The one WebSocket (websocket-interface.md): first slice, calls
    paths["/api/v1/ws"]["get"]["summary"] = "The WebSocket plane: every REST route as a call (WebSocket)";
    paths["/api/v1/ws"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/ws"]["get"]["description"] =
        "WebSocket upgrade. {\"op\":\"call\",\"rid\":7,\"method\":\"POST\",\"path\":\"/api/v1/controllers/actions/"
        "run.pause_toggle\",\"body\":{...},\"session\":\"<X-Unreal-Session>\"} runs the same handler as REST, with "
        "the same gates, and answers {\"op\":\"result\",\"t\":\"call\",\"rid\":7,\"status\":200,\"body\":{...}}; calls "
        "may be answered out of order. A companion token comes on the handshake (Authorization: Bearer) or in "
        "{\"op\":\"auth\",\"token\":\"unc_...\"}. Topics come later (websocket-interface.md §3).";
    paths["/api/v1/ws"]["get"]["responses"]["101"]["description"] = "Switching protocols to WebSocket";

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
        "Run the CPU for exactly N t-states (1 t-state = 1 ULA step / 2 pixels), 1..14000000000 in one call (more: "
        "400 with count and max, nothing runs). On the run timeline shared with run_frames: a run that starts where "
        "the previous one ended aims at that run's target plus N, so slices end where one run ends (a slice already "
        "passed by the previous run's last instruction runs nothing). Breakpoints skipped unless \"breakpoints\": "
        "true. Answers as run_frames.";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["tstates"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["tstates"]["description"] = "Number of t-states to run (alias key 'count')";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["breakpoints"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/run_tstates"]["post"]["responses"]["200"]["description"] =
        "count, frames_run, tstates_run, frame, t, stop {cpu, reason: done | breakpoint | stopped, ...}, message, pc, "
        "sp, state";

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
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["description"] =
        "Runs at full host speed and leaves the emulator paused. Host audio is held for the run (as for every "
        "direct run: run_frame, run_tstates, run_to_*, step): every sound device still computes its samples "
        "(captures and recordings get them), the speakers get nothing; the hold ends with the run, and resume "
        "drops any hold left without its reason. GET /audio/mixer host_output shows it (holders.direct_run). "
        "1..200000 frames in one call: more is refused (400 with count and max), never cut. Split-independent: "
        "calls of 30 and 70 frames end at the same t-state and machine state as one call of 100 (the run timeline: a "
        "run that starts where the previous one ended aims at that run's target plus N frames). Breakpoints skipped "
        "unless \"breakpoints\": true.";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Number of frames to run (1..200000). Alias key 'frames' is also accepted.";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["breakpoints"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/run_frames"]["post"]["responses"]["200"]["description"] =
        "count (asked), frames_run, tstates_run, frame and t where it ended, stop {cpu, reason: done | breakpoint | "
        "stopped, breakpoint_id, address, access}, message (one line from what ran), pc, sp, state";

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

    // Step out endpoint (SP-tracking walk)
    paths["/api/v1/emulator/{id}/stepout"]["post"]["summary"] = "Step out of current subroutine";
    paths["/api/v1/emulator/{id}/stepout"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/stepout"]["post"]["description"] =
        "SP-tracking walk: runs until a RET-family instruction sits at or above the "
        "entry stack level, then executes it to land in the caller. Breakpoints are "
        "skipped for the walk.";
    paths["/api/v1/emulator/{id}/stepout"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/stepout"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/stepout"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/stepout"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/stepout"]["post"]["responses"]["200"]["description"] =
        "Step out completed (pc/sp/state)";

    // Skip until endpoint (fast-forward to a PC target)
    {
        // A card CPU's run to a moment (RunControl::CardRunTo; the card debugger's Run to)
        Json::Value& op = paths["/api/v1/emulator/{id}/card_run_to"]["post"];
        op["summary"] = "Run a sound card CPU to a card cycle, its next interrupt, the next host command or a main T";
        op["tags"].append("Debug Commands");
        op["description"] =
            "The machine paused, from a card CPU's view (cpu gs, the default, or ms). until: cycle (the card CPU "
            "parks at the first instruction boundary at or after card cycle value of the ZX frame, the gs report's "
            "cpu.frame_cycle; a cycle it already passed: the next frame's), int (parks at the first instruction of "
            "the handler of the card's next accepted interrupt), command (the main CPU's next OUT to the card's "
            "command port: the machine pauses after that main instruction, the card has not taken the command yet; "
            "the reply adds command), main_t (the main CPU runs to T-state value of the frame, the next frame's when "
            "at or past it). A breakpoint of any CPU on the way ends it; no end within 5 s pauses with the reason "
            "timeout. The reply: stop {cpu, reason step / breakpoint / pause / timeout, cpu_pc, breakpoint_id, address, "
            "access, main, command}, until, hit, target_cycle / target_t, cpu, the card CPU's pc and sp, main_pc, "
            "main_sp, card_cycle (where the card stands in the frame), main_t, frame, state, message.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        Json::Value& properties = op["requestBody"]["content"]["application/json"]["schema"]["properties"];
        op["requestBody"]["content"]["application/json"]["schema"]["required"].append("until");
        properties["cpu"]["type"] = "string";
        properties["cpu"]["enum"].append("gs");
        properties["cpu"]["enum"].append("ms");
        properties["cpu"]["description"] = "The card CPU (default gs); ?cpu= works too";
        properties["until"]["type"] = "string";
        for (const char* until : {"cycle", "int", "command", "main_t"})
            properties["until"]["enum"].append(until);
        properties["value"]["type"] = "integer";
        properties["value"]["description"] =
            "cycle: the card cycle of the frame (0 .. the frame's card cycles - 1); main_t: the main CPU's T-state of "
            "the frame (0 .. frame - 1); not used by int and command";
        op["responses"]["200"]["description"] = "Where it stopped (stop, hit, pc, card_cycle, main_t, ...)";
        op["responses"]["400"]["description"] = "No or an unknown until, a value out of range, cpu main";
        op["responses"]["404"]["description"] = "Emulator instance not found, or the card CPU is not there";
        op["responses"]["409"]["description"] = "Not paused, a step of that CPU under way, or run control held";
    }
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["summary"] = "Fast-forward until PC reaches target";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["description"] =
        "Runs until the next instruction to execute is at the target address (or the "
        "t-state budget is exhausted: 0 / absent = 100 frames, at most 700000000). Breakpoints are skipped "
        "unless breakpoints=true. Frames rendered during the skip are not captured by recordings. "
        "Answers hit, target, max_tstates, stop, pc, sp, state.";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["pc"]["type"] = "string";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["max_tstates"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["breakpoints"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["breakpoints"]["description"] =
        "true: a breakpoint on the way stops it first (run to a cursor); default false (skipped)";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["responses"]["200"]["description"] =
        "Skip result (hit flag, target, max_tstates, stop, pc/sp/state)";
    paths["/api/v1/emulator/{id}/skip_until"]["post"]["responses"]["400"]["description"] =
        "Missing/invalid pc field";
