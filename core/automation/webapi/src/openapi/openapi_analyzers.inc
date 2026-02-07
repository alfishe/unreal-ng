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

