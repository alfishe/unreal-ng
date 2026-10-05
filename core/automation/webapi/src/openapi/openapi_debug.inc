    // Memory inspection and registers
    paths["/api/v1/emulator/{id}/registers"]["get"]["summary"] = "Get CPU registers";
    paths["/api/v1/emulator/{id}/registers"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/registers"]["get"]["description"] =
        "The main Z80: main (af, bc, de, hl), alternate (af_, bc_, de_, hl_), index (ix, iy), special (pc, sp, i, "
        "r with bit 7 as last written, memptr = the internal WZ latch, q = the flag capture register, t = CPU "
        "T-states since the frame's start - above the base T at turbo), interrupt (iff1, iff2, im, halted, boundary = "
        "what the next INT / NMI sampling sees: none, prefix_dd, prefix_fd, int_shadow, ld_a_ir, nmi_ack) and the "
        "decoded flags. Field names follow the debugger protocol's Registers.";
    paths["/api/v1/emulator/{id}/registers"]["get"]["responses"]["200"]["description"] = "CPU register values";

    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["summary"] = "Set CPU register value";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["description"] =
        "Write a value into the named CPU register (af, bc, de, hl, af', bc', de', hl', ix, "
        "iy, sp, pc, i, r, ir, memptr (alias wz), the 8-bit halves, and the interrupt state im (0-2), iff1 and "
        "iff2 (0-1)). The value is masked to the register width (im / iff1 / iff2: refused above their range, "
        "400), written, read back and echoed in the response.";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][1]["description"] =
        "Register name (e.g. 'pc', 'hl', 'a')";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["required"][0] = "value";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["value"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["value"]["description"] = "Register value (0-65535)";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["responses"]["200"]["description"] = "Register updated";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["responses"]["400"]["description"] =
        "Unknown register, missing 'value' field, or a value im / iff1 / iff2 cannot hold";
    paths["/api/v1/emulator/{id}/registers/{name}"]["put"]["responses"]["404"]["description"] = "Emulator instance not found";

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
        "Number of bytes to read (default: 128, max: 4096; with format=binary up to 65536, the whole address space)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][3]["name"] = "format";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][3]["description"] =
        "'hexdump' (default, 16B/line + ASCII sidebar), 'full' (data array + hex), 'sparse' (fill-run segments) or "
        "'binary' (the raw bytes as application/octet-stream; headers X-Unreal-Space, X-Unreal-Address, X-Unreal-Length)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][3]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["description"] = "Memory content";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["content"]["application/octet-stream"]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["content"]["application/octet-stream"]["schema"]["format"] = "binary";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["headers"]["X-Unreal-Space"]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["headers"]["X-Unreal-Address"]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["headers"]["X-Unreal-Length"]["schema"]["type"] = "integer";

    // GET /api/v1/emulator/{id}/memory/map - Sparse non-zero memory overview (TD-3)
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["summary"] = "Sparse non-zero memory map";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["description"] =
        "Block-by-block non-zero allocation overview: merged zero/data regions with per-block type (rom0/ram5), "
        "status (zeros/data), non-zero byte count and FNV-1a hash (~200-500 tokens for the whole 64K).";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["name"] = "view";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["description"] =
        "'address' (default, 64K CPU space) or 'ram' (physical RAM pages)";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["schema"]["enum"].append("address");
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][1]["schema"]["enum"].append("ram");
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][2]["name"] = "min_run";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][2]["description"] =
        "Zero-run merge threshold (default 64; shorter zero runs fold into data blocks)";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][3]["name"] = "max_blocks";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][3]["description"] =
        "Block budget (default 48); overflow retries with coarser granularity, then sets truncated";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["responses"]["200"]["description"] =
        "Sparse map: model, view, total_size, non_zero_bytes, min_run, block_count, truncated, blocks[]";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["responses"]["400"]["description"] = "Invalid view/min_run/max_blocks";
    paths["/api/v1/emulator/{id}/memory/map"]["get"]["responses"]["404"]["description"] = "Emulator instance not found";

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
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][5]["name"] = "format";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][5]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][5]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][5]["description"] =
        "Omit for the JSON answer; 'binary' = the raw bytes as application/octet-stream";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][5]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["description"] =
        "Page content";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["content"]["application/octet-stream"]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["content"]["application/octet-stream"]["schema"]["format"] = "binary";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["headers"]["X-Unreal-Space"]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["headers"]["X-Unreal-Address"]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["headers"]["X-Unreal-Length"]["schema"]["type"] = "integer";

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
    // GET /api/v1/emulator/{id}/debug/wait - long-poll on the snapshot's seq (debugger additions tdd §6)
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/debug/wait"]["get"];
        op["summary"] = "Wait until something a debugger shows changes (long-poll)";
        op["tags"].append("Debug Commands");
        op["description"] =
            "Answers when the debugger snapshot's seq moves past `since` - a stop (pause, breakpoint, the end of a "
            "step), a run start or a tool edit - or after timeout_ms: {seq, changed, state, pause {reason, "
            "breakpoint_id, address}}. `since` omitted = the current seq (wait for the next change); already behind = "
            "an immediate answer. No server thread waits, so many clients can wait at once. For clients without "
            "WebSocket; /api/v1/websocket pushes the same stops.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["parameters"][1]["name"] = "since";
        op["parameters"][1]["in"] = "query";
        op["parameters"][1]["schema"]["type"] = "integer";
        op["parameters"][1]["description"] = "The seq the client last saw (default: the current one)";
        op["parameters"][2]["name"] = "timeout_ms";
        op["parameters"][2]["in"] = "query";
        op["parameters"][2]["schema"]["type"] = "integer";
        op["parameters"][2]["description"] = "0..60000, default 10000 (0 = answer now)";
        op["responses"]["200"]["description"] = "{seq, changed, state: paused | running | stopped, pause}";
        op["responses"]["400"]["description"] = "Bad since / timeout_ms";
        op["responses"]["404"]["description"] = "No such emulator (or it was removed while waiting)";
    }

    // GET /api/v1/emulator/{id}/debug/snapshot - one coherent picture for a debugger front end (D7 + E3)
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/debug/snapshot"]["get"];
        op["summary"] = "Debugger snapshot: registers, previous registers, pages, stack, time, code and memory at one moment";
        op["tags"].append("Debug Commands");
        op["description"] =
            "Everything a debugger window redraws, in one request and from one moment: read while the emulator stays "
            "paused (consistency 'paused'), at the next frame boundary of a running machine without pausing it "
            "('frame'), or directly when it does not run ('stopped'). Fields: seq (grows at every stop, run start and "
            "tool edit: an equal seq means nothing changed), cpu ('z80'), state (paused | running | stopped), pause "
            "{reason none | pause | breakpoint | step, breakpoint_id, address}, consistency, regs (the GET /registers "
            "object), prev_regs (the same at the previous stop; null before the first), pages[] {window, start, kind "
            "rom | ram | cache, page}, stack {sp, words[]}, time {frame, t, frame_t, line, dot}, disasm[] (GET /disasm "
            "instructions), memory[] {space, address, length, base64} or {space, address, error}. 503 when no "
            "coherent moment came within 500 ms (the emulator is stepping on another thread).";
        Json::Value& p = op["parameters"];
        p[0]["name"] = "id";
        p[0]["in"] = "path";
        p[0]["required"] = true;
        p[0]["schema"]["type"] = "string";
        p[1]["name"] = "disasm";
        p[1]["in"] = "query";
        p[1]["description"] = "Lines from PC (default 0 = none, at most 100)";
        p[1]["schema"]["type"] = "integer";
        p[2]["name"] = "stack";
        p[2]["in"] = "query";
        p[2]["description"] = "Words from SP (default 8, 0 = none, at most 128)";
        p[2]["schema"]["type"] = "integer";
        p[3]["name"] = "memory";
        p[3]["in"] = "query";
        p[3]["description"] =
            "Memory windows <space>:<address>:<length> (space cpu | ram | ram5 | rom2 | cache0, length at most 65536); "
            "repeat the parameter or separate with commas; at most 8";
        p[3]["schema"]["type"] = "string";
        op["responses"]["200"]["description"] = "The snapshot";
        op["responses"]["400"]["description"] = "Bad parameters (the message says which)";
        op["responses"]["503"]["description"] = "No coherent moment within 500 ms; try again";
    }

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

    // Memory pattern search endpoint
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["summary"] = "Search memory for a byte pattern";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["description"] =
        "Scans memory for a byte pattern (MemorySearch, the same as CLI find, Lua / Python mem_find, MCP find_bytes). "
        "Body: {pattern_hex: \"CD ?? 00\" (?? = any byte, A? = any low nibble) or pattern: [205, 22, 0], mask_hex: "
        "\"FF 00 FF\" (optional, 1 bits must match; replaces the wildcards), space: \"cpu\" (default: what the CPU "
        "sees now) | \"ram\" (every RAM page) | a page \"ram5\" / \"rom2\" / \"cache0\", start, end (in the space; "
        "default the whole space), max: 64, alignment: 1 | 2}. Each match: address (cpu) or page {kind, page} + offset, "
        "context_start and context (4 bytes before, the match, 4 after).";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["pattern_hex"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["mask_hex"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["space"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["start"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["end"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["max"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["alignment"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/find"]["post"]["responses"]["200"]["description"] =
        "space, count, matches[] (address or page + offset, context_start, context), truncated, range";
