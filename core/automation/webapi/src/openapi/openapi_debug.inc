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
    // GET / POST /api/v1/emulator/{id}/debug/pchist - PC history with pages (debugger additions tdd §7)
    {
        const char* kPath = "/api/v1/emulator/{id}/debug/pchist";
        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "PC history: the instructions the CPU started, with their window's page";
        get["tags"].append("Debug Commands");
        get["description"] =
            "Newest first, at most 1024 kept: {armed, started_now, total, capacity, entries [{address, kind rom | ram | "
            "cache, page}]}. One entry per instruction the CPU starts (an accepted interrupt is none; a prefix is part "
            "of its instruction), the page the 16K window of its address showed at that moment. Recording costs the "
            "emulation nothing until a debugger asks: the first read arms it (started_now: entries come from then on). "
            "Example on TS-Conf: a CALL #C000 with RAM page #20 in window 3 adds {address 49152, kind ram, page 32}.";
        get["parameters"][0]["name"] = "id";
        get["parameters"][0]["in"] = "path";
        get["parameters"][0]["required"] = true;
        get["parameters"][0]["schema"]["type"] = "string";
        get["parameters"][1]["name"] = "depth";
        get["parameters"][1]["in"] = "query";
        get["parameters"][1]["description"] = "Entries (default 32, at most 1024)";
        get["parameters"][1]["schema"]["type"] = "integer";
        get["responses"]["200"]["description"] = "The history";
        get["responses"]["400"]["description"] = "Bad depth";
        get["responses"]["503"]["description"] = "No coherent moment within wait_ms (default 500); try again";

        Json::Value& post = paths[kPath]["post"];
        post["summary"] = "Start (empty) or stop the PC history";
        post["tags"].append("Debug Commands");
        post["parameters"] = Json::Value(Json::arrayValue);
        post["parameters"].append(get["parameters"][0]);
        post["requestBody"]["required"] = true;
        post["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        post["requestBody"]["content"]["application/json"]["schema"]["properties"]["enabled"]["type"] = "boolean";
        post["responses"]["200"]["description"] = "{armed}";
        post["responses"]["400"]["description"] = "Body without a boolean 'enabled'";
    }

    // GET /api/v1/emulator/{id}/debug/timeline, POST .../timeline/arm - the GS debugger's twin timeline
    // (gs-debugger design §5.4, §7.2; gui-card-debugger §A5.1)
    {
        const char* kPath = "/api/v1/emulator/{id}/debug/timeline";
        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "Twin timeline: the main CPU and a sound card's CPU on one clock";
        get["tags"].append("Debug Commands");
        get["description"] =
            "Paused or stopped: {available, state, armed, started_now, cpu, lanes [{cpu, name, clock_hz, unit, "
            "units_per_second, units_per_t, readout, recording}], now {frame, t, gap_t, main_t, main_frame, card_units, "
            "card_cycle}, position {main?, main_text (\"6.6 / 11 T · 60% · port write next\"), card_text, gap_t}, "
            "window {span_t, from_t, to_t, frame_t}, recent {main: [...], <cpu>: [...]} (oldest first: {pc, at, "
            "duration_t, frame, t | units, duration_units, duration_cycles, clock_hz, kind instruction | int | nmi | "
            "idle, text, label, idle_iterations, open}), next {main, <cpu>} ({pc, at, text, label}), markers [{lane, "
            "kind breakpoint | host_port | int | nmi | clock_change, at, pc, id, port, dir, value, from_hz, to_hz, "
            "text}], density [], leases [{owner, left_ms}], text}. `at` is main T relative to NOW (the card's moment: "
            "main T minus the gap; negative = past); a card block's width is exact in its own units (duration_units) and "
            "converts to main T through units_per_t. A block lasts until the next record of its lane starts "
            "(contention, /WAIT, an interrupt acknowledge, the NeoGS clock are in it). Running: {state running, lanes, "
            "density [{frame, main {commands, data_writes, data_reads, status_polls, accesses}, card {steps, "
            "idle_steps, busy_steps, interrupts, nmis, commands_taken, profiled, cycles?, isr_cycles?, idle_cycles?, "
            "halt_cycles?, busy_cycles?}}], revision}: the published density only, the machine never held. Nothing "
            "records until a client holds a lease: a read takes or renews it (lease_ms, default 30000), arming the "
            "execution histories `cpu`, `<slot>.cpu` and the GS command log that are off; the last lease's end switches "
            "them back off. Example: a NeoGS clock change in the window is the marker \"12→24 MHz at cycle 118,400\".";
        auto param = [&get](int index, const char* name, const char* in, bool required, const char* type,
                            const char* description) {
            Json::Value& p = get["parameters"][index];
            p["name"] = name;
            p["in"] = in;
            p["required"] = required;
            p["schema"]["type"] = type;
            if (description)
                p["description"] = description;
        };
        param(0, "id", "path", true, "string", nullptr);
        param(1, "cpu", "query", false, "string", "The card lane: gs | ms (default: the first card CPU fitted)");
        param(2, "span", "query", false, "number", "The window: ± main T around NOW (default 60, 1..100000)");
        param(3, "recent", "query", false, "integer", "Blocks per lane at most, newest kept (default 64, at most 512)");
        param(4, "density", "query", false, "integer", "Running: density ticks at most (default 50, at most 64 kept)");
        param(5, "client", "query", false, "string", "The lease's owner (default rest)");
        param(6, "lease_ms", "query", false, "integer", "The lease this read takes or renews (default 30000; 0: until released)");
        param(7, "arm", "query", false, "boolean", "false: read without taking or renewing a lease");
        param(8, "wait_ms", "query", false, "integer",
              "Paused: how long to wait for a coherent moment (default 500, 0..500; 0: one try, 503 at once - a GUI "
              "client that must not block)");
        get["responses"]["200"]["description"] = "The timeline";
        get["responses"]["400"]["description"] = "A bad parameter";
        get["responses"]["404"]["description"] = "No such card CPU fitted";
        get["responses"]["503"]["description"] = "No coherent moment within wait_ms (default 500); try again";

        Json::Value& post = paths["/api/v1/emulator/{id}/debug/timeline/arm"]["post"];
        post["summary"] = "Take, renew or end a client's twin timeline lease";
        post["tags"].append("Debug Commands");
        post["description"] =
            "{on: true} takes or renews `client`'s lease for lease_ms (default 30000; 0 until released), arming the "
            "histories the timeline reads; {on: false} ends it (the last lease switches them back off). Answer: {armed, "
            "started_now, leases [{owner, left_ms}]}.";
        post["parameters"] = Json::Value(Json::arrayValue);
        post["parameters"].append(get["parameters"][0]);
        post["requestBody"]["required"] = true;
        Json::Value& body = post["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["on"]["type"] = "boolean";
        body["properties"]["client"]["type"] = "string";
        body["properties"]["lease_ms"]["type"] = "integer";
        body["required"].append("on");
        post["responses"]["200"]["description"] = "{armed, started_now, leases}";
        post["responses"]["400"]["description"] = "A body without a boolean 'on', or a bad lease_ms";
    }

    // GET / PUT /api/v1/emulator/{id}/debug/selected_cpu - the CPU the front-ends show (gs-debugger design §5.2)
    {
        const char* kPath = "/api/v1/emulator/{id}/debug/selected_cpu";
        Json::Value& get = paths[kPath]["get"];
        get["summary"] = "The CPU the debugger front-ends show";
        get["tags"].append("Debug Commands");
        get["description"] =
            "{cpu: main | gs | ms, follows}. One per machine, the last front-end's choice. A card CPU shown follows the "
            "main CPU's steps of the paused machine (step, steps, run_tstates, run_frames) in tight mode: the card runs up "
            "to the main CPU's time at every main instruction start and at the step's end, so its registers and memory "
            "are the ones at the main CPU's moment (follows: a card is shown and is in its slot). Costs nothing while "
            "the machine runs.";
        get["parameters"][0]["name"] = "id";
        get["parameters"][0]["in"] = "path";
        get["parameters"][0]["required"] = true;
        get["parameters"][0]["schema"]["type"] = "string";
        get["responses"]["200"]["description"] = "{cpu, follows}";

        Json::Value& put = paths[kPath]["put"];
        put["summary"] = "Show a CPU in the debugger front-ends";
        put["tags"].append("Debug Commands");
        put["description"] = "Body {\"cpu\": \"gs\"} (or ?cpu=gs); main stops a card following the main CPU's steps.";
        put["parameters"] = Json::Value(Json::arrayValue);
        put["parameters"].append(get["parameters"][0]);
        put["parameters"][1]["name"] = "cpu";
        put["parameters"][1]["in"] = "query";
        put["parameters"][1]["description"] = "main, gs or ms (or the body's cpu)";
        put["parameters"][1]["schema"]["type"] = "string";
        put["requestBody"]["required"] = false;
        put["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        put["requestBody"]["content"]["application/json"]["schema"]["properties"]["cpu"]["type"] = "string";
        put["responses"]["200"]["description"] = "{cpu, follows}";
        put["responses"]["400"]["description"] = "No cpu or an unknown one";
        put["responses"]["404"]["description"] = "A card CPU with no card in its slot";
    }

    // Device boards (debugger-model widget catalog §4, protocol §6.4; gs-debugger N4 / N8)
    {
        Json::Value idParam;
        idParam["name"] = "id";
        idParam["in"] = "path";
        idParam["required"] = true;
        idParam["schema"]["type"] = "string";
        Json::Value boardParam = idParam;
        boardParam["name"] = "board";
        boardParam["description"] = "gs (the classic card), neogs, neogs_dma, neogs_sd, neogs_mp3, neogs_flash";

        Json::Value& list = paths["/api/v1/emulator/{id}/debug/boards"]["get"];
        list["summary"] = "The device boards the machine has now";
        list["tags"].append("Debug Commands");
        list["description"] =
            "A device board is a device's hardware state as data: a layout of columns, groups and controls with their "
            "values, which any front-end renders with one generic renderer. {boards: [{id, title, device, cpu, "
            "available, actions[{id, title, param}]}]}. The GS slot publishes gs (classic GS, lightweight player) or "
            "the NeoGS boards neogs, neogs_dma, neogs_sd, neogs_mp3 (available: a decoder is fitted), neogs_flash. "
            "Built on read, side-effect free (CLI board list, MCP inspect_state aspect boards, Lua / Python boards()).";
        list["parameters"].append(idParam);
        list["responses"]["200"]["description"] = "{boards: [...]}";

        Json::Value& one = paths["/api/v1/emulator/{id}/debug/boards/{board}"]["get"];
        one["summary"] = "One device board: layout and values";
        one["tags"].append("Debug Commands");
        one["description"] =
            "{id, title, device, cpu, available, actions[], columns: [{groups: [{title, raw, controls: [{id, label, "
            "type, value, note, names (bits, bit 0 first), values (enum), min / max (bar)}]}]}]}. Control types: hex8, "
            "hex16, hex21, dec, counter, rate, bool, led, text, bits, enum, bar. ?values=1: {id, available, "
            "values: {control id: value}} for polling (CLI board <id>, MCP aspect board.<id>, Lua / Python board(id)).";
        one["parameters"].append(idParam);
        one["parameters"].append(boardParam);
        one["parameters"][2]["name"] = "values";
        one["parameters"][2]["in"] = "query";
        one["parameters"][2]["description"] = "1: the values only";
        one["parameters"][2]["schema"]["type"] = "string";
        one["responses"]["200"]["description"] = "The board";
        one["responses"]["404"]["description"] = "No such board on this machine (the message lists the boards)";

        Json::Value& act = paths["/api/v1/emulator/{id}/debug/boards/{board}/actions/{action}"]["post"];
        act["summary"] = "Run a board's action";
        act["tags"].append("Debug Commands");
        act["description"] =
            "neogs_sd insert (arg: an image file or a host folder) and eject are requests to the media manager's slot "
            "sd.ngs (the same as media insert / eject: at once while the machine is not running, else at the next "
            "frame boundary; while a TTD recording runs, an event of the recording); neogs_flash save writes a "
            "modified flash in persist mode. Body {\"arg\": \"...\"} (or \"path\", or ?arg=). {board, action, arg, "
            "status: done | queued} (CLI board <id> <action> [arg], MCP emulator_manage board_action, Lua / Python "
            "board_action(id, action, arg)).";
        act["parameters"].append(idParam);
        act["parameters"].append(boardParam);
        act["parameters"][2]["name"] = "action";
        act["parameters"][2]["in"] = "path";
        act["parameters"][2]["required"] = true;
        act["parameters"][2]["schema"]["type"] = "string";
        act["requestBody"]["required"] = false;
        act["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        act["requestBody"]["content"]["application/json"]["schema"]["properties"]["arg"]["type"] = "string";
        act["responses"]["200"]["description"] = "{board, action, arg, status}";
        act["responses"]["400"]["description"] = "insert without a path";
        act["responses"]["404"]["description"] = "No such board or action";
        act["responses"]["409"]["description"] = "A TTD replay owns the machine";
        act["responses"]["422"]["description"] = "No file or folder at the path; the request failed";
    }

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
            "Memory windows <space>:<address>:<length> (space cpu | ram | ram5 | rom2 | cache0 | a device memory region by name, length at most 65536); "
            "repeat the parameter or separate with commas; at most 8";
        p[3]["schema"]["type"] = "string";
        p[4]["name"] = "pchist";
        p[4]["in"] = "query";
        p[4]["description"] =
            "PC history entries, newest first (default 0 = none, at most 1024): the snapshot's pchist {armed, "
            "started_now, total, capacity, entries [{address, kind, page}]}; the first request arms the history";
        p[4]["schema"]["type"] = "integer";
        p[5]["name"] = "cpu";
        p[5]["in"] = "query";
        p[5]["description"] =
            "main (default), gs or ms: the CPU the snapshot is of. cpu stays 'z80' and debug_cpu names it; a card "
            "CPU's regs are its registers (GET /registers?cpu=), prev_regs null, pages[] its four 16K windows {window, "
            "start, kind rom | ram | none, page}, stack its SP and words, time {frame, t, frame_t, card_time}, disasm "
            "from its PC, memory windows in its view (space cpu or the CPU's name). pchist is refused for a card CPU "
            "(400 code not_supported)";
        p[5]["schema"]["type"] = "string";
        p[5]["schema"]["enum"].append("main");
        p[5]["schema"]["enum"].append("gs");
        p[5]["schema"]["enum"].append("ms");
        op["responses"]["200"]["description"] = "The snapshot (debug_cpu: main | gs | ms)";
        op["responses"]["400"]["description"] = "Bad parameters (code invalid_argument) or pchist of a card CPU (not_supported)";
        op["responses"]["404"]["description"] = "The card CPU is not there (code cpu_unavailable)";
        op["responses"]["503"]["description"] = "No coherent moment within 500 ms; try again (code busy)";
    }

    // GET /api/v1/emulator/{id}/debug/targets - the CPUs a debugger can name (CpuView::Targets)
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/debug/targets"]["get"];
        op["summary"] = "The CPUs a debugger can name now";
        op["tags"].append("Debug Commands");
        op["description"] =
            "{selected, targets: [{cpu: main | gs | ms, available, card}]}: every value the cpu parameter takes, "
            "whether that CPU is there now (a card with a CPU in its slot) and the card (gs, neogs). selected is the "
            "CPU the front-ends show (GET /debug/selected_cpu).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["responses"]["200"]["description"] = "{selected, targets}";
        op["responses"]["404"]["description"] = "Emulator instance not found";
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
        "Number of instructions (default: 10, max: 256)";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][3]["name"] = "before";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][3]["description"] =
        "Rows above the address first (default: 0, max: 256), synced back from the known instruction starts";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][4]["name"] = "snap";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][4]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][4]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][4]["description"] =
        "1: move the address to the start of the row that holds it (a scrollbar drag)";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][4]["schema"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][5]["name"] = "cpu";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][5]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][5]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][5]["description"] =
        "main (default), gs or ms: a sound card CPU's code from its PC";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][5]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["responses"]["200"]["description"] =
        "Disassembled rows: instructions, and (kind data / cut) db rows of the memory markup's data ranges or of an "
        "instruction's bytes cut by a known instruction start";

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
        "sees now) | \"ram\" (every RAM page) | a page \"ram5\" / \"rom2\" / \"cache0\" | a device memory region by name or alias, start, end (in the space; "
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

    // The sound card CPU (cpuview.h): ?cpu= on the register, memory, disassembly and memory map routes
    {
        const char* text = "main (default), gs (the General Sound slot card's CPU) or ms (the MultiSound's General "
                           "Sound): the CPU whose registers / memory view / code it is. 400 for another name, 404 "
                           "for a card CPU that is not there now (a card without a CPU, an empty slot)";
        const std::pair<const char*, const char*> routes[] = {
            {"/api/v1/emulator/{id}/registers", "get"},        {"/api/v1/emulator/{id}/registers/{name}", "put"},
            {"/api/v1/emulator/{id}/memory/{addr}", "get"},    {"/api/v1/emulator/{id}/memory/{addr}", "put"},
            {"/api/v1/emulator/{id}/disasm", "get"},           {"/api/v1/emulator/{id}/memory/map", "get"},
            {"/api/v1/emulator/{id}/memory/info", "get"},      {"/api/v1/emulator/{id}/memory/read/{address}", "get"}};
        for (const auto& [path, verb] : routes)
        {
            Json::Value& operation = paths[path][verb];
            Json::Value cpu;
            cpu["name"] = "cpu";
            cpu["in"] = "query";
            cpu["required"] = false;
            cpu["description"] = text;
            cpu["schema"]["type"] = "string";
            cpu["schema"]["enum"].append("main");
            cpu["schema"]["enum"].append("gs");
            cpu["schema"]["enum"].append("ms");
            operation["parameters"].append(cpu);
            operation["responses"]["404"]["description"] = "Emulator instance not found, or the card CPU is not there";
        }
        // POST /memory/write takes it in the body
        Json::Value& write = paths["/api/v1/emulator/{id}/memory/write"]["post"];
        write["requestBody"]["content"]["application/json"]["schema"]["properties"]["cpu"]["type"] = "string";
        write["requestBody"]["content"]["application/json"]["schema"]["properties"]["cpu"]["description"] = text;
        write["responses"]["404"]["description"] = "Emulator instance not found, or the card CPU is not there";
        paths["/api/v1/emulator/{id}/registers"]["get"]["description"] =
            paths["/api/v1/emulator/{id}/registers"]["get"]["description"].asString() +
            " With cpu=gs / ms the card CPU's registers in the same shape plus cpu; special has card_time (the "
            "card's own clock) instead of t.";
        paths["/api/v1/emulator/{id}/memory/map"]["get"]["description"] =
            paths["/api/v1/emulator/{id}/memory/map"]["get"]["description"].asString() +
            " With cpu=gs / ms: {cpu, windows: [{start, size, kind (rom, ram, none), page, offset}]}, the card CPU's "
            "four 16K windows.";
        // The steps: ?cpu= (or the body's cpu) gs / ms steps that card CPU (RunControl::CardStep)
        const char* step = "main (default), gs or ms: the CPU that steps. A card CPU's step resumes the machine until "
                           "the card ends it, then pauses it; the card runs as the main CPU's run takes it along (at "
                           "a host port access or the frame end), so the main CPU moves on to that point. A breakpoint "
                           "of any CPU on the way ends it; no end within 2 s (over / out / skip_until 5 s) pauses with the reason "
                           "timeout. The reply: stop {cpu, reason step / breakpoint / pause / timeout, cpu_pc, "
                           "breakpoint_id, address, access}, the card CPU's pc and sp, the main CPU's main_pc and "
                           "main_sp, state. 409 when not paused or a step of that CPU is under way";
        for (const char* path : {"/api/v1/emulator/{id}/step", "/api/v1/emulator/{id}/steps",
                                 "/api/v1/emulator/{id}/stepover", "/api/v1/emulator/{id}/stepout",
                                 "/api/v1/emulator/{id}/skip_until"})
        {
            Json::Value& operation = paths[path]["post"];
            Json::Value cpu;
            cpu["name"] = "cpu";
            cpu["in"] = "query";
            cpu["required"] = false;
            cpu["description"] = step;
            cpu["schema"]["type"] = "string";
            cpu["schema"]["enum"].append("main");
            cpu["schema"]["enum"].append("gs");
            cpu["schema"]["enum"].append("ms");
            operation["parameters"].append(cpu);
            operation["responses"]["404"]["description"] = "Emulator instance not found, or the card CPU is not there";
        }
        paths["/api/v1/emulator/{id}/steps"]["post"]["description"] =
            paths["/api/v1/emulator/{id}/steps"]["post"]["description"].asString() +
            " With cpu=gs / ms: count (1-10000) instructions of that card CPU.";
        // Debugger model Phase 3 stage 3: the page routes read a card CPU's own pages by number (rom | ram; a
        // write is refused), find searches its view; the call trace, the PC history and the page disassembly are the
        // main CPU's only (a card CPU: 400 code not_supported)
        const char* page = "main (default), gs or ms. A card CPU: its own ROM / RAM page by number (type rom or ram, "
                           "offset inside the 16K page), {cpu, type, page, offset, length, data, hex}; a write is "
                           "refused (400 code not_supported: write its view, POST /memory/write with cpu)";
        const char* mainOnly = "main (default). This is the main CPU's only: gs or ms is refused (400 code "
                               "not_supported); 404 code cpu_unavailable for a card CPU that is not there";
        const std::pair<const char*, const char*> cpuRoutes[] = {
            {"/api/v1/emulator/{id}/memory/page/{type}/{page}", "get"},
            {"/api/v1/emulator/{id}/memory/page/{type}/{page}", "post"},
            {"/api/v1/emulator/{id}/memory/{type}/{page}/{offset}", "get"},
            {"/api/v1/emulator/{id}/memory/{type}/{page}/{offset}", "put"},
            {"/api/v1/emulator/{id}/calltrace", "get"},
            {"/api/v1/emulator/{id}/debug/pchist", "get"},
            {"/api/v1/emulator/{id}/disasm/page", "get"}};
        for (const auto& [path, verb] : cpuRoutes)
        {
            const bool main = std::string(path).find("/memory/") == std::string::npos;
            Json::Value& operation = paths[path][verb];
            Json::Value cpu;
            cpu["name"] = "cpu";
            cpu["in"] = "query";
            cpu["required"] = false;
            cpu["description"] = main ? mainOnly : page;
            cpu["schema"]["type"] = "string";
            cpu["schema"]["enum"].append("main");
            cpu["schema"]["enum"].append("gs");
            cpu["schema"]["enum"].append("ms");
            operation["parameters"].append(cpu);
            operation["responses"]["400"]["description"] =
                main ? "A card CPU (code not_supported)" : "Bad parameters, or a write of a card CPU's page (code not_supported)";
            operation["responses"]["404"]["description"] = "Emulator instance not found, or the card CPU is not there";
        }
        Json::Value& find = paths["/api/v1/emulator/{id}/memory/find"]["post"];
        find["requestBody"]["content"]["application/json"]["schema"]["properties"]["cpu"]["type"] = "string";
        find["requestBody"]["content"]["application/json"]["schema"]["properties"]["cpu"]["description"] =
            "main (default), gs or ms: a card CPU searches its 64K view (space cpu only; another space: 400 code "
            "not_supported); the reply adds cpu";
        find["responses"]["404"]["description"] = "Emulator instance not found, or the card CPU is not there";
        paths["/api/v1/emulator/{id}/skip_until"]["post"]["description"] =
            paths["/api/v1/emulator/{id}/skip_until"]["post"]["description"].asString() +
            " With cpu=gs / ms: that card CPU runs to pc (run to the cursor; a cursor at its PC means the next run "
            "of that instruction); breakpoints always end it, max_tstates is not used; the reply adds hit.";
    }
