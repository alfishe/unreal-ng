    // Memory inspection and registers
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
