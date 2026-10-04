    // Breakpoint endpoints
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["summary"] = "List all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["description"] =
        "Every breakpoint: id, type, address, the access flags, active, note, group; address_end for a range; page "
        "({kind, page}) and slot_only for a physical one; port_mask for a masked port; hit_mode, hit_target and the "
        "live hit_count.";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["responses"]["200"]["description"] = "List of breakpoints";

    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["summary"] = "Add breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/AddBreakpointRequest";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["description"] =
        "An execution, read, write, rw, port_in, port_out or port breakpoint: one address or a range (address_end), "
        "physical with 'page' (fires on that page through any slot that shows it; slot_only: through one slot), a port "
        "with a mask, a hit policy (hits / hit_mode + hit_target). Errors name the field: a range ending before it "
        "starts, a page range leaving its page, a page the machine lacks, a mask on memory, a policy without a target.";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["description"] = "Breakpoint created";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["400"]["description"] =
        "Bad address or type, a page on a port breakpoint, a malformed page or one the machine does not have, a range "
        "that ends before it starts or leaves its page, a mask on a memory breakpoint, a hit policy without a target";
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
        "mode; last_triggered_page ({kind, page}) when it is bound to a page; last_triggered_hit_count";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["description"] = "Breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["content"]["application/json"]
         ["schema"]["$ref"] = "#/components/schemas/BreakpointStatusResponse";

    // Hit counters (conditional-breakpoints design F5)
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["summary"] = "Reset breakpoint hit counters";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["description"] =
        "hit_count back to 0: body {\"id\": N} for one breakpoint, no body (or no id) for all. A hit policy counts "
        "again from there.";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["responses"]["200"]["description"] = "Counters reset";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["responses"]["404"]["description"] = "No such breakpoint";
