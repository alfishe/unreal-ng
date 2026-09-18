    // Analyzer Management endpoints
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["summary"] = "List all analyzers";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["responses"]["200"]["description"] =
        "List of registered analyzers with status";

    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["summary"] = "Get analyzer status";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["description"] =
        "Analyzer name (e.g., trdos)";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["responses"]["200"]["description"] = "Analyzer status";

    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["summary"] = "Enable or disable analyzer";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["description"] = "True to enable, false to disable";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["responses"]["200"]["description"] = "Analyzer state updated";

    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["summary"] = "Get analyzer events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["description"] =
        "Retrieve captured events from an analyzer. Use limit query param to control count.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["name"] = "limit";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["description"] =
        "Maximum number of events to return (default: 100)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["responses"]["200"]["description"] =
        "List of analyzer events";

    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["summary"] = "Clear analyzer events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["responses"]["200"]["description"] =
        "Events cleared";

    // Analyzer session control endpoint
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["summary"] = "Control analyzer session";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["description"] =
        "Activate or deactivate analyzer session. Activate clears event buffers for fresh capture.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["description"] =
        "Analyzer name (e.g., trdos)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["action"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["action"]["enum"]
             .append("activate");
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["action"]["enum"]
             .append("deactivate");
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]
         ["schema"]["properties"]["action"]["description"] = "Session action: activate or deactivate";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["responses"]["200"]["description"] =
        "Session action completed";

    // Raw FDC events endpoint
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["summary"] = "Get raw FDC events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["description"] =
        "Retrieve raw FDC port I/O events with Z80 CPU context. All values are JSON numbers.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["name"] = "limit";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["description"] =
        "Maximum number of events to return (default: 100)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["responses"]["200"]["description"] =
        "List of raw FDC events with Z80 main registers and 16-byte stack snapshot";

    // Raw breakpoint events endpoint
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["summary"] = "Get raw breakpoint events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["description"] =
        "Retrieve raw breakpoint hit events with complete Z80 state. Includes main, alternate, index, and special "
        "registers. All values are JSON numbers.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["name"] = "limit";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["description"] =
        "Maximum number of events to return (default: 100)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["responses"]["200"]["description"] =
        "List of raw breakpoint events with full Z80 state (main, alternate, IX, IY, I, R) and 16-byte stack snapshot";

    // Coverage analyzer endpoints (executed-address map)
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["summary"] = "Start coverage recording";
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["description"] =
        "Activate the coverage analyzer (bit map of executed Z80 addresses). Optional body: "
        "{\"clear\": true} resets recorded data on start (default true).";
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["clear"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/coverage/start"]["post"]["responses"]["200"]["description"] =
        "Coverage session started (active/recording/cleared flags)";

    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["summary"] = "Stop coverage recording";
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["description"] =
        "Deactivate the coverage analyzer. Recorded data is retained for reading.";
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage/stop"]["post"]["responses"]["200"]["description"] =
        "Coverage session stopped (executed_count/instructions retained)";

    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["summary"] = "Clear coverage data";
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["description"] =
        "Reset the recorded executed-address data (pause the emulator first for a clean reset).";
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage/clear"]["post"]["responses"]["200"]["description"] = "Coverage data cleared";

    paths["/api/v1/emulator/{id}/coverage"]["get"]["summary"] = "Read executed-address coverage";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/coverage"]["get"]["description"] =
        "Coverage statistics plus a compressed list of executed address ranges "
        "(start/end hex, size).";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][1]["name"] = "max_ranges";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][1]["description"] =
        "Maximum number of ranges to return (default: 512, 0 = unlimited)";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/coverage"]["get"]["responses"]["200"]["description"] =
        "Coverage report (executed_count/coverage_percent/ranges[]/truncated)";

    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["summary"] = "Coverage gap analysis";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["description"] =
        "Address ranges within [start, end] that were never executed.";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][1]["name"] = "start";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][1]["description"] =
        "Window start address, hex or decimal (default: 0x4000)";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][2]["name"] = "end";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][2]["description"] =
        "Window end address, hex or decimal (default: 0xFFFF)";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][3]["name"] = "max_gaps";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][3]["description"] =
        "Maximum number of gap ranges to return (default: 256)";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/coverage/gaps"]["get"]["responses"]["200"]["description"] =
        "Gap ranges within the requested window (gaps[]/total_gap_bytes)";

    // AY register-write log endpoints (TurboSound tap)
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["summary"] = "Control AY register-write log";
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["description"] =
        "Manage the AY register-write log session. Records select/write/chip-switch events "
        "with PC, tacts and frame attribution for both TurboSound chips.";
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("start");
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("stop");
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("clear");
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["capacity"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ay/log"]["post"]["responses"]["200"]["description"] =
        "Log session updated (active/recording/capacity; stop reports entry_count/dropped)";

    paths["/api/v1/emulator/{id}/ay/log"]["get"]["summary"] = "Read AY register-write entries";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["tags"].append("Analyzers");
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["description"] =
        "Read recorded AY entries (chronological) with per-register write statistics. "
        "tail=true returns the newest `limit` entries.";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][2]["name"] = "offset";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][3]["name"] = "tail";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][3]["description"] =
        "Read the newest `limit` entries instead of offset-based paging";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["parameters"][3]["schema"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/ay/log"]["get"]["responses"]["200"]["description"] =
        "Entries (index/frame/tacts/pc/port/chip/type/reg/value) + per-register stats";

