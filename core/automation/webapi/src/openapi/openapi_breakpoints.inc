    // Breakpoint endpoints
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
