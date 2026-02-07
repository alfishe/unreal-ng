    // Memory Profiler control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["summary"] = "Start memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["description"] =
        "Start memory profiler session. Tracks read/write/execute patterns.";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["responses"]["200"]["description"] =
        "Profiler started";

    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["summary"] = "Stop memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["responses"]["200"]["description"] = "Profiler stopped";

    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["summary"] = "Pause memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["responses"]["200"]["description"] = "Profiler paused";

    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["summary"] = "Resume memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["responses"]["200"]["description"] =
        "Profiler resumed";

    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["summary"] = "Clear memory profiler data";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["responses"]["200"]["description"] =
        "Profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["summary"] = "Get memory profiler status";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["description"] =
        "Get current memory profiler status including session state, tracking mode, and feature enabled status.";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["responses"]["200"]["description"] =
        "Memory profiler status";

    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["summary"] = "Get per-page access summaries";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["description"] =
        "Get read/write/execute access counts aggregated per physical memory page.";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["description"] =
        "Maximum pages to return (default: all active)";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["responses"]["200"]["description"] =
        "Per-page access summaries";

    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["summary"] = "Get address-level access counters";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["description"] =
        "Get detailed read/write/execute counters for each address within a page or Z80 address space.";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["name"] = "page";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["description"] =
        "Physical page number (0-based)";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["name"] = "mode";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["description"] =
        "Address mode: z80 or physical (default: physical)";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["responses"]["200"]["description"] =
        "Address-level access counters";

    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["summary"] = "Get monitored region statistics";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["description"] =
        "Get access statistics for all monitored memory regions.";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["responses"]["200"]["description"] =
        "Monitored region statistics";

    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["summary"] = "Save access data to file";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["description"] =
        "Save memory access profiling data to a file in the specified format.";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Output file or directory path";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["description"] = "Output format (yaml)";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["responses"]["200"]["description"] =
        "Data saved successfully";

    // Call Trace Profiler control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["summary"] = "Start call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["description"] =
        "Start call trace profiler session. Tracks CALL/RET/JP/JR/RST events.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["responses"]["200"]["description"] =
        "Profiler started";

    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["summary"] = "Stop call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["responses"]["200"]["description"] =
        "Profiler stopped";

    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["summary"] = "Pause call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["responses"]["200"]["description"] =
        "Profiler paused";

    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["summary"] = "Resume call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["responses"]["200"]["description"] =
        "Profiler resumed";

    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["summary"] = "Clear call trace profiler data";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["responses"]["200"]["description"] =
        "Profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["summary"] = "Get call trace profiler status";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["description"] =
        "Get current call trace profiler status including session state, entry count, and buffer capacity.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["responses"]["200"]["description"] =
        "Call trace profiler status";

    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["summary"] = "Get call trace entries";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["description"] =
        "Get recent control flow trace entries (CALL, RET, JP, JR, RST events) with PC, SP, and timing.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["name"] = "count";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["description"] =
        "Number of entries to return (default: 100)";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["responses"]["200"]["description"] =
        "Call trace entries";

    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["summary"] = "Get call/return statistics";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["description"] =
        "Get aggregated statistics including call counts, return counts, max call depth, and top targets.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["responses"]["200"]["description"] =
        "Call/return statistics";

    // Opcode Profiler control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["summary"] = "Start opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["description"] =
        "Start opcode profiler session. Enables feature and clears previous data.";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["responses"]["200"]["description"] =
        "Profiler started";

    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["summary"] = "Stop opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["description"] =
        "Stop opcode profiler session. Data is preserved.";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["responses"]["200"]["description"] = "Profiler stopped";

    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["summary"] = "Pause opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["description"] =
        "Pause profiler session. Data is retained.";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["responses"]["200"]["description"] = "Profiler paused";

    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["summary"] = "Resume opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["description"] = "Resume paused profiler session.";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["responses"]["200"]["description"] =
        "Profiler resumed";

    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["summary"] = "Clear opcode profiler data";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["description"] =
        "Clear all profiler data without changing session state.";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["responses"]["200"]["description"] =
        "Profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["summary"] = "Get opcode profiler status";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["description"] =
        "Get current profiler status including session state, total executions, and trace buffer size.";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["responses"]["200"]["description"] = "Profiler status";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["responses"]["200"]["content"]["application/json"]
         ["schema"]["$ref"] = "#/components/schemas/ProfilerStatusResponse";

    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["summary"] = "Get opcode execution counters";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["description"] =
        "Get top N opcodes by execution count, sorted by frequency.";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["description"] =
        "Maximum opcodes to return (default: 100)";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["responses"]["200"]["description"] =
        "Opcode counters";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["responses"]["200"]["content"]["application/json"]
         ["schema"]["$ref"] = "#/components/schemas/ProfilerCountersResponse";

    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["summary"] = "Get recent execution trace";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["description"] =
        "Get recent opcode execution trace with PC, prefix, opcode, and CPU state.";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["name"] = "count";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["description"] =
        "Number of trace entries (default: 100)";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["responses"]["200"]["description"] = "Execution trace";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["responses"]["200"]["content"]["application/json"]
         ["schema"]["$ref"] = "#/components/schemas/ProfilerTraceResponse";

    // Unified Profiler Control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["summary"] = "Start all profilers";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["description"] =
        "Start all profiler sessions (opcode, memory, calltrace) simultaneously.";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["responses"]["200"]["description"] = "All profilers started";

    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["summary"] = "Stop all profilers";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["responses"]["200"]["description"] = "All profilers stopped";

    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["summary"] = "Pause all profilers";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["responses"]["200"]["description"] = "All profilers paused";

    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["summary"] = "Resume all profilers";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["responses"]["200"]["description"] = "All profilers resumed";

    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["summary"] = "Clear all profiler data";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["responses"]["200"]["description"] =
        "All profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["summary"] = "Get status of all profilers";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["description"] =
        "Get current status of all profilers (opcode, memory, calltrace) including session states.";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["responses"]["200"]["description"] = "All profiler statuses";

